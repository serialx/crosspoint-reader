#![allow(dead_code)]
#[path = "../../rust/crates/xp-ffi/src/hal/mod.rs"]
mod hal;
#[path = "../../rust/crates/xp-ui/src/app/input.rs"]
mod input;
use input::{Feedback, HomeAction, HomeBindings, HomeContact, HomeInput};
use std::cell::RefCell;
mod abi {
    pub(crate) type XpEpubLeaves = crate::Leaves;
    pub struct XpEpubInputFacts;
    #[derive(Default)]
    pub struct Input {
        pub now_ms: u32,
        pub swipe: u8,
    }
    #[derive(Default)]
    pub struct MappedSnapshot {
        pub input: Input,
        pub has_home_key: u8,
        pub home_key_tapped: u8,
        pub home_key_long_pressed: u8,
        pub home_key_pressed: u8,
        pub power_released: u8,
    }
    pub struct Runtime {
        pub input_poll: unsafe extern "C" fn(u8, u8) -> crate::hal::gpio::Poll,
        pub input_mapped_query: unsafe extern "C" fn(u8, u8, u8, u8, *mut MappedSnapshot),
        pub touch_feedback: unsafe extern "C" fn(u8),
    }
}
struct Settings {
    home_tap: u8,
    home_double: u8,
    home_long: u8,
    short_power: u8,
}
struct App {
    runtime: abi::Runtime,
    input: RefCell<HomeInput>,
}
impl App {
    fn settings(&self) -> Settings {
        TRACE.with_borrow_mut(|trace| trace.push("settings"));
        Settings {
            home_tap: 0,
            home_double: 10,
            home_long: 9,
            short_power: 7,
        }
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/input_ports.rs"]
mod input_ports;
thread_local! {
    static TRACE: RefCell<Vec<&'static str>> = const { RefCell::new(Vec::new()) };
    static STATE: RefCell<[u8; 4]> = const { RefCell::new([0; 4]) };
    static LONG_HOME: RefCell<bool> = const { RefCell::new(false) };
    static EXPECTED_ACTION: RefCell<u8> = const { RefCell::new(7) };
    static ACTION: RefCell<u8> = const { RefCell::new(1) };
}
unsafe extern "C" fn poll(phase: u8, shared: u8) -> hal::gpio::Poll {
    match phase {
        0 => {
            assert_eq!(shared, 1);
            TRACE.with_borrow_mut(|trace| trace.push("sdk"));
            hal::gpio::Poll {
                state: STATE.with(|state| state.as_ptr().cast()),
                buttons: 2,
                usb: true,
            }
        }
        1 => {
            // The real Rust HAL update must have run between raw polling and feedback.
            STATE.with_borrow(|state| assert_eq!(*state, [1, 1, 2, 1]));
            TRACE.with_borrow_mut(|trace| trace.push("haptic"));
            hal::gpio::Poll {
                state: std::ptr::null_mut(),
                buttons: 0,
                usb: false,
            }
        }
        2 => {
            TRACE.with_borrow(|trace| {
                assert_eq!(*trace, ["sdk", "haptic", "home-snapshot", "settings"])
            });
            TRACE.with_borrow_mut(|trace| trace.push("finish"));
            hal::gpio::Poll {
                state: std::ptr::null_mut(),
                buttons: 0,
                usb: false,
            }
        }
        _ => panic!("unexpected phase"),
    }
}
unsafe extern "C" fn query(
    physical: u8,
    full: u8,
    consume: u8,
    home: u8,
    out: *mut abi::MappedSnapshot,
) {
    assert_eq!((full, consume), (0, 0));
    *out = abi::MappedSnapshot::default();
    if physical == 0 {
        TRACE.with_borrow_mut(|trace| trace.push("home-snapshot"));
        (*out).power_released = 1;
        if LONG_HOME.with_borrow(|value| *value) {
            (*out).has_home_key = 1;
            (*out).home_key_long_pressed = 1;
        }
    } else {
        TRACE.with_borrow_mut(|trace| trace.push("physical-snapshot"));
        EXPECTED_ACTION.with_borrow(|expected| assert_eq!(home, *expected)); // Direct input_polled policy ran before the returned frame.
    }
    ACTION.with_borrow_mut(|value| *value = home);
}
unsafe extern "C" fn feedback(long: u8) {
    assert_eq!(long, 1);
    TRACE.with_borrow_mut(|trace| trace.push("long-feedback"));
}
#[test]
fn shipping_poll_composes_edges_home_and_next_snapshot_once() {
    TRACE.with_borrow_mut(Vec::clear);
    STATE.with_borrow_mut(|state| *state = [0; 4]);
    let app = App {
        runtime: abi::Runtime {
            input_poll: poll,
            input_mapped_query: query,
            touch_feedback: feedback,
        },
        input: RefCell::new(HomeInput::default()),
    };
    app.poll_input(true);
    assert_eq!(app.input.borrow().action(), HomeAction::Bookmark);
    TRACE.with_borrow(|trace| {
        assert_eq!(
            *trace,
            [
                "sdk",
                "haptic",
                "home-snapshot",
                "settings",
                "finish",
                "physical-snapshot"
            ]
        )
    });
}

mod app {
    pub unsafe fn home_action() -> u8 {
        super::ACTION.with_borrow(|value| *value)
    }
}
use xp_ui::reader::input::{Query, Services};
#[repr(C)]
struct NativeQuery {
    mapped: *const core::ffi::c_void,
    tilt: *mut hal::tilt::Tilt,
    home: u8,
}
#[repr(C)]
struct Tilt {
    times: [u32; 4],
    available: bool,
    forward: bool,
    back: bool,
    activity: bool,
    in_tilt: bool,
    awake: bool,
}
thread_local! {
    static TILT: RefCell<Tilt> = const { RefCell::new(Tilt { times: [0; 4], available: false, forward: true, back: true, activity: true, in_tilt: false, awake: false }) };
    static SOURCE_READS: RefCell<usize> = const { RefCell::new(0) };
}
struct EpubSession {
    leaves: &'static Leaves,
    input_services: std::cell::OnceCell<&'static Services>,
}
struct Leaves {
    input_sources: unsafe extern "C" fn() -> *const Services,
    input_facts: unsafe extern "C" fn(u8, *mut abi::XpEpubInputFacts),
    input_snapshot: unsafe extern "C" fn(u8, *mut xp_ui::model::Input),
}
#[path = "../../rust/crates/xp-ffi/src/app/epub/input_ports.rs"]
mod reader_input_ports;
unsafe extern "C" fn source_read(ctx: *mut core::ffi::c_void, query: u8) -> u32 {
    let ctx = &mut *ctx.cast::<NativeQuery>();
    assert!(ctx.mapped.is_null());
    if query == Query::TiltNext as u8 || query == Query::TiltPrev as u8 {
        ctx.tilt = TILT.with(|tilt| tilt.as_ptr().cast());
        return 0;
    }
    u32::from(ctx.home)
}
unsafe extern "C" fn source_button(ctx: *mut core::ffi::c_void, _: u32, _: u8, _: u32) -> u8 {
    (*ctx.cast::<NativeQuery>()).home
}
unsafe extern "C" fn source_tap(_: *mut core::ffi::c_void, x: *mut i32, y: *mut i32) -> u8 {
    *x = 3;
    *y = 7;
    1
}
unsafe extern "C" fn source() -> *const Services {
    SOURCE_READS.with_borrow_mut(|value| *value += 1);
    // Same process-lifetime table contract as firmware; the fixture intentionally leaks one table.
    Box::into_raw(Box::new(Services {
        context: core::ptr::null_mut(),
        read: Some(source_read),
        button: Some(source_button),
        tap: Some(source_tap),
    }))
}
#[test]
fn reader_services_cache_only_address_and_consume_tilt_lazily() {
    let leaves = Box::leak(Box::new(Leaves {
        input_sources: source,
        input_facts: unused_facts,
        input_snapshot: unused_snapshot,
    }));
    let session = EpubSession {
        leaves,
        input_services: std::cell::OnceCell::new(),
    };
    let first = session.input_services();
    unsafe {
        ACTION.with_borrow_mut(|value| *value = 5);
        assert_eq!(first.read.unwrap()(first.context, Query::HeldMs as u8), 5);
        ACTION.with_borrow_mut(|value| *value = 1);
        assert_eq!(first.button.unwrap()(first.context, 1, 0, 0), 1);
        assert_eq!(first.read.unwrap()(first.context, Query::TiltNext as u8), 1);
        assert_eq!(first.read.unwrap()(first.context, Query::TiltNext as u8), 0);
        TILT.with_borrow(|tilt| assert!(tilt.back && tilt.activity));
        let second = session.input_services();
        assert_eq!(
            second.read.unwrap()(second.context, Query::TiltPrev as u8),
            1
        );
        let (mut x, mut y) = (0, 0);
        assert_eq!(second.tap.unwrap()(second.context, &mut x, &mut y), 1);
        assert_eq!((x, y), (3, 7));
    }
    SOURCE_READS.with_borrow(|value| assert_eq!(*value, 1));
}

unsafe extern "C" fn unused_facts(_: u8, _: *mut abi::XpEpubInputFacts) {
    panic!("unused");
}
unsafe extern "C" fn unused_snapshot(_: u8, _: *mut xp_ui::model::Input) {
    panic!("unused");
}

#[test]
fn native_transfer_defers_home_until_next_poll_without_repeating_feedback() {
    let app = App {
        runtime: abi::Runtime {
            input_poll: poll,
            input_mapped_query: query,
            touch_feedback: feedback,
        },
        input: RefCell::new(HomeInput::default()),
    };
    LONG_HOME.with_borrow_mut(|value| *value = true);
    app.input_polled(true);
    assert_eq!(app.input.borrow().action(), HomeAction::ReaderMenu);
    TRACE.with_borrow(|trace| assert_eq!(*trace, ["home-snapshot", "settings", "long-feedback"]));
    TRACE.with_borrow_mut(Vec::clear);
    LONG_HOME.with_borrow_mut(|value| *value = false);
    EXPECTED_ACTION.with_borrow_mut(|value| *value = HomeAction::ReaderMenu as u8);
    app.poll_input(true);
    assert_eq!(app.input.borrow().action(), HomeAction::ReaderMenu);
    TRACE.with_borrow(|trace| {
        assert_eq!(
            *trace,
            [
                "sdk",
                "haptic",
                "home-snapshot",
                "settings",
                "finish",
                "physical-snapshot"
            ]
        )
    });
}
