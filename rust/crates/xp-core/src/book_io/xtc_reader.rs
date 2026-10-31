//! XTC file lifetime, lazy metadata and page reads over a HAL callback table.
use crate::xtc::{self, ChapterAction, Entry, Header, OK, READ_ERROR};
use core::{cell::Cell, ffi::c_void};

const FILE_NOT_FOUND: i32 = 1;
const PAGE_OUT_OF_RANGE: i32 = 5;
const MEMORY_ERROR: i32 = 8;

#[repr(u32)]
enum Diagnostic {
    HeaderMagic = 1,
    HeaderVersion = 2,
    Header = 3,
    Title = 4,
    Author = 5,
    TableZero = 6,
    TableBounds = 7,
    TableSeek = 8,
    FirstEntry = 9,
    TableValidated = 10,
    HeaderRead = 11,
    TitleRead = 12,
    AuthorRead = 13,
    FirstPageRead = 14,
    Opened = 15,
    Reopen = 16,
    EntrySeek = 17,
    EntryRead = 18,
    Chapters = 19,
    LazyChapters = 20,
    PageSeek = 21,
    PageHeader = 22,
    PageMagic = 23,
    PagePayload = 24,
    BufferSmall = 25,
    PageRead = 26,
}

#[repr(C)]
pub struct State {
    pub header: Cell<Header>,
    pub width: Cell<u16>,
    pub height: Cell<u16>,
    pub depth: Cell<u8>,
    pub opened: Cell<bool>,
    pub has_chapters: Cell<bool>,
    pub chapters_loaded: Cell<bool>,
    pub error: Cell<i32>,
}
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub open: unsafe extern "C" fn(*mut c_void) -> bool,
    pub is_open: unsafe extern "C" fn(*mut c_void) -> bool,
    pub close: unsafe extern "C" fn(*mut c_void),
    pub read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    pub seek: unsafe extern "C" fn(*mut c_void, u64) -> bool,
    pub size: unsafe extern "C" fn(*mut c_void) -> u64,
    pub metadata: unsafe extern "C" fn(*mut c_void, u32, *const u8, usize),
    pub chapters: unsafe extern "C" fn(*mut c_void, u32, usize, *const u8, u16, u16) -> usize,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32)>,
}
impl Services {
    unsafe fn log(&self, event: Diagnostic, a: u32, b: u32, c: u32) {
        if let Some(log) = self.diagnostic {
            log(self.context, event as u32, a, b, c, 0);
        }
    }
    unsafe fn read(&self, out: &mut [u8]) -> usize {
        let count = (self.read)(self.context, out.as_mut_ptr(), out.len());
        if count < 0 {
            usize::MAX
        } else {
            count as usize
        }
    }
    unsafe fn seek(&self, offset: u64) -> bool {
        (self.seek)(self.context, offset)
    }
    unsafe fn ensure_open(&self) -> bool {
        (self.is_open)(self.context) || (self.open)(self.context)
    }
    unsafe fn close_if_open(&self) {
        if (self.is_open)(self.context) {
            (self.close)(self.context);
        }
    }
    unsafe fn chapter_clear(&self) {
        (self.chapters)(self.context, 0, 0, core::ptr::null(), 0, 0);
    }
}

/// # Safety
/// Services obey their buffer bounds and borrow all pointers for their calls only.
/// They may read State for diagnostics but may not mutate it or reenter this reader.
pub unsafe fn close(state: &State, io: &Services) {
    io.close_if_open();
    state.opened.set(false);
    state.chapters_loaded.set(false);
    io.chapter_clear();
    (io.metadata)(io.context, 0, core::ptr::null(), 0);
    state.has_chapters.set(false);
    state.header.set(Header::default());
}

