extern crate alloc;

// Host oracle boundary: only physical storage, clock, randomness and path hash are substituted.
use super::{
    io::{File, Io, Mode},
    record::Record,
    Store,
};
use alloc::{boxed::Box, vec::Vec};
use core::{ffi::c_void, ptr::NonNull, slice};

#[repr(C)]
pub struct Bytes {
    data: *const u8,
    length: usize,
}
impl Bytes {
    unsafe fn bytes(&self) -> &[u8] {
        if self.length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.data, self.length) }
        }
    }
    fn from(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            length: bytes.len(),
        }
    }
}

#[repr(C)]
pub struct Operations {
    xpc_test_open: unsafe extern "C" fn(path: Bytes, mode: u8, checked: u8) -> *mut c_void,
    xpc_test_read: unsafe extern "C" fn(file: *mut c_void, out: *mut u8, len: usize) -> i32,
    xpc_test_write: unsafe extern "C" fn(file: *mut c_void, data: *const u8, len: usize) -> usize,
    xpc_test_seek: unsafe extern "C" fn(file: *mut c_void, offset: u32, relative: u8) -> u8,
    xpc_test_position: unsafe extern "C" fn(file: *mut c_void) -> u32,
    xpc_test_available: unsafe extern "C" fn(file: *mut c_void) -> i32,
    xpc_test_size: unsafe extern "C" fn(file: *mut c_void) -> u32,
    xpc_test_truncate: unsafe extern "C" fn(file: *mut c_void, size: u32) -> u8,
    xpc_test_flush: unsafe extern "C" fn(file: *mut c_void),
    xpc_test_close: unsafe extern "C" fn(file: *mut c_void),
    xpc_test_drop: unsafe extern "C" fn(file: *mut c_void),
    xpc_test_directory: unsafe extern "C" fn(file: *mut c_void) -> u8,
    xpc_test_next: unsafe extern "C" fn(file: *mut c_void) -> *mut c_void,
    xpc_test_name: unsafe extern "C" fn(file: *mut c_void, out: *mut u8, size: usize) -> usize,
    xpc_test_exists: unsafe extern "C" fn(path: Bytes) -> u8,
    xpc_test_mkdir: unsafe extern "C" fn(path: Bytes),
    xpc_test_remove: unsafe extern "C" fn(path: Bytes) -> u8,
    xpc_test_rename: unsafe extern "C" fn(from: Bytes, to: Bytes) -> u8,
    xpc_test_hash: unsafe extern "C" fn(path: Bytes) -> u64,
    xpc_test_epoch: unsafe extern "C" fn() -> i64,
    xpc_test_random: unsafe extern "C" fn(out: *mut u8, size: usize),
}

