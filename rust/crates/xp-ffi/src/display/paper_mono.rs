use super::retained::{data, Memory, RetainedConfig};
use super::*;
use xp_display::papermono::{PaperMono, CHUNK, SIZE};
#[repr(C)]
struct State {
    header: Header,
    panel: PaperMono,
    platform: *const Platform,
    scratch: *mut [u8; CHUNK],
}
const _: () = assert!(size_of::<State>() <= 512);
const _: () = assert!(align_of::<State>() <= STATE_ALIGN as usize);
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    match action {
        Action::Size => size_of::<State>() as u32,
        Action::Align => align_of::<State>() as u32,
        Action::Init {
            storage,
            config,
            options,
        } => initialize(storage, config, options),
        Action::Drop(storage) => {
            let state = storage.cast::<State>();
            (*state).panel.release(&mut Memory((*state).platform));
            core::ptr::drop_in_place(state);
            OK
        }
        Action::Capabilities { mode, .. } => PaperMono::capabilities(mode),
        Action::Execute {
            storage,
            bus,
            request: r,
            result,
        } => {
            if r.mode > 2 || r.flags & !3 != 0 {
                return INVALID;
            }
            let size = if r.operation == OP_GRAY_STRIP {
                usize::from(r.height) * 100
            } else {
                SIZE
            };
            let data = match data(r, size) {
                Ok(value) => value,
                Err(status) => return status,
            };
            let state = storage.cast::<State>();
            let panel = &mut (*state).panel;
            let chunk = &mut *(*state).scratch;
            let mut memory = Memory((*state).platform);
            // Only panel is exclusively borrowed; abort may access Header concurrently.
            let generation = (*state).header.generation.load(Ordering::Acquire);
            match r.operation {
                OP_BEGIN => {
                    panel.begin(bus, &mut memory);
                    (*state).header.generation.store(0, Ordering::SeqCst);
                }
                OP_SLEEP => panel.sleep(bus),
                OP_DISPLAY | OP_DISPLAY_START => {
                    if let Some(fb) = data {
                        panel.display(bus, &mut memory, fb, r.mode, chunk);
                    }
                }
                OP_DISPLAY_WINDOW => {
                    if let Some(fb) = data {
                        panel.display_window(
                            bus,
                            &mut memory,
                            fb,
                            [r.x, r.y, r.width, r.height],
                            r.flags & FLAG_TURN_OFF != 0,
                            chunk,
                        );
                    }
                }
                OP_SEED_PREVIOUS => {
                    if let Some(fb) = data {
                        panel.seed(&mut memory, fb);
                    }
                }
                OP_GRAY_BEGIN | OP_GRAY_BASE => {
                    if let Some(fb) = data {
                        panel.gray_base(
                            bus,
                            &mut memory,
                            fb,
                            if r.operation == OP_GRAY_BEGIN {
                                r.arg
                            } else {
                                r.mode
                            },
                            chunk,
                        );
                    }
                }
                OP_GRAY_LSB | OP_GRAY_MSB => {
                    if let Some(fb) = data {
                        panel.gray_copy(&mut memory, usize::from(r.operation == OP_GRAY_MSB), fb);
                    }
                }
                OP_GRAY_STRIP => {
                    if r.mode > 1 {
                        return INVALID;
                    }
                    if let Some(fb) = data {
                        panel.gray_strip(&mut memory, r.mode as usize, fb, r.y, r.height);
                    }
                }
                OP_GRAY_DISPLAY => panel.gray_display(
                    bus,
                    &mut memory,
                    || (*state).header.generation.load(Ordering::Acquire),
                    chunk,
                ),
                OP_GRAY_CALIBRATE => {
                    if let Some(fb) = data {
                        panel.calibrate(bus, &mut memory, fb, chunk);
                    }
                }
                OP_GRAY_CLEANUP => panel.cleanup(
                    bus,
                    data,
                    || (*state).header.generation.load(Ordering::Acquire),
                    chunk,
                ),
                OP_RESYNC => panel.resync(),
                OP_SKIP_INITIAL_RESYNC => panel.skip_resync(),
                OP_BACKGROUND => panel.background(r.arg != 0),
                OP_BEGIN_WORK => panel.begin_work(generation),
                OP_ABORT_POST => {
                    xp_display_abort(storage);
                }
                OP_IDLE => panel.idle(bus),
                OP_DISPLAY_FINISH | OP_GRAY_PRECONDITION | OP_GRAY_PREPARE | OP_GRAY_REVERT
                | OP_QUERY | OP_MAINTENANCE | OP_COMPLETE_NEXT | OP_FULL_COMPLETES | OP_ACCENT
                | OP_FAST_CUTOFF | OP_HOLD_FULL | OP_CUSTOM_LUT => {}
                _ => return UNSUPPORTED,
            }
            if !result.is_null() {
                *result = if panel.committed() {
                    RESULT_COMMITTED
                } else {
                    0
                } | if panel.aborted((*state).header.generation.load(Ordering::Acquire)) {
                    RESULT_ABORTED
                } else {
                    0
                };
            }
            OK
        }
        Action::Extra {
            storage,
            operation,
            payload,
            size,
            version,
        } => {
            if version != 1 {
                return INVALID;
            }
            let panel = &mut (*storage.cast::<State>()).panel;
            match operation {
                1 if size == 1 && !payload.is_null() => panel.gray_params(*payload.cast::<u8>()),
                2 if size == 0 => panel.reset_gray(),
                _ => return INVALID,
            }
            OK
        }
    }
}

// Keep initialization temporaries out of every refresh dispatch frame.
#[inline(never)]
unsafe fn initialize(storage: *mut c_void, config: &DisplayConfig, options: &InitOptions) -> u32 {
    if config.width != 800 || config.height != 480 || config.variant > 1 || config.flags != 0 {
        return INVALID;
    }
    let Some(scratch) = RetainedConfig::get(config, options, CHUNK) else {
        return INVALID;
    };
    let log = (*config.controller_config.cast::<RetainedConfig>()).serial_log != 0;
    storage.cast::<State>().write(State {
        header: Header::new(PAPER_MONO),
        panel: PaperMono::new(config.variant == 1, log),
        platform: options.platform,
        scratch: scratch.cast(),
    });
    OK
}