unsafe fn header(state: &State, io: &Services) -> i32 {
    let mut bytes = [0u8; 56];
    let count = io.read(&mut bytes);
    if count <= bytes.len() {
        core::ptr::copy_nonoverlapping(bytes.as_ptr(), state.header.as_ptr().cast(), count);
    }
    if count == bytes.len() {
        let depth = xtc::bit_depth(&bytes);
        if depth != 0 {
            state.depth.set(depth);
        }
    }
    let error = if count != bytes.len() {
        READ_ERROR
    } else {
        match xtc::header(&bytes) {
            Ok(header) => {
                state.header.set(header);
                OK
            }
            Err(error) => error,
        }
    };
    match error {
        xtc::INVALID_MAGIC => io.log(Diagnostic::HeaderMagic, 0, 0, 0),
        xtc::INVALID_VERSION => io.log(Diagnostic::HeaderVersion, 0, 0, 0),
        OK => io.log(Diagnostic::Header, 0, 0, 0),
        _ => {}
    }
    error
}
unsafe fn metadata(io: &Services, author: bool) -> i32 {
    if !io.seek(u64::from(xtc::metadata_offset(author))) {
        return READ_ERROR;
    }
    let mut bytes = [0u8; 128];
    let length = if author { 63 } else { 127 };
    io.read(&mut bytes[..length]);
    let size = xtc::text_len(&bytes[..length]);
    (io.metadata)(io.context, if author { 2 } else { 1 }, bytes.as_ptr(), size);
    io.log(
        if author {
            Diagnostic::Author
        } else {
            Diagnostic::Title
        },
        0,
        0,
        0,
    );
    OK
}
unsafe fn first_page(state: &State, io: &Services) -> i32 {
    let file_size = (io.size)(io.context);
    if !xtc::table_valid(&state.header.get(), file_size) {
        io.log(
            if state.header.get().page_table_offset == 0 {
                Diagnostic::TableZero
            } else {
                Diagnostic::TableBounds
            },
            file_size as u32,
            (file_size >> 32) as u32,
            0,
        );
        return xtc::CORRUPTED_HEADER;
    }
    if !io.seek(state.header.get().page_table_offset) {
        io.log(Diagnostic::TableSeek, 0, 0, 0);
        return READ_ERROR;
    }
    let mut bytes = [0u8; 16];
    if io.read(&mut bytes) != bytes.len() {
        io.log(Diagnostic::FirstEntry, 0, 0, 0);
        return READ_ERROR;
    }
    let Ok(entry) = xtc::entry(&bytes) else {
        return READ_ERROR;
    };
    state.width.set(entry.width);
    state.height.set(entry.height);
    io.log(Diagnostic::TableValidated, 0, 0, 0);
    OK
}

/// # Safety
/// The caller has closed the previous session and assigned its new path. Services
/// obey close's contract; metadata/chapters callbacks own the existing containers.
pub unsafe fn open(state: &State, io: &Services) -> i32 {
    if !(io.open)(io.context) {
        state.error.set(FILE_NOT_FOUND);
        return state.error.get();
    }
    state.error.set(header(state, io));
    if state.error.get() != OK {
        io.log(Diagnostic::HeaderRead, state.error.get() as u32, 0, 0);
        (io.close)(io.context);
        return state.error.get();
    }
    if state.header.get().has_metadata != 0 {
        state.error.set(metadata(io, false));
        if state.error.get() != OK {
            io.log(Diagnostic::TitleRead, state.error.get() as u32, 0, 0);
            (io.close)(io.context);
            return state.error.get();
        }
        state.error.set(metadata(io, true));
        if state.error.get() != OK {
            io.log(Diagnostic::AuthorRead, state.error.get() as u32, 0, 0);
            (io.close)(io.context);
            return state.error.get();
        }
        (io.metadata)(io.context, 3, core::ptr::null(), 0);
    }
    state.error.set(first_page(state, io));
    if state.error.get() != OK {
        io.log(Diagnostic::FirstPageRead, state.error.get() as u32, 0, 0);
        (io.close)(io.context);
        return state.error.get();
    }
    state
        .has_chapters
        .set(xtc::has_chapters(&state.header.get()));
    state.chapters_loaded.set(false);
    (io.close)(io.context);
    state.opened.set(true);
    io.log(Diagnostic::Opened, 0, 0, 0);
    OK
}

