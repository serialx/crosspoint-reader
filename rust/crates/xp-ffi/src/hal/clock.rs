use core::ffi::c_void;
#[repr(C)]
pub struct Clock {
    utc: i64,
    last_poll_ms: u32,
    available: bool,
    cached: bool,
}
#[repr(C)]
pub struct Calendar {
    year: u16,
    month: u8,
    day: u8,
    hour: u8,
    minute: u8,
    second: u8,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, *mut Calendar) -> bool,
    millis: unsafe extern "C" fn() -> u32,
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_clock_epoch(date: *const Calendar) -> i64 {
    let Some(d) = date.as_ref() else { return 0 };
    let y = i32::from(d.year) - i32::from(d.month <= 2);
    let m = i32::from(d.month);
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = (y - era * 400) as u32;
    let doy = ((153 * (m + if m > 2 { -3 } else { 9 }) as u32 + 2) / 5)
        .wrapping_add(u32::from(d.day))
        .wrapping_sub(1);
    let doe = (yoe * 365 + yoe / 4 - yoe / 100).wrapping_add(doy);
    // Firmware C++ long is signed 32-bit; preserve its calendar arithmetic.
    let days = era
        .wrapping_mul(146097)
        .wrapping_add(doe as i32)
        .wrapping_sub(719468);
    i64::from(days) * 86400
        + i64::from(d.hour) * 3600
        + i64::from(d.minute) * 60
        + i64::from(d.second)
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_clock_read(
    state: *mut Clock,
    services: *const Services,
    utc: *mut i64,
) -> bool {
    let (Some(c), Some(s), Some(out)) = (state.as_mut(), services.as_ref(), utc.as_mut()) else {
        return false;
    };
    if !c.available {
        return false;
    }
    let now = (s.millis)();
    if c.last_poll_ms == 0 || now.wrapping_sub(c.last_poll_ms) >= 10000 {
        let mut date = Calendar {
            year: 0,
            month: 0,
            day: 0,
            hour: 0,
            minute: 0,
            second: 0,
        };
        if (s.read)(s.ctx, &mut date) {
            c.utc = xp_hal_clock_epoch(&date);
            c.cached = true;
        } else if !c.cached {
            return false;
        }
        c.last_poll_ms = now.max(1);
    }
    *out = c.utc;
    true
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_clock_format(
    out: *mut u8,
    size: usize,
    twelve: bool,
    hour: u8,
    minute: u8,
) -> bool {
    if out.is_null() || size < if twelve { 9 } else { 6 } {
        return false;
    }
    let h = if twelve {
        let h = hour % 12;
        if h == 0 {
            12
        } else {
            h
        }
    } else {
        hour
    };
    let mut p = out;
    if !twelve || h >= 10 {
        p.write(b'0' + h / 10);
        p = p.add(1);
    }
    p.write(b'0' + h % 10);
    p = p.add(1);
    p.write(b':');
    p = p.add(1);
    p.write(b'0' + minute / 10);
    p = p.add(1);
    p.write(b'0' + minute % 10);
    p = p.add(1);
    if twelve {
        p.write(b' ');
        p = p.add(1);
        p.write(if hour >= 12 { b'P' } else { b'A' });
        p = p.add(1);
        p.write(b'M');
        p = p.add(1);
    }
    p.write(0);
    true
}
#[repr(C)]
pub struct SyncServices {
    ctx: *mut c_void,
    wifi: unsafe extern "C" fn(*mut c_void) -> bool,
    configure: unsafe extern "C" fn(*mut c_void),
    completed: unsafe extern "C" fn() -> bool,
    delay: unsafe extern "C" fn(u32),
    write_rtc: unsafe extern "C" fn(*mut c_void, *mut Calendar) -> bool,
    restore_timezone: unsafe extern "C" fn(*mut c_void),
    diagnostic: super::Diagnostic,
}
impl SyncServices {
    unsafe fn diag(&self, event: u32) {
        if let Some(d) = self.diagnostic {
            d(self.ctx, event, 0, 0, 0, 0);
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_clock_sync(
    state: *mut Clock,
    services: *const SyncServices,
) -> bool {
    let Some(s) = services.as_ref() else {
        return false;
    };
    if state.is_null() || !(*state).available {
        return false;
    }
    if !(s.wifi)(s.ctx) {
        s.diag(0);
        return false;
    }
    s.diag(1);
    (s.configure)(s.ctx);
    for _ in 0..50 {
        if (s.completed)() {
            let mut date = Calendar {
                year: 0,
                month: 0,
                day: 0,
                hour: 0,
                minute: 0,
                second: 0,
            };
            let ok = (s.write_rtc)(s.ctx, &mut date);
            if ok {
                (*state).utc = xp_hal_clock_epoch(&date);
                (*state).cached = true;
                (*state).last_poll_ms = 0;
                s.diag(2);
            }
            // The facade invalidates the cache while restoring libc's timezone.
            // Do not hold a Rust reference to state across this callback.
            (s.restore_timezone)(s.ctx);
            return ok;
        }
        (s.delay)(100);
    }
    s.diag(3);
    (s.restore_timezone)(s.ctx);
    false
}

const _: () = assert!(core::mem::size_of::<Clock>() == 16);
const _: () = assert!(core::mem::size_of::<Calendar>() == 8);
