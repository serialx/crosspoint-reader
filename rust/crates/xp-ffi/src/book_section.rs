//! Synchronous section file policy over borrowed HAL services.
use crate::section_cache::{self as cache, Diagnostic};
use core::{ffi::c_void, slice};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FileServices {
    ctx: *mut c_void,
    operation: Option<unsafe extern "C" fn(*mut c_void, u32, usize) -> usize>,
    read: Option<unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> usize>,
    resize_text: Option<unsafe extern "C" fn(*mut c_void, usize) -> *mut u8>,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32)>,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum FileOp {
    OpenRead = 0,
    Seek = 1,
    Size = 2,
    Position = 3,
    ReleaseText = 5,
}
impl FileServices {
    fn op(&self, op: FileOp, argument: usize) -> usize {
        self.operation
            .map_or(0, |call| unsafe { call(self.ctx, op as u32, argument) })
    }
    fn log(&self, event: Diagnostic, a: u32, b: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event as u32, a, b) };
        }
    }
    fn read(&self, output: &mut [u8]) -> bool {
        self.read.is_some_and(|read| unsafe {
            read(self.ctx, output.as_mut_ptr(), output.len()) == output.len()
        })
    }
    fn seek(&self, position: u32) -> bool {
        self.op(FileOp::Seek, position as usize) != 0
    }
    fn size(&self) -> u32 {
        self.op(FileOp::Size, 0) as u32
    }
}

impl cache::Reader for FileServices {
    fn open_read(&mut self) -> bool {
        self.op(FileOp::OpenRead, 0) != 0
    }
    fn read(&mut self, output: &mut [u8]) -> bool {
        FileServices::read(self, output)
    }
    fn seek(&mut self, position: u32) -> bool {
        FileServices::seek(self, position)
    }
    fn size(&mut self) -> u32 {
        FileServices::size(self)
    }
    fn position(&mut self) -> u32 {
        self.op(FileOp::Position, 0) as u32
    }
    fn log(&mut self, event: Diagnostic, a: u32, b: u32) {
        FileServices::log(self, event, a, b)
    }
    fn text_matches(&mut self, length: u32, needle: &[u8]) -> Option<bool> {
        let data = unsafe { self.resize_text?(self.ctx, length as usize) };
        let text = if length == 0 {
            &mut []
        } else {
            if data.is_null() {
                return None;
            }
            // The native descriptor lends exclusive scratch until release or resize.
            unsafe { slice::from_raw_parts_mut(data, length as usize) }
        };
        if length != 0 && !FileServices::read(self, text) {
            return None;
        }
        Some(text == needle)
    }
    fn release_text(&mut self) {
        self.op(FileOp::ReleaseText, 0);
    }
}

/// # Safety
/// Services and callbacks remain valid for the call; output points to three writable u32s.
/// Callbacks must not retain pointers or reenter this operation.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_query(
    services: *const FileServices,
    operation: u32,
    index: u32,
    output: *mut [u32; 3],
) -> u8 {
    let (Some(services), Some(output)) = (
        services
            .as_ref()
            .copied()
            .filter(|s| s.operation.is_some() && s.read.is_some()),
        output.as_mut(),
    ) else {
        return 0;
    };
    let operation = match operation {
        1 => cache::Query::PageCount,
        3 => cache::Query::PageForParagraph(index),
        4 => cache::Query::PageForList(index),
        _ => cache::Query::Invalid,
    };
    let mut io = services;
    cache::query(&mut io, operation, output).is_some() as u8
}

/// # Safety
/// Services and needle are readable through the call. Output is one writable u32;
/// resize_text returns disjoint writable storage until ReleaseText or the next resize.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_anchor(
    services: *const FileServices,
    needle: *const u8,
    length: usize,
    output: *mut u32,
) -> u8 {
    let (Some(io), Some(output)) = (
        services
            .as_ref()
            .copied()
            .filter(|s| s.operation.is_some() && s.read.is_some()),
        output.as_mut(),
    ) else {
        return 0;
    };
    if length > isize::MAX as usize || (needle.is_null() && length != 0) {
        return 0;
    }
    let needle = if length == 0 {
        &[]
    } else {
        slice::from_raw_parts(needle, length)
    };
    let mut io = io;
    let Some(page) = cache::anchor(&mut io, needle) else {
        return 0;
    };
    *output = page;
    1
}

/// # Safety
/// Services own a borrowed read cursor; page points to a disjoint writable u32.
#[no_mangle]
pub unsafe extern "C" fn xp_book_section_visible(
    io: *const FileServices,
    offset: u32,
    first: u8,
    page: *mut u32,
) -> u8 {
    let (Some(io), Some(page)) = (io.as_ref(), page.as_mut()) else {
        return 0;
    };
    let mut output = [0; 3];
    let mut io = *io;
    let query = cache::Query::PageForVisible {
        offset,
        first: first != 0,
    };
    if cache::query(&mut io, query, &mut output).is_none() {
        return 0;
    }
    *page = output[0];
    1
}

#[repr(C)]
pub struct TextSpan {
    pub(crate) data: *const u8,
    pub(crate) length: usize,
}
