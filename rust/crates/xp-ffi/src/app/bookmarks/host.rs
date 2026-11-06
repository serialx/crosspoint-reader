// Host-only boundary: the real record/persistence code uses extracted SDK I/O.
use super::{
    alloc::{boxed::Box, vec::Vec},
    persistence::{self, Storage},
    Name, Record, Text,
};
use core::{ffi::c_void, slice};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bytes {
    pub data: *const u8,
    pub length: usize,
}
impl Bytes {
    pub(super) unsafe fn bytes(&self) -> &[u8] {
        if self.length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.data, self.length) }
        }
    }
    pub(super) fn from(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            length: bytes.len(),
        }
    }
}
#[repr(C)]
pub struct Row {
    xpath: Bytes,
    summary: Bytes,
    name: Bytes,
    percentage: f32,
    spine: u16,
    count: u16,
    page: u16,
    has_offset: u8,
    offset: u32,
}
#[repr(C)]
pub struct Operations {
    pub exists: unsafe extern "C" fn(Bytes) -> u8,
    pub mkdir: unsafe extern "C" fn(Bytes),
    pub read:
        unsafe extern "C" fn(Bytes, unsafe extern "C" fn(*mut c_void, Bytes), *mut c_void) -> u8,
    pub write: unsafe extern "C" fn(Bytes, Bytes) -> u8,
}
pub struct Host(pub &'static Operations);
impl Storage for Host {
    fn exists(&self, path: &[u8]) -> bool {
        unsafe { (self.0.exists)(Bytes::from(path)) != 0 }
    }
    fn mkdir(&self, path: &[u8]) {
        unsafe { (self.0.mkdir)(Bytes::from(path)) };
    }
    fn read_file<T>(&self, path: &[u8], consume: impl FnOnce(&[u8]) -> T) -> Option<T> {
        struct Call<F, T> {
            consume: Option<F>,
            result: Option<T>,
        }
        unsafe extern "C" fn receive<F: FnOnce(&[u8]) -> T, T>(context: *mut c_void, bytes: Bytes) {
            let call = unsafe { &mut *context.cast::<Call<F, T>>() };
            let consume = call.consume.take().expect("one read delivery");
            call.result = Some(consume(unsafe { bytes.bytes() }));
        }
        fn run<F: FnOnce(&[u8]) -> T, T>(host: &Host, path: &[u8], consume: F) -> Option<T> {
            let mut call = Call {
                consume: Some(consume),
                result: None,
            };
            let ok = unsafe {
                (host.0.read)(
                    Bytes::from(path),
                    receive::<F, T>,
                    (&mut call as *mut Call<F, T>).cast(),
                )
            };
            if ok != 0 {
                call.result
            } else {
                None
            }
        }
        run(self, path, consume)
    }
    fn write_file_atomic(&self, path: &[u8], bytes: &[u8]) -> bool {
        unsafe { (self.0.write)(Bytes::from(path), Bytes::from(bytes)) != 0 }
    }
    fn log(&self, _: u8, _: &[u8], _: core::fmt::Arguments<'_>, _: &[&[u8]]) {}
}
pub struct Owner {
    io: Host,
    rows: Vec<Record<Vec<u8>>>,
}

#[no_mangle]
pub extern "C" fn xp_bookmarks_test_create(operations: &'static Operations) -> *mut Owner {
    Box::into_raw(Box::new(Owner {
        io: Host(operations),
        rows: Vec::new(),
    }))
}
#[no_mangle]
/// # Safety
/// `owner` is null or the unique live pointer returned by test_create.
pub unsafe extern "C" fn xp_bookmarks_test_destroy(owner: *mut Owner) {
    if !owner.is_null() {
        drop(unsafe { Box::from_raw(owner) });
    }
}
#[no_mangle]
/// # Safety
/// Every row span is readable for this call and does not alias mutated storage.
pub unsafe extern "C" fn xp_bookmarks_test_push(owner: &mut Owner, row: &Row) -> u8 {
    fn copy(bytes: &[u8]) -> Result<Vec<u8>, super::Error> {
        Vec::read(bytes.len(), |out| out.copy_from_slice(bytes))
    }
    let build = || -> Result<Record<Vec<u8>>, super::Error> {
        Ok(Record {
            xpath: Text::copy(unsafe { row.xpath.bytes() })
                .map_err(|_| super::Error::Allocation)?,
            summary: Text::copy(unsafe { row.summary.bytes() })
                .map_err(|_| super::Error::Allocation)?,
            name: copy(unsafe { row.name.bytes() })?,
            percentage: row.percentage,
            spine: row.spine,
            page_count: row.count,
            page: row.page,
            visible_offset: (row.has_offset != 0).then_some(row.offset),
        })
    };
    let Ok(row) = build() else {
        return 0;
    };
    if owner.rows.try_reserve(1).is_err() {
        return 0;
    }
    owner.rows.push(row);
    1
}
#[no_mangle]
pub extern "C" fn xp_bookmarks_test_count(owner: &Owner) -> usize {
    owner.rows.len()
}
#[no_mangle]
pub extern "C" fn xp_bookmarks_test_row(owner: &Owner, index: usize, out: &mut Row) -> u8 {
    let Some(row) = owner.rows.get(index) else {
        return 0;
    };
    *out = Row {
        xpath: Bytes::from(row.xpath.bytes()),
        summary: Bytes::from(row.summary.bytes()),
        name: Bytes::from(&row.name),
        percentage: row.percentage,
        spine: row.spine,
        count: row.page_count,
        page: row.page,
        has_offset: u8::from(row.visible_offset.is_some()),
        offset: row.visible_offset.unwrap_or(0),
    };
    1
}
#[no_mangle]
/// # Safety
/// The book span is readable; physical callbacks do not reenter this owner.
pub unsafe extern "C" fn xp_bookmarks_test_load(owner: &mut Owner, book: Bytes) -> u8 {
    owner.rows = Vec::new();
    let Some(rows) = persistence::read(&owner.io, unsafe { book.bytes() }) else {
        return 0;
    };
    owner.rows = rows;
    1
}
#[no_mangle]
/// # Safety
/// The book span is readable; physical callbacks do not mutate this owner.
pub unsafe extern "C" fn xp_bookmarks_test_save(owner: &Owner, book: Bytes) -> u8 {
    let prepared = persistence::prepare(&owner.rows);
    u8::from(persistence::write(
        &owner.io,
        unsafe { book.bytes() },
        prepared,
    ))
}
#[no_mangle]
/// # Safety
/// The input span is readable; sink uses context only during this call and
/// neither retains nor modifies the borrowed output span.
pub unsafe extern "C" fn xp_bookmarks_test_path(
    book: Bytes,
    sink: unsafe extern "C" fn(*mut c_void, Bytes),
    context: *mut c_void,
) {
    let path = persistence::path(unsafe { book.bytes() }).expect("host path allocation");
    unsafe { sink(context, Bytes::from(&path)) };
}