/// # Safety
/// Services obey close's contract. Entry is writable and disjoint from State.
pub unsafe fn page_info(state: &State, io: &Services, index: u32, out: &mut Entry) -> bool {
    if index >= u32::from(state.header.get().page_count) {
        return false;
    }
    if !io.ensure_open() {
        io.log(Diagnostic::Reopen, 0, 0, 0);
        return false;
    }
    let offset = state
        .header
        .get()
        .page_table_offset
        .wrapping_add(u64::from(index) * 16);
    if !io.seek(offset) {
        io.log(
            Diagnostic::EntrySeek,
            index,
            offset as u32,
            (offset >> 32) as u32,
        );
        return false;
    }
    let mut bytes = [0u8; 16];
    if io.read(&mut bytes) != bytes.len() {
        io.log(Diagnostic::EntryRead, index, 0, 0);
        return false;
    }
    let Ok(entry) = xtc::entry(&bytes) else {
        return false;
    };
    *out = entry;
    true
}

unsafe fn read_chapters(state: &State, io: &Services) -> i32 {
    io.chapter_clear();
    if !io.ensure_open() {
        return READ_ERROR;
    }
    let mut flag = [0u8; 1];
    if !io.seek(0x0b) || io.read(&mut flag) != 1 {
        return READ_ERROR;
    }
    if !xtc::chapter_flag(flag[0]) {
        return OK;
    }
    let mut bytes = [0u8; 96];
    if !io.seek(0x30) || io.read(&mut bytes[..8]) != 8 {
        return READ_ERROR;
    }
    let (offset, count) =
        xtc::chapter_span(&state.header.get(), &bytes[..8], (io.size)(io.context));
    let count = count as usize;
    if count == 0 {
        return OK;
    }
    if !io.seek(offset) {
        return READ_ERROR;
    }
    (io.chapters)(io.context, 1, count, core::ptr::null(), 0, 0);
    let mut emitted = 0;
    for _ in 0..count {
        if io.read(&mut bytes) != bytes.len() {
            return READ_ERROR;
        }
        let Ok(chapter) = xtc::chapter(&bytes, state.header.get().page_count) else {
            return READ_ERROR;
        };
        match chapter.action {
            ChapterAction::Skip => {}
            ChapterAction::Emit => {
                emitted = (io.chapters)(
                    io.context,
                    2,
                    usize::from(chapter.name_len),
                    bytes.as_ptr(),
                    chapter.start,
                    chapter.end,
                );
            }
            ChapterAction::Stop => break,
        }
    }
    state.has_chapters.set(emitted != 0);
    io.log(Diagnostic::Chapters, emitted as u32, 0, 0);
    OK
}

/// # Safety
/// Services obey close's contract.
pub unsafe fn chapters(state: &State, io: &Services) {
    if !state.chapters_loaded.get() && state.has_chapters.get() {
        let error = read_chapters(state, io);
        if error != OK {
            io.log(Diagnostic::LazyChapters, error as u32, 0, 0);
            state.has_chapters.set(false);
            io.chapter_clear();
        }
        state.chapters_loaded.set(true);
        io.close_if_open();
    }
}

