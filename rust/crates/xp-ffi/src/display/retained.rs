use super::*;
use xp_display::retained::Psram;

pub(super) struct Memory(pub *const Platform);
// init validates the retained platform table; the C adapter owns its lifetime.
unsafe impl Psram for Memory {
    fn allocate(&mut self, bytes: usize) -> *mut u8 {
        unsafe { ((*self.0).alloc.unwrap())((*self.0).ctx, bytes as u32, 2).cast() }
    }
    unsafe fn release(&mut self, pointer: *mut u8) {
        ((*self.0).free.unwrap())((*self.0).ctx, pointer.cast());
    }
}
#[repr(C)]
pub(super) struct RetainedConfig {
    pub scratch: *mut u8,
    pub scratch_size: u32,
    pub serial_log: u32,
}
impl RetainedConfig {
    pub unsafe fn get(
        config: &DisplayConfig,
        options: &InitOptions,
        chunk: usize,
    ) -> Option<*mut u8> {
        if options.platform.is_null()
            || options.controller_config_version != 1
            || config.controller_config_size as usize != size_of::<Self>()
            || config.controller_config.is_null()
            || !(config.controller_config as usize).is_multiple_of(align_of::<Self>())
        {
            return None;
        }
        let retained = &*config.controller_config.cast::<Self>();
        if retained.scratch.is_null()
            || retained.scratch_size as usize != chunk
            || retained.serial_log > 1
        {
            return None;
        }
        Some(retained.scratch)
    }
}
pub(super) unsafe fn data<'a>(
    request: &DisplayRequest,
    size: usize,
) -> Result<Option<&'a [u8]>, u32> {
    if request.data.is_null() {
        return Ok(None);
    }
    if (request.data_len as usize) < size {
        return Err(INVALID);
    }
    Ok(Some(core::slice::from_raw_parts(request.data, size)))
}
impl xp_display::papermono::Diagnostics for DisplayBus {
    fn event(&mut self, event: u32, values: [u32; 4]) {
        if let Some(callback) = self.diagnostic {
            unsafe {
                callback(
                    self.ctx,
                    event,
                    values[0],
                    values[1],
                    values[2],
                    values[3],
                    core::ptr::null(),
                    0,
                );
            }
        }
    }
}
