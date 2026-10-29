//! Dictionary byte formats and streaming policy. Buffers and HAL handles are borrowed.
use core::{ffi::c_void, slice};
mod abi;
mod html;
mod zip;

#[repr(C)]
pub struct File {
    context: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
    seek: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    tell: unsafe extern "C" fn(*mut c_void) -> usize,
    size: unsafe extern "C" fn(*mut c_void) -> usize,
}
impl File {
    fn read(&self, out: &mut [u8]) -> i32 {
        unsafe { (self.read)(self.context, out.as_mut_ptr(), out.len()) }
    }
    fn write(&self, bytes: &[u8]) -> bool {
        unsafe { (self.write)(self.context, bytes.as_ptr(), bytes.len()) == bytes.len() }
    }
    fn seek(&self, at: usize) -> bool {
        unsafe { (self.seek)(self.context, at) }
    }
    fn tell(&self) -> usize {
        unsafe { (self.tell)(self.context) }
    }
    fn size(&self) -> usize {
        unsafe { (self.size)(self.context) }
    }
    fn byte(&self) -> Option<u8> {
        let mut b = [0];
        (self.read(&mut b) == 1).then_some(b[0])
    }
    fn le16(&self) -> Option<u16> {
        let mut b = [0; 2];
        (self.read(&mut b) == 2).then_some(u16::from_le_bytes(b))
    }
}
unsafe fn bytes<'a>(p: *const u8, n: usize) -> &'a [u8] {
    if n == 0 {
        &[]
    } else {
        slice::from_raw_parts(p, n)
    }
}

mod definition;
mod engine;
mod lifecycle;
mod lookup;
mod registry;
