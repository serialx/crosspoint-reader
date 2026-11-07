//! XTC format resources in stable caller storage, over the existing HAL reader.
pub mod abi;
mod chapters;
mod text;

use abi::*;
use chapters::Chapters;
use core::{
    cell::{Cell, UnsafeCell},
    ffi::c_void,
    mem::size_of,
    ptr,
};
use text::Text;
use xp_core::{
    book_io::xtc_reader::{self, Services, State},
    xtc::{Entry, Header},
};

#[repr(C)]
pub struct Owner {
    state: State,
    path: UnsafeCell<Text>,
    chapters: UnsafeCell<Chapters>,
    title: UnsafeCell<Text>,
    author: UnsafeCell<Text>,
    leaves: *const XpXtcLeaves,
    context: *mut c_void,
}
const _: () = {
    assert!(size_of::<Owner>() == size_of::<XpXtcOwnerStorage>());
    assert!(core::mem::align_of::<Owner>() == core::mem::align_of::<XpXtcOwnerStorage>());
    assert!(size_of::<XpXtcChapterRow>() == if size_of::<usize>() == 4 { 28 } else { 40 });
};

unsafe fn leaves(owner: *mut Owner) -> &'static XpXtcLeaves {
    &*(*owner).leaves
}
unsafe fn state<'a>(owner: *mut Owner) -> &'a State {
    &*ptr::addr_of!((*owner).state)
}
unsafe fn path(owner: *mut Owner) -> *mut Text {
    ptr::addr_of_mut!((*owner).path).cast()
}
unsafe fn title(owner: *mut Owner) -> *mut Text {
    ptr::addr_of_mut!((*owner).title).cast()
}
unsafe fn author(owner: *mut Owner) -> *mut Text {
    ptr::addr_of_mut!((*owner).author).cast()
}
unsafe fn chapters(owner: *mut Owner) -> *mut Chapters {
    ptr::addr_of_mut!((*owner).chapters).cast()
}

unsafe fn services(owner: *mut Owner) -> Services {
    unsafe extern "C" fn open(p: *mut c_void) -> bool {
        let o = p.cast::<Owner>();
        (leaves(o).open)((*o).context, (*path(o)).data)
    }
    unsafe extern "C" fn is_open(p: *mut c_void) -> bool {
        let o = p.cast::<Owner>();
        (leaves(o).is_open)((*o).context)
    }
    unsafe extern "C" fn close(p: *mut c_void) {
        let o = p.cast::<Owner>();
        (leaves(o).close)((*o).context)
    }
    unsafe extern "C" fn read(p: *mut c_void, out: *mut u8, n: usize) -> i32 {
        let o = p.cast::<Owner>();
        (leaves(o).read)((*o).context, out, n)
    }
    unsafe extern "C" fn seek(p: *mut c_void, at: u64) -> bool {
        let o = p.cast::<Owner>();
        (leaves(o).seek)((*o).context, at)
    }
    unsafe extern "C" fn size(p: *mut c_void) -> u64 {
        let o = p.cast::<Owner>();
        (leaves(o).size)((*o).context)
    }
    unsafe extern "C" fn metadata(p: *mut c_void, action: u32, bytes: *const u8, n: usize) {
        let o = p.cast::<Owner>();
        match action {
            0 => {
                Text::clear(title(o));
                Text::clear(author(o));
            }
            1 => Text::assign(title(o), bytes, n, leaves(o)),
            2 => Text::assign(author(o), bytes, n, leaves(o)),
            3 => {
                Text::shrink(title(o), leaves(o));
                Text::shrink(author(o), leaves(o));
            }
            _ => {}
        }
    }
    unsafe extern "C" fn chapter(
        p: *mut c_void,
        action: u32,
        count: usize,
        bytes: *const u8,
        start: u16,
        end: u16,
    ) -> usize {
        let o = p.cast::<Owner>();
        match action {
            0 => Chapters::clear(chapters(o), leaves(o)),
            1 => Chapters::reserve(chapters(o), count, leaves(o)),
            2 => Chapters::append(chapters(o), bytes, count, start, end, leaves(o)),
            _ => {}
        }
        (*chapters(o)).length
    }
    unsafe extern "C" fn diagnostic(p: *mut c_void, event: u32, a: u32, b: u32, c: u32, d: u32) {
        let o = p.cast::<Owner>();
        (leaves(o).diagnostic)((*o).context, event, a, b, c, d);
    }
    Services {
        context: owner.cast(),
        open,
        is_open,
        close,
        read,
        seek,
        size,
        metadata,
        chapters: chapter,
        diagnostic: Some(diagnostic),
    }
}

