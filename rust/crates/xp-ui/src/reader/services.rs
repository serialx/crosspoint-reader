//! Shared synchronous dispatch and scope cleanup for reader policy families.
use core::ffi::c_void;

pub(super) struct Core {
    pub context: *mut c_void,
    pub read: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    pub write: unsafe extern "C" fn(*mut c_void, u8, u32),
    pub command: unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32,
}
impl Core {
    #[inline]
    pub fn read(&self, field: u8) -> u32 {
        unsafe { (self.read)(self.context, field) }
    }
    #[inline]
    pub fn write(&self, field: u8, value: u32) {
        unsafe {
            (self.write)(self.context, field, value);
        }
    }
    #[inline]
    pub fn command(&self, command: u8, a: i32, b: i32) -> u32 {
        unsafe { (self.command)(self.context, command, a, b) }
    }
    #[inline]
    pub fn op(&self, command: u8) -> u32 {
        unsafe { (self.command)(self.context, command, 0, 0) }
    }
    pub fn guard(&self, release: u8) -> Guard<'_> {
        Guard {
            core: self,
            release,
        }
    }
}
pub(super) struct Guard<'a> {
    core: &'a Core,
    release: u8,
}
impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.core.op(self.release);
    }
}
