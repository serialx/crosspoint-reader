use super::*;
use xp_display::ultrachip::{
    x4::{Config, Kind, X4},
    Memory,
};

pub(super) struct Allocator(pub *const Platform);
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
#[repr(C)]
struct State {
    header: Header,
    platform: *const Platform,
    panel: X4,
}
const _: () = assert!(size_of::<State>() <= STATE_BYTES as usize);
const _: () = assert!(align_of::<State>() <= STATE_ALIGN as usize);
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    dispatch_x4(action, UC8179)
}
pub(super) unsafe fn dispatch_x4(action: Action<'_>, controller: u32) -> u32 {
    match action {
        Action::Size => size_of::<State>() as u32,
        Action::Align => align_of::<State>() as u32,
        Action::Init {
            storage,
            config,
            options,
        } => {
            if config.width != 800
                || config.height != 480
                || config.flags != 0
                || config.variant > 255
                || options.controller_config_version != 1
                || config.controller_config.is_null()
                || config.controller_config_size as usize != size_of::<Config>()
                || !(config.controller_config as usize).is_multiple_of(align_of::<Config>())
            {
                return INVALID;
            }
            let cfg = &*config.controller_config.cast::<Config>();
            if cfg.controller_height < 480
                || cfg.controller_height > 600
                || cfg.gate_offset > cfg.controller_height - 480
                || cfg.psram > 1
                || cfg.classic > 1
                || cfg.row_reverse > 1
                || cfg.mirror > 1
            {
                return INVALID;
            }
            storage.cast::<State>().write(State {
                header: Header::new(controller),
                platform: options.platform,
                panel: X4::new(
                    800,
                    480,
                    if controller == UC8179 {
                        Kind::Uc8179
                    } else {
                        Kind::Uc8279
                    },
                    config.variant as u8,
                    cfg,
                ),
            });
            OK
        }
        Action::Drop(storage) => {
            let s = &mut *storage.cast::<State>();
            s.panel.release(&mut Allocator(s.platform));
            core::ptr::drop_in_place(storage.cast::<State>());
            OK
        }
        Action::Capabilities { storage, mode } => {
            (*storage.cast::<State>()).panel.capabilities(mode)
        }
        Action::Execute {
            storage,
            bus,
            request: r,
            result,
        } => {
            let s = &mut *storage.cast::<State>();
            let p = &mut s.panel;
            let mut memory = Allocator(s.platform);
            let d = match data(r, 48000) {
                Ok(d) => d,
                Err(e) => return e,
            };
            let off = r.flags & FLAG_TURN_OFF != 0;
            let factory = r.flags & FLAG_FACTORY != 0;
            let mut value = RESULT_COMMITTED;
            match r.operation {
                OP_BEGIN => p.begin(bus, &mut memory),
                OP_SLEEP => p.sleep(bus),
                OP_DISPLAY | OP_DISPLAY_WINDOW | OP_DISPLAY_START => {
                    let Some(d) = d else {
                        return INVALID;
                    };
                    if r.mode > 2 {
                        return INVALID;
                    }
                    if r.operation == OP_DISPLAY_START {
                        p.start(bus, d, r.mode, off);
                        value |= RESULT_ASYNC;
                    } else {
                        p.display(
                            bus,
                            d,
                            if r.operation == OP_DISPLAY_WINDOW {
                                2
                            } else {
                                r.mode
                            },
                            off,
                        );
                    }
                }
                OP_DISPLAY_FINISH => {
                    let Some(d) = d else {
                        return INVALID;
                    };
                    p.finish(bus, d);
                }
                OP_GRAY_BEGIN => {
                    if r.mode > 2 || r.arg > 2 {
                        return INVALID;
                    }
                    p.gray_begin(bus, d, r.mode, r.arg, off);
                }
                OP_GRAY_BASE => {
                    if r.mode > 2 {
                        return INVALID;
                    }
                    p.gray_base(bus, d, r.mode, off);
                }
                OP_GRAY_LSB => p.lsb(bus, d),
                OP_GRAY_MSB => p.msb(bus, d),
                OP_GRAY_DISPLAY => p.gray(bus, off, factory),
                OP_GRAY_CALIBRATE => p.gray(bus, false, true),
                OP_GRAY_CLEANUP => p.cleanup(bus, d),
                OP_RESYNC => p.resync(),
                OP_SKIP_INITIAL_RESYNC => p.skip_initial(),
                OP_SEED_PREVIOUS | OP_GRAY_PRECONDITION | OP_GRAY_STRIP | OP_GRAY_PREPARE
                | OP_GRAY_REVERT | OP_BACKGROUND | OP_BEGIN_WORK | OP_ABORT_POST | OP_QUERY
                | OP_MAINTENANCE | OP_IDLE | OP_COMPLETE_NEXT | OP_FULL_COMPLETES | OP_ACCENT
                | OP_FAST_CUTOFF | OP_HOLD_FULL | OP_CUSTOM_LUT => {}
                _ => return UNSUPPORTED,
            }
            if !result.is_null() {
                *result = value;
            }
            OK
        }
        Action::Extra { .. } => UNSUPPORTED,
    }
}
