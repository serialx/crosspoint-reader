use super::ultrachip::data;
use super::*;
use xp_display::ultrachip::x3::Uc8279X3;
#[repr(C)]
struct State {
    header: Header,
    panel: Uc8279X3,
}
const _: () = assert!(size_of::<State>() <= 24);
const _: () = assert!(align_of::<State>() <= align_of::<*const c_void>());
#[inline(always)]
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    match action {
        Action::Size => size_of::<State>() as u32,
        Action::Align => align_of::<State>() as u32,
        Action::Init {
            storage, config, ..
        } => {
            if config.width != 792
                || config.height != 528
                || config.flags != 0
                || !config.controller_config.is_null()
            {
                return INVALID;
            }
            storage.cast::<State>().write(State {
                header: Header::new(UC8279_X3),
                panel: Uc8279X3::new(792, 528),
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
            request: r,
            result,
        } => {
            let p = &mut (*storage.cast::<State>()).panel;
            let expected = if r.operation == OP_GRAY_STRIP {
                r.height as usize * 99
            } else {
                52272
            };
            let d = match data(r, expected) {
                Ok(d) => d,
                Err(e) => return e,
            };
            let off = r.flags & FLAG_TURN_OFF != 0;
            let factory = r.flags & FLAG_FACTORY != 0;
            let mut value = RESULT_COMMITTED;
            match r.operation {
                OP_BEGIN => p.begin(bus),
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
                    if r.mode != 2 && d.is_none() {
                        return INVALID;
                    }
                    p.gray_begin(bus, d.unwrap_or(&[]), r.mode, r.arg, off);
                }
                OP_GRAY_BASE => {
                    let Some(d) = d else {
                        return INVALID;
                    };
                    if r.mode > 2 {
                        return INVALID;
                    }
                    p.gray_base(bus, d, r.mode, off);
                }
                OP_GRAY_PRECONDITION => p.precondition(bus, r.x, r.y, r.width, r.height),
                OP_GRAY_LSB => p.lsb(bus, d),
                OP_GRAY_MSB => p.msb(bus, d),
                OP_GRAY_STRIP => {
                    if r.mode > 1 {
                        return INVALID;
                    }
                    p.strip(bus, d, r.mode, r.y, r.height);
                }
                OP_GRAY_DISPLAY => p.gray(bus, off, factory),
                OP_GRAY_CALIBRATE => p.gray(bus, false, true),
                OP_GRAY_CLEANUP => p.cleanup(bus, d),
                OP_GRAY_REVERT => p.revert(bus),
                OP_RESYNC => p.resync(),
                OP_SKIP_INITIAL_RESYNC => p.skip_initial(),
                OP_SEED_PREVIOUS | OP_GRAY_PREPARE | OP_BACKGROUND | OP_BEGIN_WORK
                | OP_ABORT_POST | OP_QUERY | OP_MAINTENANCE | OP_IDLE | OP_COMPLETE_NEXT
                | OP_FULL_COMPLETES | OP_ACCENT | OP_FAST_CUTOFF | OP_HOLD_FULL | OP_CUSTOM_LUT => {
                }
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
