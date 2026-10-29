//! Application HAL policy. SDK objects and synchronization stay with the C++ facade.
mod secret;
mod time;
use core::mem;

#[repr(C)]
pub struct Light {
    brightness: u8,
    lit: bool,
}
#[no_mangle]
pub extern "C" fn xp_hal_percent(percent: u8) -> u8 {
    percent.min(100)
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_light_brightness(state: *mut Light, percent: u8) -> bool {
    let Some(state) = state.as_mut() else {
        return false;
    };
    state.brightness = xp_hal_percent(percent);
    state.lit
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_light_on(state: *mut Light, on: bool) -> bool {
    let Some(state) = state.as_mut() else {
        return false;
    };
    mem::replace(&mut state.lit, on) != on
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_light_output(state: *const Light) -> u8 {
    let Some(state) = state.as_ref() else {
        return 0;
    };
    if state.lit {
        state.brightness
    } else {
        0
    }
}
mod clock;
mod tilt;

mod power;

mod display;
mod gpio;

mod storage;

mod system;

const _: () = assert!(core::mem::size_of::<Light>() == 2);

type Diagnostic = Option<unsafe extern "C" fn(*mut core::ffi::c_void, u32, u32, u32, u32, u32)>;