/// # Safety
/// Output is writable for capacity bytes; callbacks obey close's contract.
pub unsafe fn load(
    state: &State,
    io: &Services,
    index: u32,
    output: *mut u8,
    capacity: usize,
) -> usize {
    state.error.set(if !state.opened.get() {
        FILE_NOT_FOUND
    } else if index >= u32::from(state.header.get().page_count) {
        PAGE_OUT_OF_RANGE
    } else {
        OK
    });
    if state.error.get() != OK {
        return 0;
    }
    let mut entry = Entry::default();
    if !page_info(state, io, index, &mut entry) {
        state.error.set(READ_ERROR);
        return 0;
    }
    if !io.ensure_open() {
        state.error.set(FILE_NOT_FOUND);
        return 0;
    }
    if !io.seek(entry.offset) {
        io.log(
            Diagnostic::PageSeek,
            index,
            entry.offset as u32,
            (entry.offset >> 32) as u32,
        );
        state.error.set(READ_ERROR);
        return 0;
    }
    let mut bytes = [0u8; 22];
    let count = io.read(&mut bytes);
    let result = if count != bytes.len() {
        Err(READ_ERROR)
    } else {
        xtc::page(&bytes, state.depth.get(), false)
    };
    let size = match result {
        Ok(size) => size as usize,
        Err(error) => {
            state.error.set(error);
            match error {
                READ_ERROR => io.log(Diagnostic::PageHeader, index, 0, 0),
                xtc::INVALID_MAGIC => io.log(
                    Diagnostic::PageMagic,
                    index,
                    u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
                    0,
                ),
                xtc::CORRUPTED_HEADER => io.log(
                    Diagnostic::PagePayload,
                    index,
                    u32::from_le_bytes([bytes[10], bytes[11], bytes[12], bytes[13]]),
                    xtc::bitmap_size(
                        u16::from_le_bytes([bytes[4], bytes[5]]),
                        u16::from_le_bytes([bytes[6], bytes[7]]),
                        state.depth.get(),
                    ),
                ),
                _ => {}
            }
            return 0;
        }
    };
    if capacity < size {
        io.log(Diagnostic::BufferSmall, size as u32, capacity as u32, 0);
        state.error.set(MEMORY_ERROR);
        return 0;
    }
    let count = (io.read)(io.context, output, size);
    if count < 0 || count as usize != size {
        io.log(Diagnostic::PageRead, size as u32, count as u32, 0);
        state.error.set(READ_ERROR);
        return 0;
    }
    state.error.set(OK);
    count as usize
}

#[repr(C)]
pub struct Stream {
    pub context: *mut c_void,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
    pub emit: unsafe extern "C" fn(*mut c_void, *const u8, usize, usize),
}
/// # Safety
/// IO obeys close's contract. Stream allocates initialized chunk storage, retains
/// its ownership until return, and borrows each emitted span only for the callback.
pub unsafe fn stream(
    state: &State,
    io: &Services,
    index: u32,
    chunk_size: usize,
    sink: &Stream,
) -> i32 {
    if !state.opened.get() {
        return FILE_NOT_FOUND;
    }
    if index >= u32::from(state.header.get().page_count) {
        return PAGE_OUT_OF_RANGE;
    }
    let mut entry = Entry::default();
    if !page_info(state, io, index, &mut entry) {
        return READ_ERROR;
    }
    if !io.ensure_open() {
        return FILE_NOT_FOUND;
    }
    if !io.seek(entry.offset) {
        return READ_ERROR;
    }
    let mut bytes = [0u8; 22];
    if io.read(&mut bytes) != bytes.len() {
        return READ_ERROR;
    }
    let size = match xtc::page(&bytes, state.depth.get(), true) {
        Ok(size) => size as usize,
        Err(error) => {
            if error == xtc::CORRUPTED_HEADER {
                io.log(
                    Diagnostic::PagePayload,
                    index,
                    u32::from_le_bytes([bytes[10], bytes[11], bytes[12], bytes[13]]),
                    xtc::bitmap_size(
                        u16::from_le_bytes([bytes[4], bytes[5]]),
                        u16::from_le_bytes([bytes[6], bytes[7]]),
                        state.depth.get(),
                    ),
                );
            }
            return error;
        }
    };
    let chunk = (sink.allocate)(sink.context, chunk_size);
    if chunk.is_null() && chunk_size != 0 {
        return MEMORY_ERROR;
    }
    let mut total = 0;
    while total < size {
        let request = chunk_size.min(size - total);
        let count = (io.read)(io.context, chunk, request);
        // A negative HAL read formerly became SIZE_MAX at the consumer boundary.
        if count <= 0 || count as usize > request {
            return READ_ERROR;
        }
        (sink.emit)(sink.context, chunk, count as usize, total);
        total += count as usize;
    }
    OK
}