/// # Safety
/// Storage is aligned, uninitialized and at its final address. Context and leaves
/// outlive it. Calls are serialized; callbacks may only inspect a non-lazy view.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_init(
    storage: *mut XpXtcOwnerStorage,
    context: *mut c_void,
    callbacks: *const XpXtcLeaves,
) {
    let owner = storage.cast::<Owner>();
    ptr::addr_of_mut!((*owner).state).write(State {
        header: Cell::new(Header::default()),
        width: Cell::new(480),
        height: Cell::new(800),
        depth: Cell::new(1),
        opened: Cell::new(false),
        has_chapters: Cell::new(false),
        chapters_loaded: Cell::new(false),
        error: Cell::new(0),
    });
    ptr::addr_of_mut!((*owner).leaves).write(callbacks);
    ptr::addr_of_mut!((*owner).context).write(context);
    Text::init(path(owner));
    Text::init(title(owner));
    Text::init(author(owner));
    chapters(owner).write(Chapters::new());
}
/// # Safety
/// Storage contains one live initialized owner, has no outstanding loans and will
/// not be used again. The native HalFile is destroyed after this call returns.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_drop(storage: *mut XpXtcOwnerStorage) {
    let owner = storage.cast::<Owner>();
    xp_xtc_owner_close(storage);
    Text::release(author(owner), leaves(owner));
    Text::release(title(owner), leaves(owner));
    Chapters::release(chapters(owner), leaves(owner));
    Text::release(path(owner), leaves(owner));
}
/// # Safety
/// Storage obeys init's contract. Path is a disjoint readable span for the call;
/// no old view/row/text loan is used after this mutation.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_open(
    storage: *mut XpXtcOwnerStorage,
    bytes: *const u8,
    length: usize,
) -> i32 {
    let owner = storage.cast::<Owner>();
    let io = services(owner);
    if state(owner).opened.get() {
        xtc_reader::close(state(owner), &io);
    }
    Text::assign(path(owner), bytes, length, leaves(owner));
    xtc_reader::open(state(owner), &io)
}
/// # Safety
/// Same initialized/serialized contract as init; invalidates existing loans.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_close(storage: *mut XpXtcOwnerStorage) {
    let owner = storage.cast::<Owner>();
    xtc_reader::close(state(owner), &services(owner));
}
/// # Safety
/// Same initialized/serialized contract. Output is disjoint writable storage.
/// With lazy=false only, a callback may inspect this snapshot during an operation.
/// Returned pointers are loans invalidated by close/open/drop or lazy mutation.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_view(
    storage: *mut XpXtcOwnerStorage,
    lazy: bool,
    out: *mut XpXtcView,
) {
    let owner = storage.cast::<Owner>();
    if lazy {
        xtc_reader::chapters(state(owner), &services(owner));
    }
    out.write(XpXtcView {
        state: ptr::addr_of!((*owner).state),
        title: (*title(owner)).data,
        title_len: (*title(owner)).length,
        author: (*author(owner)).data,
        author_len: (*author(owner)).length,
        chapters: (*chapters(owner)).data,
        chapter_count: (*chapters(owner)).length,
    });
}
/// # Safety
/// Same contract as open; output is disjoint writable Entry storage.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_page_info(
    storage: *mut XpXtcOwnerStorage,
    index: u32,
    out: *mut Entry,
) -> bool {
    let owner = storage.cast::<Owner>();
    let mut entry = Entry::default();
    if !xtc_reader::page_info(state(owner), &services(owner), index, &mut entry) {
        return false;
    }
    out.write(entry);
    true
}
/// # Safety
/// Same contract as open; output is a disjoint writable capacity-byte span.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_load(
    storage: *mut XpXtcOwnerStorage,
    index: u32,
    out: *mut u8,
    capacity: usize,
) -> usize {
    let owner = storage.cast::<Owner>();
    xtc_reader::load(state(owner), &services(owner), index, out, capacity)
}
/// # Safety
/// Same contract as open. Sink and context live for the call; each emitted byte
/// span is borrowed only until emit returns. Emit may inspect a non-lazy view.
#[no_mangle]
pub unsafe extern "C" fn xp_xtc_owner_stream(
    storage: *mut XpXtcOwnerStorage,
    index: u32,
    chunk_size: usize,
    sink: *const XpXtcStream,
) -> i32 {
    struct Chunk {
        owner: *mut Owner,
        sink: *const XpXtcStream,
        bytes: *mut u8,
        length: usize,
    }
    let owner = storage.cast::<Owner>();
    let mut chunk = Chunk {
        owner,
        sink,
        bytes: ptr::null_mut(),
        length: 0,
    };
    unsafe extern "C" fn allocate(ctx: *mut c_void, n: usize) -> *mut u8 {
        let chunk = ctx.cast::<Chunk>();
        if n != 0 {
            let bytes = (leaves((*chunk).owner).allocate)(n, ALLOC_STREAM);
            bytes.write_bytes(0, n);
            (*chunk).bytes = bytes;
            (*chunk).length = n;
        }
        (*chunk).bytes
    }
    unsafe extern "C" fn emit(ctx: *mut c_void, bytes: *const u8, n: usize, offset: usize) {
        let chunk = ctx.cast::<Chunk>();
        let sink = &*(*chunk).sink;
        (sink.emit)(sink.context, bytes, n, offset);
    }
    let stream = xtc_reader::Stream {
        context: ptr::addr_of_mut!(chunk).cast(),
        allocate,
        emit,
    };
    let result = xtc_reader::stream(state(owner), &services(owner), index, chunk_size, &stream);
    if chunk.length != 0 {
        (leaves(owner).release)(chunk.bytes, chunk.length);
    }
    result
}

#[cfg(test)]
mod tests;
