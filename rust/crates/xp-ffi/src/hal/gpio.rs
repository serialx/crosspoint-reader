use core::ffi::c_void;
#[repr(C)]
pub struct DetectServices {
    ctx: *mut c_void,
    read: unsafe extern "C" fn(bool) -> u8,
    write: unsafe extern "C" fn(u8),
    probe: unsafe extern "C" fn(*mut u8, *mut u8) -> u8,
    diagnostic: super::Diagnostic,
}
impl DetectServices {
    unsafe fn diag(&self, e: u32, a: u32, b: u32, c: u32) {
        if let Some(d) = self.diagnostic {
            d(self.ctx, e, a, b, c, 0);
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_detect_x3(services: *const DetectServices) -> bool {
    let Some(s) = services.as_ref() else {
        return false;
    };
    for cached in [false, true] {
        let value = (s.read)(cached);
        if value == 1 || value == 2 {
            s.diag(u32::from(cached), value.into(), 0, 0);
            return value == 2;
        }
    }
    let (mut first, mut second) = (0, 0);
    let verdict = (s.probe)(&mut first, &mut second);
    s.diag(2, first.into(), second.into(), verdict.into());
    match verdict {
        1 => {
            (s.write)(2);
            true
        }
        0 => {
            (s.write)(1);
            false
        }
        _ => false,
    }
}
#[repr(C)]
pub struct Input {
    usb_connected: bool,
    usb_changed: bool,
    page_buttons: u8,
    page_pressed: bool,
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_input_update(state: *mut Input, buttons: u8, usb: bool) {
    let Some(s) = state.as_mut() else { return };
    s.page_pressed = buttons & !s.page_buttons != 0;
    s.page_buttons = buttons;
    s.usb_changed = usb != s.usb_connected;
    s.usb_connected = usb;
}
#[no_mangle]
pub extern "C" fn xp_hal_input_adc(first: i32, second: i32) -> bool {
    (0..4000).contains(&first) || (0..4000).contains(&second)
}
#[no_mangle]
pub extern "C" fn xp_hal_wake_reason(
    deep: bool,
    button: bool,
    undefined: bool,
    power: bool,
    unknown: bool,
    usb: bool,
    cold_button: bool,
) -> u32 {
    if (deep && button) || (undefined && power && !usb && cold_button) {
        0
    } else if undefined && unknown && usb {
        1
    } else if undefined && power && usb {
        2
    } else {
        3
    }
}
#[repr(C)]
pub struct WakeServices {
    ctx: *mut c_void,
    pressed: unsafe extern "C" fn(*mut c_void) -> bool,
    debounce: unsafe extern "C" fn(*mut c_void) -> bool,
    update: unsafe extern "C" fn(*mut c_void),
    millis: unsafe extern "C" fn() -> u32,
    delay: unsafe extern "C" fn(u32),
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_verify_wake(services: *const WakeServices) -> bool {
    let Some(s) = services.as_ref() else {
        return false;
    };
    let held = (s.pressed)(s.ctx);
    let start = (s.millis)();
    (s.update)(s.ctx);
    while (s.millis)().wrapping_sub(start) < 10 || (s.debounce)(s.ctx) {
        (s.delay)(1);
        (s.update)(s.ctx);
    }
    held && (s.pressed)(s.ctx)
}

const _: () = assert!(core::mem::size_of::<Input>() == 4);
