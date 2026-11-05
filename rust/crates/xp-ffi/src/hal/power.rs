use core::ffi::c_void;
#[repr(C)]
pub struct Power {
    normal_freq: i32,
    low_power: bool,
    locked: bool,
}
#[repr(C)]
pub struct Battery {
    battery_percent: i32,
    battery_poll_ms: u32,
}
#[repr(u32)]
pub enum Op {
    Wifi,
    Lock,
    Unlock,
    Frequency,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, Op, i32) -> i32,
    diagnostic: super::Diagnostic,
    low_frequency: i32,
}
impl Services {
    unsafe fn op(&self, op: Op, arg: i32) -> i32 {
        (self.operation)(self.ctx, op, arg)
    }
    unsafe fn diag(&self, event: u32, arg: i32) {
        if let Some(d) = self.diagnostic {
            d(self.ctx, event, arg as u32, 0, 0, 0);
        }
    }
    unsafe fn lock(&self) -> Guard<'_> {
        self.op(Op::Lock, 0);
        Guard(self)
    }
}
struct Guard<'a>(&'a Services);
impl Drop for Guard<'_> {
    fn drop(&mut self) {
        unsafe {
            self.0.op(Op::Unlock, 0);
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_power_set(
    state: *mut Power,
    services: *const Services,
    mut enabled: bool,
) {
    let Some(s) = services.as_ref() else { return };
    if state.is_null() || (*state).normal_freq <= 0 {
        return;
    }
    if s.op(Op::Wifi, 0) != 0 {
        enabled = false;
    }
    // Obtain the lock before creating a mutable reference to shared state.
    let _guard = s.lock();
    let p = &mut *state;
    let (frequency, low) = if !p.locked && enabled && !p.low_power {
        s.diag(0, 0);
        (s.low_frequency, true)
    } else if (!enabled || p.locked) && p.low_power {
        s.diag(1, 0);
        (p.normal_freq, false)
    } else {
        return;
    };
    if s.op(Op::Frequency, frequency) == 0 {
        s.diag(2, frequency);
        return;
    }
    p.low_power = low;
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_power_lock(state: *mut Power, services: *const Services) -> bool {
    let Some(s) = services.as_ref() else {
        return false;
    };
    if state.is_null() {
        return false;
    }
    let valid = {
        let _guard = s.lock();
        let p = &mut *state;
        if p.locked {
            s.diag(3, 0);
            false
        } else {
            p.locked = true;
            true
        }
    };
    if valid {
        xp_hal_power_set(state, services, false);
    }
    valid
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_power_unlock(
    state: *mut Power,
    services: *const Services,
    valid: bool,
) {
    let Some(s) = services.as_ref() else { return };
    if state.is_null() {
        return;
    }
    let _guard = s.lock();
    if valid {
        (*state).locked = false;
    }
}
#[repr(C)]
pub struct BatteryServices {
    ctx: *mut c_void,
    millis: unsafe extern "C" fn() -> u32,
    read_checked: unsafe extern "C" fn(*mut c_void, *mut u16) -> bool,
    read: unsafe extern "C" fn(*mut c_void) -> u16,
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_power_battery(
    state: *mut Battery,
    services: *const BatteryServices,
    gauge: bool,
) -> u16 {
    let (Some(p), Some(s)) = (state.as_mut(), services.as_ref()) else {
        return 0;
    };
    if gauge {
        let now = (s.millis)();
        if p.battery_poll_ms != 0 && now.wrapping_sub(p.battery_poll_ms) < 1500 {
            return p.battery_percent as u16;
        }
        p.battery_poll_ms = now;
        let mut percent = 0;
        if !(s.read_checked)(s.ctx, &mut percent) {
            return p.battery_percent as u16;
        }
        p.battery_percent = i32::from(percent);
        return percent;
    }
    let read = i32::from((s.read)(s.ctx));
    p.battery_percent = if p.battery_percent == 0 {
        10 * read
    } else {
        (p.battery_percent * 9 + read * 10) / 10
    };
    (p.battery_percent / 10) as u16
}

const _: () = assert!(core::mem::size_of::<Power>() == 8);
const _: () = assert!(core::mem::size_of::<Battery>() == 8);
