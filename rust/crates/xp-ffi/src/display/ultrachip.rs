use super::*;
#[cfg(any(
    feature = "display-uc8179",
    feature = "display-uc8279-x4",
    feature = "display-uc8279c-a4"
))]
use xp_display::ultrachip::Memory;

#[cfg(any(
    feature = "display-uc8179",
    feature = "display-uc8279-x4",
    feature = "display-uc8279c-a4"
))]
pub(super) struct Allocator(pub *const Platform);
#[cfg(any(
    feature = "display-uc8179",
    feature = "display-uc8279-x4",
    feature = "display-uc8279c-a4"
))]
unsafe impl Memory for Allocator {
    fn allocate(&mut self, bytes: usize, psram_only: bool) -> *mut u8 {
        if self.0.is_null() {
            return core::ptr::null_mut();
        }
        unsafe {
            let platform = &*self.0;
            let Some(allocate) = platform.alloc else {
                return core::ptr::null_mut();
            };
            let ptr = allocate(platform.ctx, bytes as u32, 2);
            if ptr.is_null() && !psram_only {
                allocate(platform.ctx, bytes as u32, 3).cast()
            } else {
                ptr.cast()
            }
        }
    }
    unsafe fn release(&mut self, ptr: *mut u8) {
        if !ptr.is_null() && !self.0.is_null() {
            if let Some(f) = (*self.0).free {
                f((*self.0).ctx, ptr.cast());
            }
        }
    }
}

pub(super) unsafe fn data<'a>(
    r: &DisplayRequest,
    expected: usize,
) -> Result<Option<&'a [u8]>, u32> {
    if r.data.is_null() {
        return Ok(None);
    }
    if (r.data_len as usize) < expected {
        return Err(INVALID);
    }
    Ok(Some(core::slice::from_raw_parts(r.data, expected)))
}
