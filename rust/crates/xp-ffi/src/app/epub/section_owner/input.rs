//! Main parse input owns its file and replaceable entity/parse buffer independently.
use super::super::{abi::XpEpubLeaves, book_owner::FileSlot, Text};
use crate::app::{abi, resources::Buffer};
use core::{cell::UnsafeCell, ptr::NonNull};

pub(super) struct Input {
    file: UnsafeCell<FileSlot>,
    buffer: UnsafeCell<Option<Buffer>>,
    resources: &'static abi::Resources,
}

impl Input {
    pub(super) fn new(resources: &'static abi::Resources) -> Self {
        Self {
            file: UnsafeCell::new(FileSlot::new()),
            buffer: UnsafeCell::new(None),
            resources,
        }
    }

    pub(super) fn open(&self, leaves: &'static XpEpubLeaves, path: &Text) -> bool {
        // Only this file member is borrowed; storage cannot access the parser or buffer owner.
        unsafe { (&mut *self.file.get()).open(leaves, path, false, c"EHP") }
    }

    pub(super) fn replace_buffer(&self, length: u32) -> Option<NonNull<[u8]>> {
        if length != 0 {
            // Allocate first, then publish; OOM retains the original buffer.
            let next = Buffer::allocate(self.resources, abi::MEMORY_DEFAULT, length as usize)?;
            let previous = unsafe { self.buffer.get().replace(Some(next)) };
            drop(previous);
        }
        unsafe { &*self.buffer.get() }.as_ref().map(|buffer| {
            NonNull::slice_from_raw_parts(NonNull::new(buffer.as_ptr()).unwrap(), buffer.len())
        })
    }

    pub(super) fn bytes(&self) -> *const u8 {
        unsafe { &*self.buffer.get() }
            .as_ref()
            .map_or(core::ptr::null(), |buffer| buffer.as_ptr())
    }

    pub(super) fn read(&self) -> u32 {
        // No buffer reference survives the physical read or subsequent XML dispatch.
        let data = {
            let Some(buffer) = (unsafe { &*self.buffer.get() }).as_ref() else {
                return 0;
            };
            if buffer.len() < 1024 {
                return 0;
            }
            buffer.as_ptr()
        };
        unsafe { (&mut *self.file.get()).read(data, 1024) as u32 }
    }

    pub(super) fn total(&self) -> u32 {
        unsafe { &*self.file.get() }.size()
    }
    pub(super) fn consumed(&self) -> u32 {
        unsafe { &*self.file.get() }.position()
    }
    pub(super) fn available(&self) -> u32 {
        self.total().wrapping_sub(self.consumed())
    }
    pub(super) fn release_buffer(&self) {
        let previous = unsafe { self.buffer.get().replace(None) };
        drop(previous);
    }
    pub(super) fn close(&self) {
        unsafe {
            (&mut *self.file.get()).close();
        }
    }
}

impl Drop for Input {
    fn drop(&mut self) {
        self.release_buffer();
        self.close();
    }
}

// This local reader owns only a second file. The visitor receives the fresh State
// reborrow supplied by Chapter; it never obtains the Chapter owner or main input.
pub(super) unsafe fn table(
    leaves: &'static XpEpubLeaves,
    path: &Text,
    state: *mut xp_core::chapter::State,
    visit: xp_core::chapter::table::UseReader,
) {
    use core::ffi::c_void;
    use xp_core::chapter::table::Reader;
    struct Context<'a> {
        leaves: &'static XpEpubLeaves,
        path: &'a Text,
        file: FileSlot,
    }
    unsafe extern "C" fn open(raw: *mut c_void) -> u8 {
        let context = unsafe { &mut *raw.cast::<Context<'_>>() };
        context
            .file
            .open(context.leaves, context.path, false, c"EHP")
            .into()
    }
    unsafe extern "C" fn seek(raw: *mut c_void, offset: u64) -> u8 {
        u32::try_from(offset)
            .is_ok_and(|offset| unsafe { (&mut *raw.cast::<Context<'_>>()).file.seek(offset) })
            .into()
    }
    unsafe extern "C" fn read(raw: *mut c_void, bytes: *mut u8, length: u32) -> u32 {
        unsafe {
            (&mut *raw.cast::<Context<'_>>())
                .file
                .read(bytes, length as usize) as u32
        }
    }
    let mut context = Context {
        leaves,
        path,
        file: FileSlot::new(),
    };
    let reader = Reader {
        ctx: (&mut context as *mut Context<'_>).cast(),
        open,
        seek,
        read,
    };
    unsafe { visit(state, &reader) };
}
