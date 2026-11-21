#![allow(dead_code)]
#[path = "../../rust/crates/xp-ffi/src/hal/mod.rs"]
mod hal;
mod abi {
    #[repr(C)]
    pub struct RawInput {
        pub power: u8,
        pub auxiliary: u8,
        pub first: i32,
        pub second: i32,
    }
    pub struct Runtime {
        pub now_ms: unsafe extern "C" fn() -> u32,
        pub delay_ms: unsafe extern "C" fn(u32),
        pub raw_input_sample: unsafe extern "C" fn(*mut RawInput),
        pub power_context: unsafe extern "C" fn(*mut crate::hal::power::Context),
        pub tilt_context: unsafe extern "C" fn(*mut crate::hal::tilt::Context),
        pub light_state: unsafe extern "C" fn() -> *mut crate::hal::Light,
        pub light_output: unsafe extern "C" fn(u8),
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/runtime_ports.rs"]
mod runtime_ports;

#[cfg(test)]
mod tests {
    use super::{abi::*, hal};
    use std::{cell::RefCell, ffi::c_void};
    #[repr(C)]
    #[derive(Default)]
    struct Power {
        normal: i32,
        low: bool,
        locked: bool,
    }
    #[repr(C)]
    #[derive(Default)]
    struct Tilt {
        init: u32,
        poll: u32,
        last: u32,
        wake: u32,
        available: bool,
        forward: bool,
        back: bool,
        activity: bool,
        in_tilt: bool,
        awake: bool,
    }
    #[repr(C)]
    struct Light {
        brightness: u8,
        lit: bool,
    }
    #[repr(C)]
    struct PowerServices {
        ctx: *mut c_void,
        operation: unsafe extern "C" fn(*mut c_void, hal::power::Op, i32) -> i32,
        diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32)>,
        low_frequency: i32,
    }
    #[repr(C)]
    struct TiltServices {
        ctx: *mut c_void,
        operation: unsafe extern "C" fn(*mut c_void, hal::tilt::Op) -> bool,
        read: unsafe extern "C" fn(*mut c_void, *mut f32, *mut f32) -> bool,
        millis: unsafe extern "C" fn() -> u32,
        diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32)>,
    }
    struct State {
        power: Box<Power>,
        tilt: Box<Tilt>,
        light: Box<Light>,
        wifi: bool,
        frequency_ok: bool,
        now: u32,
        samples: Vec<(u8, u8, i32, i32)>,
        trace: Vec<String>,
    }
    impl Default for State {
        fn default() -> Self {
            Self {
                power: Box::new(Power {
                    normal: 160,
                    ..Power::default()
                }),
                tilt: Box::default(),
                light: Box::new(Light {
                    brightness: 60,
                    lit: false,
                }),
                wifi: false,
                frequency_ok: true,
                now: 0,
                samples: vec![],
                trace: vec![],
            }
        }
    }
    thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
    fn with<T>(f: impl FnOnce(&mut State) -> T) -> T {
        STATE.with(|s| f(&mut s.borrow_mut()))
    }
    unsafe extern "C" fn now() -> u32 {
        with(|s| s.now)
    }
    unsafe extern "C" fn delay(ms: u32) {
        with(|s| {
            s.trace.push(format!("delay:{ms}"));
            s.now = s.now.wrapping_add(ms);
        });
    }
    unsafe extern "C" fn sample(out: *mut RawInput) {
        with(|s| {
            s.trace.push("sample".into());
            let (power, auxiliary, first, second) = if s.samples.is_empty() {
                (0, 0, 4095, 4095)
            } else {
                s.samples.remove(0)
            };
            out.write(RawInput {
                power,
                auxiliary,
                first,
                second,
            });
        });
    }
    unsafe extern "C" fn power_op(_: *mut c_void, op: hal::power::Op, arg: i32) -> i32 {
        with(|s| match op {
            hal::power::Op::Wifi => {
                s.trace.push("wifi".into());
                s.wifi.into()
            }
            hal::power::Op::Lock => {
                s.trace.push("lock".into());
                1
            }
            hal::power::Op::Unlock => {
                s.trace.push("unlock".into());
                1
            }
            hal::power::Op::Frequency => {
                s.trace.push(format!("frequency:{arg}"));
                s.frequency_ok.into()
            }
        })
    }
    unsafe extern "C" fn diag(_: *mut c_void, event: u32, _: u32, _: u32, _: u32, _: u32) {
        with(|s| s.trace.push(format!("diag:{event}")));
    }
    unsafe extern "C" fn power(out: *mut hal::power::Context) {
        let services = PowerServices {
            ctx: core::ptr::null_mut(),
            operation: power_op,
            diagnostic: Some(diag),
            low_frequency: 10,
        };
        assert_eq!(
            core::mem::size_of::<PowerServices>(),
            core::mem::size_of::<hal::power::Services>()
        );
        assert_eq!(
            core::mem::size_of::<Power>(),
            core::mem::size_of::<hal::power::Power>()
        );
        let state = with(|s| &mut *s.power as *mut Power);
        out.write(hal::power::Context {
            state: state.cast(),
            services: core::ptr::read((&services as *const PowerServices).cast()),
        });
    }
    unsafe extern "C" fn tilt_op(_: *mut c_void, op: hal::tilt::Op) -> bool {
        with(|s| s.trace.push(format!("tilt-op:{}", op as u32)));
        true
    }
    unsafe extern "C" fn tilt_read(_: *mut c_void, x: *mut f32, y: *mut f32) -> bool {
        with(|s| s.trace.push("imu-read".into()));
        x.write(300.0);
        y.write(0.0);
        true
    }
    unsafe extern "C" fn tilt(out: *mut hal::tilt::Context) {
        let services = TiltServices {
            ctx: core::ptr::null_mut(),
            operation: tilt_op,
            read: tilt_read,
            millis: now,
            diagnostic: Some(diag),
        };
        assert_eq!(
            core::mem::size_of::<TiltServices>(),
            core::mem::size_of::<hal::tilt::Services>()
        );
        assert_eq!(
            core::mem::size_of::<Tilt>(),
            core::mem::size_of::<hal::tilt::Tilt>()
        );
        let state = with(|s| &mut *s.tilt as *mut Tilt);
        out.write(hal::tilt::Context {
            state: state.cast(),
            services: core::ptr::read((&services as *const TiltServices).cast()),
        });
    }
    unsafe extern "C" fn light() -> *mut hal::Light {
        with(|s| &mut *s.light as *mut Light).cast()
    }
    unsafe extern "C" fn output(value: u8) {
        with(|s| s.trace.push(format!("light:{value}")));
    }
    fn runtime() -> Runtime {
        Runtime {
            now_ms: now,
            delay_ms: delay,
            raw_input_sample: sample,
            power_context: power,
            tilt_context: tilt,
            light_state: light,
            light_output: output,
        }
    }
    #[test]
    fn power_reuses_state_and_keeps_wifi_lock_failure_order() {
        let r = runtime();
        unsafe { r.power_saving(1) };
        with(|s| {
            assert!(s.power.low);
            assert_eq!(
                s.trace,
                ["wifi", "lock", "diag:0", "frequency:10", "unlock"]
            );
            s.trace.clear();
            s.wifi = true;
            s.frequency_ok = false;
        });
        unsafe { r.power_saving(1) };
        with(|s| {
            assert!(s.power.low);
            assert_eq!(
                s.trace,
                [
                    "wifi",
                    "lock",
                    "diag:1",
                    "frequency:160",
                    "diag:2",
                    "unlock"
                ]
            );
            s.trace.clear();
            s.frequency_ok = true;
        });
        unsafe { r.power_saving(1) };
        with(|s| {
            assert!(!s.power.low);
            assert_eq!(
                s.trace,
                ["wifi", "lock", "diag:1", "frequency:160", "unlock"]
            );
        });
    }
    #[test]
    fn tilt_takes_activity_only_and_keeps_wake_delay() {
        with(|s| {
            s.tilt.available = true;
            s.now = 1000;
        });
        let r = runtime();
        assert_eq!(unsafe { r.tilt_update(1, 0, 1) }, 0);
        with(|s| {
            assert!(s.tilt.awake);
            assert!(!s.trace.iter().any(|x| x == "imu-read"));
            s.now = 1600;
        });
        assert_eq!(unsafe { r.tilt_update(1, 0, 1) }, 1);
        with(|s| {
            assert!(s.tilt.forward);
            assert!(!s.tilt.activity);
        });
        assert_eq!(unsafe { r.tilt_update(1, 0, 1) }, 0);
        assert!(unsafe {
            hal::tilt::xp_hal_tilt_take(with(|s| &mut *s.tilt as *mut Tilt).cast(), 0)
        });
    }
    #[test]
    fn frontlight_changes_output_only_when_on_changes_including_zero_brightness() {
        let r = runtime();
        assert_eq!(unsafe { r.frontlight_state() }, 0);
        unsafe {
            r.frontlight_set(1);
            r.frontlight_set(1);
            r.frontlight_set(0)
        };
        with(|s| {
            assert_eq!(s.trace, ["light:60", "light:0"]);
            s.light.brightness = 0;
            s.trace.clear();
        });
        unsafe { r.frontlight_set(1) };
        assert_eq!(unsafe { r.frontlight_state() }, 1);
        with(|s| assert_eq!(s.trace, ["light:0"]));
    }
    #[test]
    fn idle_wait_wraps_and_stops_for_power_or_adc_without_extra_delay() {
        let r = runtime();
        unsafe { r.idle_wait(0, 10) };
        with(|s| {
            assert!(s.trace.is_empty());
            s.now = u32::MAX - 5;
        });
        unsafe { r.idle_wait(20, 10) };
        with(|s| {
            assert_eq!(s.now, 14);
            assert_eq!(s.trace, ["delay:10", "sample", "delay:10", "sample"]);
            s.trace.clear();
            s.samples = vec![(1, 0, 4095, 4095)];
        });
        unsafe { r.idle_wait(50, 10) };
        with(|s| {
            assert_eq!(s.trace, ["delay:10", "sample"]);
            s.trace.clear();
            s.samples = vec![(0, 0, 4000, -1), (0, 0, -1, 3999)];
        });
        unsafe { r.idle_wait(50, 10) };
        with(|s| assert_eq!(s.trace, ["delay:10", "sample", "delay:10", "sample"]));
    }

    #[test]
    fn idle_wait_stops_for_raw_auxiliary_contact() {
        with(|s| s.samples = vec![(0, 1, -1, -1)]);
        unsafe { runtime().idle_wait(50, 10) };
        with(|s| assert_eq!(s.trace, ["delay:10", "sample"]));
    }
}
