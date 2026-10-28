use super::*;
use xp_display::ssd1677::{Refresh, Ssd1677, Window, WindowAllocator};

#[repr(C)]
struct State {
    header: Header,
    platform: *const Platform,
    panel: Ssd1677,
}
const _: () = assert!(size_of::<State>() <= 24 + 2 * size_of::<*const c_void>());
const _: () = assert!(align_of::<State>() <= align_of::<*const c_void>());

struct Allocator<'a>(Option<&'a Platform>);
struct Buffer<'a> {
    data: *mut u8,
    size: usize,
    platform: Option<&'a Platform>,
}
impl AsRef<[u8]> for Buffer<'_> {
    fn as_ref(&self) -> &[u8] {
        unsafe { core::slice::from_raw_parts(self.data, self.size) }
    }
}
impl AsMut<[u8]> for Buffer<'_> {
    fn as_mut(&mut self) -> &mut [u8] {
        unsafe { core::slice::from_raw_parts_mut(self.data, self.size) }
    }
}
impl Drop for Buffer<'_> {
    fn drop(&mut self) {
        if self.size != 0 {
            if let Some(p) = self.platform {
                if let Some(free) = p.free {
                    unsafe { free(p.ctx, self.data.cast()) };
                }
            }
        }
    }
}
impl<'a> WindowAllocator for Allocator<'a> {
    type Buffer = Buffer<'a>;
    fn allocate(&self, size: usize) -> Option<Self::Buffer> {
        if size == 0 {
            return Some(Buffer {
                data: core::ptr::NonNull::dangling().as_ptr(),
                size,
                platform: self.0,
            });
        }
        let p = self.0?;
        // Preserve std::vector malloc placement, including the S3 PSRAM threshold.
        let data = unsafe { (p.alloc?)(p.ctx, size as u32, ALLOC_DEFAULT) }.cast::<u8>();
        if data.is_null() {
            return None;
        }
        unsafe {
            core::ptr::write_bytes(data, 0, size);
        }
        Some(Buffer {
            data,
            size,
            platform: self.0,
        })
    }
}
fn refresh(mode: u32) -> Option<Refresh> {
    match mode {
        0 => Some(Refresh::Full),
        1 => Some(Refresh::Half),
        2 => Some(Refresh::Fast),
        _ => None,
    }
}
unsafe fn optional<'a>(ptr: *const u8, len: u32, needed: usize) -> Result<Option<&'a [u8]>, u32> {
    if ptr.is_null() {
        return Ok(None);
    }
    if (len as usize) < needed || len as usize > isize::MAX as usize {
        return Err(INVALID);
    }
    Ok(Some(core::slice::from_raw_parts(ptr, needed)))
}
unsafe fn execute(
    panel: &mut Ssd1677,
    platform: *const Platform,
    bus: &mut &DisplayBus,
    r: &DisplayRequest,
) -> Result<u32, u32> {
    let turn_off = r.flags & FLAG_TURN_OFF != 0;
    let size = panel.frame_size();
    let mut result = RESULT_COMMITTED;
    match r.operation {
        OP_BEGIN => panel.begin(bus),
        OP_SLEEP => panel.sleep(bus),
        OP_DISPLAY | OP_DISPLAY_START => {
            let frame = optional(r.data, r.data_len, size)?.ok_or(INVALID)?;
            let previous = optional(r.previous, r.previous_len, size)?;
            let mode = refresh(r.mode).ok_or(INVALID)?;
            let deferred = r.operation == OP_DISPLAY_START;
            panel.display(bus, frame, previous, mode, turn_off, deferred);
            if deferred {
                result |= RESULT_ASYNC;
            }
        }
        OP_DISPLAY_FINISH => panel.finish(bus, optional(r.data, r.data_len, size)?),
        OP_DISPLAY_WINDOW => {
            if let Some(frame) = optional(r.data, r.data_len, size)? {
                let previous = optional(r.previous, r.previous_len, size)?;
                let allocator = Allocator(platform.as_ref());
                let rect = Window {
                    x: r.x,
                    y: r.y,
                    width: r.width,
                    height: r.height,
                };
                if let Err(failed) =
                    panel.display_window(bus, &allocator, frame, previous, rect, turn_off)
                {
                    if let Some(diagnostic) = bus.diagnostic {
                        diagnostic(
                            bus.ctx,
                            DIAG_SSD_WINDOW_ALLOCATION,
                            failed.bytes as u32,
                            0,
                            0,
                            0,
                            core::ptr::null(),
                            0,
                        );
                    }
                    return Err(INVALID);
                }
            }
        }
        OP_SEED_PREVIOUS | OP_GRAY_CLEANUP => {
            if let Some(frame) = optional(r.data, r.data_len, size)? {
                if r.operation == OP_GRAY_CLEANUP {
                    panel.cleanup_gray(bus, frame);
                } else {
                    panel.seed(bus, frame);
                }
            }
        }
        OP_GRAY_BEGIN | OP_GRAY_BASE => {
            let frame = optional(r.data, r.data_len, size)?;
            if r.operation == OP_GRAY_BEGIN {
                if r.mode > 2 {
                    return Err(INVALID);
                }
                panel.begin_gray(
                    bus,
                    if r.mode == 1 {
                        frame.unwrap_or(&[])
                    } else {
                        frame.ok_or(INVALID)?
                    },
                    r.mode,
                    refresh(r.arg).ok_or(INVALID)?,
                    turn_off,
                );
            } else {
                panel.display(
                    bus,
                    frame.ok_or(INVALID)?,
                    None,
                    refresh(r.mode).ok_or(INVALID)?,
                    turn_off,
                    false,
                );
            }
        }
        OP_GRAY_LSB | OP_GRAY_MSB => {
            if let Some(frame) = optional(r.data, r.data_len, size)? {
                panel.gray_strip(bus, r.operation == OP_GRAY_MSB, frame, 0, panel.height);
            }
        }
        OP_GRAY_STRIP => {
            if r.data.is_null() || r.height == 0 {
                return Ok(result);
            }
            if r.mode > 1 || u32::from(r.y) + u32::from(r.height) > u32::from(panel.height) {
                return Err(INVALID);
            }
            let count = usize::from(r.height) * usize::from(panel.width / 8);
            if let Some(rows) = optional(r.data, r.data_len, count)? {
                panel.gray_strip(bus, r.mode == 1, rows, r.y, r.height);
            }
        }
        OP_GRAY_DISPLAY | OP_GRAY_CALIBRATE => {
            let lut = optional(r.lut, r.lut_len, 110)?;
            let calibration = r.operation == OP_GRAY_CALIBRATE;
            panel.gray_display(
                bus,
                !calibration && turn_off,
                if calibration { None } else { lut },
                calibration || r.flags & FLAG_FACTORY != 0,
            );
        }
        OP_CUSTOM_LUT => {
            let lut = if r.arg != 0 {
                Some(optional(r.lut, r.lut_len, 110)?.ok_or(INVALID)?)
            } else {
                None
            };
            panel.set_lut(bus, lut);
        }
        OP_RESYNC => panel.resync(),
        OP_GRAY_PRECONDITION
        | OP_GRAY_PREPARE
        | OP_GRAY_REVERT
        | OP_SKIP_INITIAL_RESYNC
        | OP_BACKGROUND
        | OP_BEGIN_WORK
        | OP_ABORT_POST
        | OP_QUERY
        | OP_MAINTENANCE
        | OP_IDLE
        | OP_COMPLETE_NEXT
        | OP_FULL_COMPLETES
        | OP_ACCENT
        | OP_FAST_CUTOFF
        | OP_HOLD_FULL => {}
        _ => return Err(UNSUPPORTED),
    }
    Ok(result)
}

#[inline(always)]
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    match action {
        Action::Size => size_of::<State>() as u32,
        Action::Align => align_of::<State>() as u32,
        Action::Init {
            storage,
            config,
            options,
        } => {
            if !config.controller_config.is_null()
                || config.controller_config_size != 0
                || options.controller_config_version != 1
            {
                return INVALID;
            }
            let Some(panel) =
                Ssd1677::new(config.width, config.height, config.variant, config.flags)
            else {
                return INVALID;
            };
            storage.cast::<State>().write(State {
                header: Header::new(SSD1677),
                platform: options.platform,
                panel,
            });
            OK
        }
        Action::Drop(storage) => {
            core::ptr::drop_in_place(storage.cast::<State>());
            OK
        }
        Action::Capabilities { storage, mode } => {
            (*storage.cast::<State>()).panel.capabilities(mode)
        }
        Action::Execute {
            storage,
            bus,
            request,
            result,
        } => {
            let state = storage.cast::<State>();
            match execute(&mut (*state).panel, (*state).platform, bus, request) {
                Ok(value) => {
                    if !result.is_null() {
                        *result = value;
                    }
                    OK
                }
                Err(status) => status,
            }
        }
        Action::Extra { .. } => UNSUPPORTED,
    }
}
