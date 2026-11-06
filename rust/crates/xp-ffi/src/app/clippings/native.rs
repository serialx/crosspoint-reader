use super::{
    abi,
    clippings::io::{File, Io, Mode},
    epub::abi::XpEpubLeaves,
    resources::Text,
};
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct Physical {
    pub open: unsafe extern "C" fn(abi::Text, u32) -> *mut c_void,
    pub seek_relative: unsafe extern "C" fn(*mut c_void, u32) -> u8,
    pub available: unsafe extern "C" fn(*mut c_void) -> i32,
    pub truncate: unsafe extern "C" fn(*mut c_void, u32) -> u8,
    pub is_directory: unsafe extern "C" fn(*mut c_void) -> u8,
    pub next: unsafe extern "C" fn(*mut c_void, *mut u8) -> *mut c_void,
    pub name: unsafe extern "C" fn(*mut c_void, *mut u8, u32) -> u32,
    pub epoch_seconds: unsafe extern "C" fn() -> i64,
    pub random: unsafe extern "C" fn(*mut u8, u32),
}
const _: () = assert!(core::mem::size_of::<Physical>() == 9 * core::mem::size_of::<usize>());

#[derive(Clone, Copy)]
pub struct Native {
    pub leaves: &'static XpEpubLeaves,
    pub resources: &'static abi::Resources,
    pub physical: &'static Physical,
}

pub struct Handle {
    raw: Option<NonNull<c_void>>,
    io: Native,
}
impl Handle {
    fn pointer(&self) -> *mut c_void {
        self.raw.expect("open clipping file").as_ptr()
    }
}
impl Drop for Handle {
    fn drop(&mut self) {
        self.close();
    }
}
impl File for Handle {
    fn read(&mut self, bytes: &mut [u8]) -> i32 {
        unsafe {
            (self.io.leaves.file_read)(self.pointer(), bytes.as_mut_ptr(), bytes.len() as u32)
        }
    }
    fn write(&mut self, bytes: &[u8]) -> usize {
        unsafe {
            (self.io.leaves.file_write)(self.pointer(), bytes.as_ptr(), bytes.len() as u32) as usize
        }
    }
    fn seek(&mut self, offset: u32) -> bool {
        unsafe { (self.io.leaves.file_seek)(self.pointer(), offset) != 0 }
    }
    fn seek_relative(&mut self, offset: u32) -> bool {
        unsafe { (self.io.physical.seek_relative)(self.pointer(), offset) != 0 }
    }
    fn position(&self) -> u32 {
        unsafe { (self.io.leaves.file_position)(self.pointer()) }
    }
    fn available(&self) -> i32 {
        unsafe { (self.io.physical.available)(self.pointer()) }
    }
    fn size(&self) -> u32 {
        unsafe { (self.io.leaves.file_size)(self.pointer()) }
    }
    fn truncate(&mut self, size: u32) -> bool {
        unsafe { (self.io.physical.truncate)(self.pointer(), size) != 0 }
    }
    fn flush(&mut self) {
        unsafe { (self.io.leaves.file_flush)(self.pointer()) };
    }
    fn close(&mut self) {
        if let Some(raw) = self.raw.take() {
            unsafe { (self.io.leaves.file_close)(raw.as_ptr()) };
        }
    }
    fn is_directory(&self) -> bool {
        unsafe { (self.io.physical.is_directory)(self.pointer()) != 0 }
    }
    fn next(&mut self) -> Result<Option<Self>, super::clippings::io::DirectoryError> {
        let mut failed = 0;
        let raw = unsafe { (self.io.physical.next)(self.pointer(), &mut failed) };
        if failed != 0 {
            return Err(super::clippings::io::DirectoryError);
        }
        Ok(NonNull::new(raw).map(|raw| Self {
            raw: Some(raw),
            io: self.io,
        }))
    }
    fn name(&self, out: &mut [u8]) -> usize {
        unsafe {
            (self.io.physical.name)(self.pointer(), out.as_mut_ptr(), out.len() as u32) as usize
        }
    }
}
impl Io for Native {
    type File = Handle;
    fn open(&self, path: &[u8], mode: Mode) -> Option<Handle> {
        let path = Text::copy(path).ok()?;
        let mode = match mode {
            Mode::Read => 0,
            Mode::Rewrite => 1,
            Mode::Append => 2,
            Mode::ReadWrite => 3,
        };
        let raw = NonNull::new(unsafe { (self.physical.open)(path.wire(), mode) })?;
        Some(Handle {
            raw: Some(raw),
            io: *self,
        })
    }
    fn open_read(&self, path: &[u8]) -> Option<Handle> {
        let path = Text::copy(path).ok()?;
        let raw = NonNull::new(unsafe {
            (self.leaves.file_open_read)(path.wire(), abi::Text::from_terminated(b"CLIP\0"))
        })?;
        Some(Handle {
            raw: Some(raw),
            io: *self,
        })
    }
    fn exists(&self, path: &[u8]) -> bool {
        // Allocation failure must never make an occupied destination look absent.
        Text::copy(path).map_or(true, |path| unsafe {
            (self.resources.path_exists)(path.wire()) != 0
        })
    }
    fn mkdir(&self, path: &[u8]) {
        if let Ok(path) = Text::copy(path) {
            unsafe { (self.leaves.path_mkdir)(path.wire()) };
        }
    }
    fn remove(&self, path: &[u8]) -> bool {
        Text::copy(path).is_ok_and(|path| unsafe { (self.leaves.path_remove)(path.wire()) != 0 })
    }
    fn rename(&self, from: &[u8], to: &[u8]) -> bool {
        let (Ok(from), Ok(to)) = (Text::copy(from), Text::copy(to)) else {
            return false;
        };
        unsafe { (self.leaves.path_rename)(from.wire(), to.wire()) != 0 }
    }
    fn path_hash(&self, path: &[u8]) -> u64 {
        unsafe { (self.leaves.native_path_hash)(abi::Text::from_bytes(path)) }
    }
    fn epoch_seconds(&self) -> i64 {
        unsafe { (self.physical.epoch_seconds)() }
    }
    fn random(&self, out: &mut [u8]) {
        unsafe { (self.physical.random)(out.as_mut_ptr(), out.len() as u32) };
    }
    fn log(&self, severity: u8, arguments: core::fmt::Arguments<'_>, tail: &[&[u8]]) {
        super::epub::book_owner::log::with_tail(self.leaves, severity, b"CLIP", arguments, tail);
    }
}
