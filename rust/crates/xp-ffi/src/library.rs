//! Library ABI borrows all data for the call and never retains callback buffers.
use core::{ffi::c_void, slice};
use xp_core::library::{
    format::{self, Header},
    text,
};
unsafe fn bytes<'a>(p: *const u8, n: usize) -> &'a [u8] {
    if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n)
    }
}
/// # Safety
/// `data` is an exclusive writable buffer of `len` bytes, or null when empty.
/// The returned prefix is the transformed string; no terminator is written.
#[no_mangle]
pub unsafe extern "C" fn xp_library_text(data: *mut u8, len: usize, operation: u8) -> usize {
    let b = if len == 0 {
        &mut []
    } else {
        slice::from_raw_parts_mut(data, len)
    };
    match operation {
        0 => text::fold(b),
        1 => text::clean(b),
        2 => text::author(b),
        3 => text::surname(b),
        _ => 0,
    }
}
/// # Safety
/// Both inputs borrow their specified lengths, or are null for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_library_matches(
    a: *const u8,
    an: usize,
    b: *const u8,
    bn: usize,
) -> u8 {
    u8::from(text::matches(bytes(a, an), bytes(b, bn)))
}
/// # Safety
/// Input borrows `len` readable bytes, or is null for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_library_initial(data: *const u8, len: usize) -> u32 {
    text::initial(bytes(data, len))
}
/// # Safety
/// Input borrows `len` readable bytes, or is null for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_library_hash(data: *const u8, len: usize) -> u64 {
    xp_core::fnv1a64(bytes(data, len))
}
#[no_mangle]
pub extern "C" fn xp_library_align(value: u32) -> u32 {
    format::align(value)
}
/// # Safety
/// `header` borrows 64 writable bytes containing a CLX1 header.
#[no_mangle]
pub unsafe extern "C" fn xp_library_layout(header: *mut c_void, folders: u32, names: u32) {
    let mut h = header.cast::<Header>().read_unaligned();
    format::layout(&mut h, folders, names);
    header.cast::<Header>().write_unaligned(h);
}
/// # Safety
/// `header` borrows 64 readable bytes containing a CLX1 header.
#[no_mangle]
pub unsafe extern "C" fn xp_library_validate(header: *const c_void, size: u64, stale: u8) -> u8 {
    format::validate(&header.cast::<Header>().read_unaligned(), size, stale != 0)
}
/// # Safety
/// `header` borrows 64 readable bytes containing a CLX1 header.
#[no_mangle]
pub unsafe extern "C" fn xp_library_offset(header: *const c_void, n: u16, section: u8) -> u32 {
    let h = header.cast::<Header>().read_unaligned();
    match section {
        0 => format::record_offset(&h, n),
        1 => format::order_offset(&h, n, false),
        _ => format::order_offset(&h, n, true),
    }
}
#[path = "library/builder.rs"]
mod builder;
use xp_core::library::{
    format::Record,
    reader::{self, Identity, Read},
};
#[repr(C)]
pub struct IndexIo {
    context: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> u8,
    string: unsafe extern "C" fn(*mut c_void, u32, usize, u8, *mut u8) -> u8,
    append: unsafe extern "C" fn(*mut c_void, *const u8, usize),
    fail: unsafe extern "C" fn(*mut c_void),
}
impl Read for IndexIo {
    fn read(&mut self, at: u32, out: &mut [u8]) -> bool {
        unsafe { (self.read)(self.context, at, out.as_mut_ptr(), out.len()) != 0 }
    }
    fn string(&mut self, at: u32, len: usize, append: bool) -> Option<u8> {
        let mut last = 0;
        if unsafe { (self.string)(self.context, at, len, u8::from(append), &mut last) != 0 } {
            Some(last)
        } else {
            None
        }
    }
    fn append(&mut self, b: &[u8]) {
        unsafe {
            (self.append)(self.context, b.as_ptr(), b.len());
        }
    }
    fn fail(&mut self) {
        unsafe {
            (self.fail)(self.context);
        }
    }
}
/// # Safety
/// Header and record point to readable CLX1 structs. For operation 0, record
/// instead points to 128 writable bytes which need not be initialized. IO callbacks borrow a live exclusive context, never retain
/// buffers or reenter Rust. Output points to eight writable bytes for hash.
#[no_mangle]
pub unsafe extern "C" fn xp_library_read(
    header: *const c_void,
    io: *mut IndexIo,
    record: *mut c_void,
    operation: u8,
    arg: u32,
    output: *mut u64,
) -> u32 {
    let h = header.cast::<Header>().read_unaligned();
    let io = &mut *io;
    if operation == 1 {
        return u32::from(reader::ordinal(&h, io, (arg >> 16) as u8, arg as u16));
    }
    if operation == 0 {
        if arg >= u32::from(h.books)
            || (io.read)(
                io.context,
                format::record_offset(&h, arg as u16),
                record.cast(),
                128,
            ) == 0
        {
            return 0;
        }
        let mut r = record.cast::<Record>().read_unaligned();
        let ok = reader::sanitize(&h, &mut r);
        record.cast::<Record>().write_unaligned(r);
        return u32::from(ok);
    }
    let r = record.cast::<Record>().read_unaligned();
    let ok = match operation {
        2 => {
            let hash = reader::hash(&h, io, &r);
            output.write(hash.unwrap_or(0));
            hash.is_some()
        }
        3 => reader::name(&h, io, &r, false),
        4 => reader::blob(&h, io, &r, arg as u8),
        5 => reader::path(&h, io, &r),
        _ => false,
    };
    u32::from(ok)
}
/// # Safety
/// Header, IO, identities and rows are valid disjoint buffers; count <= 16.
/// Chunk is 4096 writable bytes. Callback contract is as for xp_library_read.
#[no_mangle]
pub unsafe extern "C" fn xp_library_recent(
    header: *const c_void,
    io: *mut IndexIo,
    books: *const Identity,
    count: usize,
    rows: *mut u16,
    chunk: *mut u8,
) -> u8 {
    let h = header.cast::<Header>().read_unaligned();
    u8::from(reader::recent(
        &h,
        &mut *io,
        slice::from_raw_parts(books, count),
        slice::from_raw_parts_mut(rows, count),
        slice::from_raw_parts_mut(chunk, 4096),
    ))
}
/// # Safety
/// Inputs borrow their lengths; output has folder_len + name_len + 1 bytes
/// and is disjoint from both inputs. No terminator is written.
#[no_mangle]
pub unsafe extern "C" fn xp_library_join(
    folder: *const u8,
    folder_len: usize,
    name: *const u8,
    name_len: usize,
    out: *mut u8,
) -> usize {
    let folder = bytes(folder, folder_len);
    let name = bytes(name, name_len);
    let b = slice::from_raw_parts_mut(out, folder_len + name_len + 1);
    b[..folder_len].copy_from_slice(folder);
    let sep = usize::from(folder.last() != Some(&b'/'));
    if sep > 0 {
        b[folder_len] = b'/';
    }
    b[folder_len + sep..folder_len + sep + name_len].copy_from_slice(name);
    folder_len + sep + name_len
}
/// # Safety
/// Path borrows length bytes (without NUL), or is null for zero length.
#[no_mangle]
pub unsafe extern "C" fn xp_library_book(path: *const u8, len: usize) -> u8 {
    u8::from(builder::book(bytes(path, len)))
}
/// # Safety
/// Services and callbacks remain live for the call. Callbacks never unwind,
/// retain borrowed buffers, or reenter Rust. Allocation returns zeroed,
/// max_align_t-aligned exclusive storage, released by the matching callback.
/// Metadata lends a writable title and readable author until the release call
/// (operation 2, with null path/output arguments), which frees both owners.
/// Root borrows len bytes including a final NUL; stats and dirty are exclusive.
/// Returns Failed=0, Unchanged=1, EmitFailed=2, Installed=3. After Installed,
/// the caller logs the summary and invokes xp_library_dirty with operation 2.
#[no_mangle]
pub unsafe extern "C" fn xp_library_build(
    services: *const builder::Services,
    root: *const u8,
    len: usize,
    stats: *mut builder::Stats,
    metadata: u8,
    dirty: *mut u8,
) -> u8 {
    builder::build(
        &*services,
        bytes(root, len),
        &mut *stats,
        metadata != 0,
        &mut *dirty,
    ) as u8
}
/// # Safety
/// Service contract is as for xp_library_build; dirty is exclusive writable
/// storage retained by the caller between calls. Operation: query=0, mark=1, clear=2.
#[no_mangle]
pub unsafe extern "C" fn xp_library_dirty(
    services: *const builder::Services,
    dirty: *mut u8,
    mark: u8,
) -> u8 {
    u8::from(builder::dirty(&*services, &mut *dirty, mark))
}
