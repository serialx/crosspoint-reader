use super::retained::{data, Memory, RetainedConfig};
use super::*;
use xp_display::ssd2677::Ssd2677;
#[repr(C)]
struct State {
    header: Header,
    panel: Ssd2677,
    platform: *const Platform,
    scratch: *mut [u8; 1024],
}
const _: () = assert!(size_of::<State>() <= 128);
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
            if config.width != 800
                || config.height != 480
                || config.variant > 1
                || config.flags != 0
            {
                return INVALID;
            }
            let Some(scratch) = RetainedConfig::get(config, options, 1024) else {
                return INVALID;
            };
            storage.cast::<State>().write(State {
                header: Header::new(SSD2677),
                panel: Ssd2677::new(config.variant == 1, 800, 480),
                platform: options.platform,
                scratch: scratch.cast(),
            });
            OK
        }
        Action::Drop(storage) => {
            let state = storage.cast::<State>();
            (*state).panel.release(&mut Memory((*state).platform));
            core::ptr::drop_in_place(state);
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
            if r.mode > 2 || r.flags & !3 != 0 {
                return INVALID;
            }
            let data = match data(r, 48000) {
                Ok(value) => value,
                Err(status) => return status,
            };
            let state = storage.cast::<State>();
            let panel = &mut (*state).panel;
            let chunk = &mut *(*state).scratch;
            let mut memory = Memory((*state).platform);
            let off = r.flags & FLAG_TURN_OFF != 0;
            match r.operation {
                OP_BEGIN => panel.begin(bus, &mut memory),
                OP_SLEEP => panel.sleep(bus),
                OP_DISPLAY | OP_DISPLAY_START | OP_GRAY_BASE => {
                    if let Some(fb) = data {
                        panel.display(bus, fb, r.mode, off, chunk);
                    }
                }
                OP_DISPLAY_WINDOW => {
                    if let Some(fb) = data {
                        panel.display(bus, fb, 2, off, chunk);
                    }
                }
                OP_GRAY_BEGIN => {
                    if let Some(fb) = data {
                        panel.gray_begin(bus, fb, r.mode, r.arg, off, chunk);
                    }
                }
                OP_GRAY_LSB => {
                    if let Some(fb) = data {
                        panel.gray_lsb(fb, &mut memory);
                    }
                }
                OP_GRAY_MSB => {
                    if let Some(fb) = data {
                        panel.gray_msb(bus, fb, chunk);
                    }
                }
                OP_GRAY_DISPLAY | OP_GRAY_CALIBRATE => {
                    if let Some(fb) = data {
                        panel.gray_display(bus, fb, r.operation != OP_GRAY_CALIBRATE && off, chunk);
                    }
                }
                OP_DISPLAY_FINISH
                | OP_SEED_PREVIOUS
                | OP_GRAY_PRECONDITION
                | OP_GRAY_STRIP
                | OP_GRAY_PREPARE
                | OP_GRAY_CLEANUP
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