pub struct Host(&'static Operations);
pub struct Handle {
    value: NonNull<c_void>,
    io: &'static Operations,
}
impl Drop for Handle {
    fn drop(&mut self) {
        unsafe {
            (self.io.xpc_test_drop)(self.value.as_ptr());
        }
    }
}
impl File for Handle {
    fn read(&mut self, bytes: &mut [u8]) -> i32 {
        unsafe { (self.io.xpc_test_read)(self.value.as_ptr(), bytes.as_mut_ptr(), bytes.len()) }
    }
    fn write(&mut self, bytes: &[u8]) -> usize {
        unsafe { (self.io.xpc_test_write)(self.value.as_ptr(), bytes.as_ptr(), bytes.len()) }
    }
    fn seek(&mut self, offset: u32) -> bool {
        unsafe { (self.io.xpc_test_seek)(self.value.as_ptr(), offset, 0) != 0 }
    }
    fn seek_relative(&mut self, offset: u32) -> bool {
        unsafe { (self.io.xpc_test_seek)(self.value.as_ptr(), offset, 1) != 0 }
    }
    fn position(&self) -> u32 {
        unsafe { (self.io.xpc_test_position)(self.value.as_ptr()) }
    }
    fn available(&self) -> i32 {
        unsafe { (self.io.xpc_test_available)(self.value.as_ptr()) }
    }
    fn size(&self) -> u32 {
        unsafe { (self.io.xpc_test_size)(self.value.as_ptr()) }
    }
    fn truncate(&mut self, size: u32) -> bool {
        unsafe { (self.io.xpc_test_truncate)(self.value.as_ptr(), size) != 0 }
    }
    fn flush(&mut self) {
        unsafe {
            (self.io.xpc_test_flush)(self.value.as_ptr());
        }
    }
    fn close(&mut self) {
        unsafe {
            (self.io.xpc_test_close)(self.value.as_ptr());
        }
    }
    fn is_directory(&self) -> bool {
        unsafe { (self.io.xpc_test_directory)(self.value.as_ptr()) != 0 }
    }
    fn next(&mut self) -> Option<Self> {
        NonNull::new(unsafe { (self.io.xpc_test_next)(self.value.as_ptr()) })
            .map(|value| Self { value, io: self.io })
    }
    fn name(&self, out: &mut [u8]) -> usize {
        unsafe { (self.io.xpc_test_name)(self.value.as_ptr(), out.as_mut_ptr(), out.len()) }
    }
}
impl Io for Host {
    type File = Handle;
    fn open(&self, path: &[u8], mode: Mode) -> Option<Handle> {
        let mode = match mode {
            Mode::Read => 0,
            Mode::Rewrite => 1,
            Mode::Append => 2,
            Mode::ReadWrite => 3,
        };
        NonNull::new(unsafe { (self.0.xpc_test_open)(Bytes::from(path), mode, 0) })
            .map(|value| Handle { value, io: self.0 })
    }
    fn open_read(&self, path: &[u8]) -> Option<Handle> {
        NonNull::new(unsafe { (self.0.xpc_test_open)(Bytes::from(path), 0, 1) })
            .map(|value| Handle { value, io: self.0 })
    }
    fn exists(&self, path: &[u8]) -> bool {
        unsafe { (self.0.xpc_test_exists)(Bytes::from(path)) != 0 }
    }
    fn mkdir(&self, path: &[u8]) {
        unsafe {
            (self.0.xpc_test_mkdir)(Bytes::from(path));
        }
    }
    fn remove(&self, path: &[u8]) -> bool {
        unsafe { (self.0.xpc_test_remove)(Bytes::from(path)) != 0 }
    }
    fn rename(&self, from: &[u8], to: &[u8]) -> bool {
        unsafe { (self.0.xpc_test_rename)(Bytes::from(from), Bytes::from(to)) != 0 }
    }
    fn path_hash(&self, path: &[u8]) -> u64 {
        unsafe { (self.0.xpc_test_hash)(Bytes::from(path)) }
    }
    fn epoch_seconds(&self) -> i64 {
        unsafe { (self.0.xpc_test_epoch)() }
    }
    fn random(&self, out: &mut [u8]) {
        unsafe {
            (self.0.xpc_test_random)(out.as_mut_ptr(), out.len());
        }
    }
}

type Owner = Store<Host, 16>;

#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_create(operations: *const Operations) -> *mut Owner {
    let Some(operations) = (unsafe { operations.as_ref() }) else {
        return core::ptr::null_mut();
    };
    Owner::try_new(Host(operations)).map_or(core::ptr::null_mut(), Box::into_raw)
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_destroy(store: *mut Owner) {
    unsafe {
        drop(Box::from_raw(store));
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_load(
    store: &mut Owner,
    book: Bytes,
    title: Bytes,
    author: Bytes,
    kind: Bytes,
) -> u8 {
    unsafe { store.load(book.bytes(), title.bytes(), author.bytes(), kind.bytes()) as u8 }
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_unload(store: &mut Owner) {
    store.unload();
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_count(store: &Owner) -> usize {
    store.count()
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_row(
    store: &Owner,
    index: usize,
    out: *mut Record,
) -> u8 {
    let Some(row) = store.row(index) else {
        return 0;
    };
    unsafe {
        out.write(*row);
    }
    1
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_add(
    store: &mut Owner,
    record: &Record,
    chapter: Bytes,
    text: Bytes,
) -> u8 {
    unsafe { store.add(*record, chapter.bytes(), text.bytes()) as u8 }
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_remove(store: &mut Owner, index: usize) -> u8 {
    store.remove(index) as u8
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_save(store: &mut Owner) -> u8 {
    store.save() as u8
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_sync(store: &mut Owner) -> u8 {
    store.prepare_sync() as u8
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_uploads(store: &mut Owner) -> u8 {
    store.finish_uploads() as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_remote(
    store: &mut Owner,
    row: &Record,
    text: Bytes,
    deleted: u8,
) -> u8 {
    unsafe { store.apply_remote(*row, text.bytes(), deleted != 0) as u8 }
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_next_deletion(
    store: &Owner,
    offset: &mut u32,
    id: &mut [u8; 65],
) -> u8 {
    store.next_deletion(offset, id) as u8
}
#[no_mangle]
pub extern "C" fn xp_clippings_test_finish_deletions(store: &Owner) -> u8 {
    store.finish_deletions() as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_text(
    store: &Owner,
    index: usize,
    out: *mut u8,
    capacity: usize,
    length: &mut usize,
) -> u8 {
    let mut text = Vec::new();
    let ok = store.read_text(index, &mut text);
    *length = text.len();
    if text.len() > capacity {
        return 0;
    }
    if !text.is_empty() {
        unsafe {
            core::ptr::copy_nonoverlapping(text.as_ptr(), out, text.len());
        }
    }
    ok as u8
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_preview(
    store: &Owner,
    index: usize,
    out: *mut u8,
    capacity: usize,
    length: &mut usize,
) -> u8 {
    unsafe { store.preview(index, slice::from_raw_parts_mut(out, capacity), length) as u8 }
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_move(store: &Owner, from: Bytes, to: Bytes) -> u8 {
    unsafe { store.move_book(from.bytes(), to.bytes()) as u8 }
}
#[no_mangle]
pub unsafe extern "C" fn xp_clippings_test_delete_path(
    store: &Owner,
    path: Bytes,
    kind: Bytes,
) -> u8 {
    unsafe { store.delete_for_path(path.bytes(), kind.bytes()) as u8 }
}
