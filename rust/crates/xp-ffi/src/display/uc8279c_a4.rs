use super::uc8179::{data, Allocator};
use super::*;
use xp_display::ultrachip::a4::Uc8279cA4;
#[repr(C)]
struct State {
    header: Header,
    platform: *const Platform,
    panel: Uc8279cA4,
}
const _: () = assert!(size_of::<State>() <= STATE_BYTES as usize);
const _: () = assert!(align_of::<State>() <= STATE_ALIGN as usize);
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    match action {
        Action::Size => size_of::<State>() as u32,
        Action::Align => align_of::<State>() as u32,
        Action::Init {
            storage,
            config,
            options,
        } => {
            if config.width != 768
                || config.height != 552
                || config.flags != 0
                || config.variant != 0
                || !config.controller_config.is_null()
            {
                return INVALID;
            }
            storage.cast::<State>().write(State {
                header: Header::new(UC8279C_A4),
                platform: options.platform,
                panel: Uc8279cA4::new(),
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
            let expected = if r.operation == OP_GRAY_STRIP {
                r.height as usize * 96
            } else {
                52992
            };
            let d = match data(r, expected) {
                Ok(d) => d,
                Err(e) => return e,
            };
            let off = r.flags & FLAG_TURN_OFF != 0;
            match r.operation {
                OP_BEGIN => p.begin(bus),
                OP_SLEEP => p.sleep(bus),
                OP_DISPLAY | OP_DISPLAY_WINDOW | OP_DISPLAY_START | OP_GRAY_BASE => {
                    let Some(d) = d else {
                        return INVALID;
                    };
                    if r.mode > 2 {
                        return INVALID;
                    }
                    p.display(
                        bus,
                        &mut memory,
                        d,
                        if r.operation == OP_DISPLAY_WINDOW {
                            2
                        } else {
                            r.mode
                        },
                        off,
                    );
                }
                OP_GRAY_BEGIN => {
                    if r.mode > 2 || r.arg > 2 {
                        return INVALID;
                    }
                    p.gray_begin(&mut memory, d, r.mode, r.arg);
                }
                OP_GRAY_LSB => p.copy(&mut memory, d, 0, 0, 552),
                OP_GRAY_MSB => p.copy(&mut memory, d, 1, 0, 552),
                OP_GRAY_STRIP => {
                    if r.mode > 1 {
                        return INVALID;
                    }
                    p.copy(&mut memory, d, r.mode, r.y, r.height);
                }
                OP_GRAY_DISPLAY => p.gray(bus, &mut memory, off),
                OP_GRAY_CALIBRATE => p.gray(bus, &mut memory, false),
                OP_GRAY_CLEANUP => p.cleanup(bus, &mut memory),
                OP_DISPLAY_FINISH
                | OP_SEED_PREVIOUS
                | OP_GRAY_PRECONDITION
                | OP_GRAY_PREPARE
                | OP_GRAY_REVERT
                | OP_RESYNC
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
                | OP_HOLD_FULL
                | OP_CUSTOM_LUT => {}
                _ => return UNSUPPORTED,
            }
            if !result.is_null() {
                *result = RESULT_COMMITTED;
            }
            OK
        }
        Action::Extra { .. } => UNSUPPORTED,
    }
}
