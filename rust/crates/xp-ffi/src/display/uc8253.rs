use super::*;
use xp_display::{Panel, Request, Uc8253};
#[repr(C)]
struct State {
    header: Header,
    panel: Uc8253,
}
const _: () = assert!(size_of::<State>() <= 24 + 2 * size_of::<*const c_void>());
const _: () = assert!(align_of::<State>() <= align_of::<*const c_void>());
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
            if config.variant != 0 || config.flags != 0 {
                return INVALID;
            }
            if !config.controller_config.is_null()
                && (options.controller_config_version != 1
                    || config.controller_config_size as usize != size_of::<X3Config>()
                    || !(config.controller_config as usize).is_multiple_of(align_of::<X3Config>()))
            {
                return INVALID;
            }
            match Uc8253::new(config.width, config.height, config.controller_config.cast()) {
                Ok(panel) => {
                    storage.cast::<State>().write(State {
                        header: Header::new(UC8253_X3),
                        panel,
                    });
                    OK
                }
                Err(status) => status,
            }
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
            if request.data_len as usize > isize::MAX as usize {
                return INVALID;
            }
            let data = if request.data.is_null() {
                None
            } else {
                Some(core::slice::from_raw_parts(
                    request.data,
                    request.data_len as usize,
                ))
            };
            let r = Request {
                operation: request.operation,
                mode: request.mode,
                flags: request.flags,
                arg: request.arg,
                x: request.x,
                y: request.y,
                width: request.width,
                height: request.height,
                data,
            };
            let panel = &mut (*storage.cast::<State>()).panel;
            match panel.execute(bus, r) {
                Ok(value) => {
                    if !result.is_null() {
                        *result = value;
                    }
                    OK
                }
                Err(status) => status,
            }
        }
        Action::Extra {
            storage,
            operation,
            payload,
            size,
            version,
        } => {
            let _ = (storage, operation, payload, size, version);
            UNSUPPORTED
        }
    }
}
