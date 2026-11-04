use super::super::{File, Text, XpEpubLeaves};
use crate::{
    app::{abi, resources::Buffer},
    settings,
};
use core::{ffi::c_void, ptr, ptr::NonNull};

pub(in crate::app::epub) struct FileSlot {
    file: Option<File>,
    position: u32,
}

impl FileSlot {
    pub(in crate::app::epub) fn new() -> Self {
        Self {
            file: None,
            position: 0,
        }
    }

    pub(in crate::app::epub) fn open(
        &mut self,
        leaves: &'static XpEpubLeaves,
        path: &Text,
        write: bool,
        origin: &'static core::ffi::CStr,
    ) -> bool {
        self.close();
        let open = if write {
            leaves.file_open_write
        } else {
            leaves.file_open_read
        };
        self.file = NonNull::new(unsafe {
            open(
                path.wire(),
                abi::Text::from_terminated(origin.to_bytes_with_nul()),
            )
        })
        .map(|handle| File {
            handle: Some(handle),
            leaves,
        });
        self.position = 0;
        self.file.is_some()
    }

    pub(in crate::app::epub) fn close(&mut self) -> bool {
        self.file.take().is_some_and(|mut file| file.close())
    }
    pub(in crate::app::epub) fn is_open(&self) -> bool {
        self.file.is_some()
    }
    pub(in crate::app::epub) fn position(&self) -> u32 {
        self.position
    }
    pub(in crate::app::epub) fn size(&self) -> u32 {
        self.file.as_ref().map_or(0, File::size)
    }

    pub(in crate::app::epub) unsafe fn read(&mut self, bytes: *mut u8, length: usize) -> i32 {
        let Some(file) = &self.file else { return -1 };
        let Some(handle) = file.handle else { return -1 };
        let Ok(length) = u32::try_from(length) else {
            return -1;
        };
        let count = unsafe { (file.leaves.file_read)(handle.as_ptr(), bytes, length) };
        self.position = self.position.wrapping_add(count.max(0) as u32);
        count
    }

    pub(in crate::app::epub) unsafe fn write(&mut self, bytes: *const u8, length: usize) -> usize {
        let Some(file) = &self.file else { return 0 };
        let Some(handle) = file.handle else { return 0 };
        let Ok(length) = u32::try_from(length) else {
            return 0;
        };
        let count = unsafe { (file.leaves.file_write)(handle.as_ptr(), bytes, length) };
        self.position = self.position.wrapping_add(count);
        count as usize
    }

    pub(in crate::app::epub) fn flush(&self, flush: unsafe extern "C" fn(*mut c_void)) {
        if let Some(handle) = self.file.as_ref().and_then(|file| file.handle) {
            unsafe { flush(handle.as_ptr()) };
        }
    }

    pub(in crate::app::epub) fn physical_position(&mut self) -> u32 {
        if let Some(file) = &self.file {
            if let Some(handle) = file.handle {
                self.position = unsafe { (file.leaves.file_position)(handle.as_ptr()) };
            }
        }
        self.position
    }

    // Page::load borrows the HAL file and advances its physical cursor directly.
    pub(in crate::app::epub) fn load_page(&mut self, bytes: u32, visible: u32) -> *mut c_void {
        let Some(file) = &self.file else {
            return ptr::null_mut();
        };
        let Some(handle) = file.handle else {
            return ptr::null_mut();
        };
        let page = unsafe { (file.leaves.page_load)(handle.as_ptr(), bytes, visible) };
        self.physical_position();
        page
    }

    pub(in crate::app::epub) fn seek(&mut self, position: u32) -> bool {
        if !self.file.as_ref().is_some_and(|file| file.seek(position)) {
            return false;
        }
        self.position = position;
        true
    }

    fn ops(&mut self) -> settings::FileOps {
        unsafe extern "C" fn read(raw: *mut c_void, bytes: *mut u8, length: usize) -> i32 {
            unsafe { (&mut *raw.cast::<FileSlot>()).read(bytes, length) }
        }
        unsafe extern "C" fn write(raw: *mut c_void, bytes: *const u8, length: usize) -> usize {
            unsafe { (&mut *raw.cast::<FileSlot>()).write(bytes, length) }
        }
        unsafe extern "C" fn seek(raw: *mut c_void, position: usize) -> bool {
            u32::try_from(position)
                .is_ok_and(|position| unsafe { (&mut *raw.cast::<FileSlot>()).seek(position) })
        }
        settings::FileOps {
            ctx: (self as *mut Self).cast(),
            read,
            write,
            seek,
        }
    }
}

enum State {
    Read(settings::Reader),
    Write(settings::Writer),
}

// One existing 4 KiB build buffer. Allocation failure uses the same unbuffered path.
pub(in crate::app::epub) struct Buffered {
    backing: Option<Buffer>,
    state: State,
}

impl Buffered {
    pub(in crate::app::epub) fn new(
        resources: &'static abi::Resources,
        file: &FileSlot,
        write: bool,
    ) -> Self {
        Self::with_capacity(resources, file, write, 4096)
    }

    pub(in crate::app::epub) fn with_capacity(
        resources: &'static abi::Resources,
        file: &FileSlot,
        write: bool,
        capacity: usize,
    ) -> Self {
        Self {
            backing: Buffer::allocate(resources, abi::MEMORY_DEFAULT, capacity),
            state: if write {
                State::Write(settings::Writer::new(file.position as usize))
            } else {
                State::Read(settings::Reader::new(file.position as usize))
            },
        }
    }

    fn bytes(&self) -> (*mut u8, usize) {
        self.backing
            .as_ref()
            .map_or((ptr::null_mut(), 0), |buffer| {
                (buffer.as_ptr(), buffer.len())
            })
    }

    pub(in crate::app::epub) fn position(&self) -> u32 {
        (match &self.state {
            State::Read(state) => state.position(),
            State::Write(state) => state.position(),
        }) as u32
    }

    pub(in crate::app::epub) fn flush(&mut self, file: &mut FileSlot) -> bool {
        let (buffer, _) = self.bytes();
        match &mut self.state {
            State::Write(state) => unsafe {
                settings::xp_store_buffer_flush(state, buffer, &file.ops())
            },
            State::Read(_) => true,
        }
    }

    pub(in crate::app::epub) unsafe fn read(
        &mut self,
        file: &mut FileSlot,
        bytes: *mut u8,
        length: usize,
    ) -> usize {
        let (buffer, capacity) = self.bytes();
        match &mut self.state {
            State::Read(state) => unsafe {
                settings::xp_store_buffer_read(state, buffer, capacity, &file.ops(), bytes, length)
            },
            State::Write(_) => 0,
        }
    }

    pub(in crate::app::epub) unsafe fn write(
        &mut self,
        file: &mut FileSlot,
        bytes: *const u8,
        length: usize,
    ) -> usize {
        let (buffer, capacity) = self.bytes();
        match &mut self.state {
            State::Write(state) => {
                unsafe {
                    settings::xp_store_buffer_write(
                        state,
                        buffer,
                        capacity,
                        &file.ops(),
                        bytes,
                        length,
                    )
                };
                length
            }
            State::Read(_) => 0,
        }
    }

    pub(in crate::app::epub) fn seek(&mut self, file: &mut FileSlot, position: u32) -> bool {
        let (_, capacity) = self.bytes();
        match &mut self.state {
            State::Read(state) => unsafe {
                settings::xp_store_buffer_seek(state, capacity, &file.ops(), position as usize)
            },
            State::Write(_) => false,
        }
    }
}
