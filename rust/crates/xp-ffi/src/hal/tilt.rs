use core::{ffi::c_void, mem};
#[repr(C)]
pub struct Tilt {
    init_ms: u32,
    last_poll_ms: u32,
    last_tilt_ms: u32,
    wake_ms: u32,
    available: bool,
    forward: bool,
    back: bool,
    activity: bool,
    in_tilt: bool,
    awake: bool,
}
#[repr(u32)]
pub enum Op {
    Begin,
    Sleep,
    Wake,
    HasGyro,
}
#[repr(C)]
pub struct Services {
    ctx: *mut c_void,
    operation: unsafe extern "C" fn(*mut c_void, Op) -> bool,
    read: unsafe extern "C" fn(*mut c_void, *mut f32, *mut f32) -> bool,
    millis: unsafe extern "C" fn() -> u32,
    diagnostic: super::Diagnostic,
}
impl Services {
    unsafe fn op(&self, op: Op) -> bool {
        (self.operation)(self.ctx, op)
    }
    unsafe fn diag(&self, event: u32, axis: f32) {
        if let Some(d) = self.diagnostic {
            d(self.ctx, event, axis.to_bits(), 0, 0, 0);
        }
    }
}
impl Tilt {
    fn clear(&mut self) {
        self.forward = false;
        self.back = false;
        self.activity = false;
    }
    unsafe fn wake(&mut self, s: &Services) -> bool {
        if !self.available {
            return false;
        }
        if !s.op(Op::Wake) {
            s.diag(4, 0.0);
            return false;
        }
        self.last_poll_ms = (s.millis)();
        self.last_tilt_ms = (s.millis)();
        self.wake_ms = (s.millis)();
        self.awake = true;
        true
    }
    unsafe fn sleep(&mut self, s: &Services) -> bool {
        if !self.available {
            return false;
        }
        if !s.op(Op::Sleep) {
            s.diag(5, 0.0);
            return false;
        }
        self.clear();
        self.in_tilt = false;
        self.awake = false;
        true
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_tilt_begin(state: *mut Tilt, services: *const Services) {
    let (Some(t), Some(s)) = (state.as_mut(), services.as_ref()) else {
        return;
    };
    if !s.op(Op::Begin) {
        s.diag(0, 0.0);
        return;
    }
    if !s.op(Op::Sleep) {
        s.diag(1, 0.0);
    }
    t.available = s.op(Op::HasGyro);
    if !t.available {
        s.diag(2, 0.0);
        return;
    }
    t.init_ms = (s.millis)();
    t.last_poll_ms = (s.millis)();
    s.diag(3, 0.0);
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_tilt_wake(state: *mut Tilt, services: *const Services) -> bool {
    let (Some(t), Some(s)) = (state.as_mut(), services.as_ref()) else {
        return false;
    };
    t.wake(s)
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_tilt_sleep(state: *mut Tilt, services: *const Services) -> bool {
    let (Some(t), Some(s)) = (state.as_mut(), services.as_ref()) else {
        return false;
    };
    t.sleep(s)
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_tilt_update(
    state: *mut Tilt,
    services: *const Services,
    mode: u8,
    orientation: u8,
    in_reader: bool,
) {
    let (Some(t), Some(s)) = (state.as_mut(), services.as_ref()) else {
        return;
    };
    if !t.available {
        return;
    }
    if mode != 0 && !t.awake {
        t.awake = t.wake(s);
        return;
    }
    if mode == 0 && t.awake {
        t.awake = !t.sleep(s);
        return;
    }
    if mode == 0 || !in_reader {
        return;
    }
    let now = (s.millis)();
    if now.wrapping_sub(t.wake_ms) < 300 || now.wrapping_sub(t.last_poll_ms) < 50 {
        return;
    }
    t.last_poll_ms = now;
    let (mut gx, mut gy) = (0.0, 0.0);
    if !(s.read)(s.ctx, &mut gx, &mut gy) {
        return;
    }
    let inverted = mode == 2;
    let axis = match orientation {
        0 => {
            if inverted {
                -gx
            } else {
                gx
            }
        }
        2 => {
            if inverted {
                gx
            } else {
                -gx
            }
        }
        1 => {
            if inverted {
                gy
            } else {
                -gy
            }
        }
        3 => {
            if inverted {
                -gy
            } else {
                gy
            }
        }
        _ => gx,
    };
    if t.in_tilt {
        if axis.abs() < 50.0 {
            t.in_tilt = false;
        }
    } else if now.wrapping_sub(t.last_tilt_ms) >= 600 {
        let event = if axis > 270.0 {
            t.forward = true;
            6
        } else if axis < -270.0 {
            t.back = true;
            7
        } else {
            return;
        };
        t.activity = true;
        t.in_tilt = true;
        t.last_tilt_ms = now;
        s.diag(event, axis);
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_tilt_clear(state: *mut Tilt) {
    if let Some(t) = state.as_mut() {
        t.clear();
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_tilt_take(state: *mut Tilt, event: u8) -> bool {
    let Some(t) = state.as_mut() else {
        return false;
    };
    match event {
        0 => mem::take(&mut t.forward),
        1 => mem::take(&mut t.back),
        2 => mem::take(&mut t.activity),
        _ => false,
    }
}

const _: () = assert!(core::mem::size_of::<Tilt>() == 24);
