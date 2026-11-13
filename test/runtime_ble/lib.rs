#![cfg(test)]
use std::{cell::RefCell, ffi::c_void};
#[derive(Debug, PartialEq)]
enum Call {
    Wifi,
    Tick(u8, u32, bool, bool, bool),
    Before(u32, bool),
    TryLock,
    Release(usize, u8),
    Unlock,
    Event(u8, u8, u8),
    Power(u8),
    Hold(bool),
    Restart(u8, u8),
    Light,
    Translate(u16),
    Popup,
    Delay(u32),
}
#[derive(Default)]
struct Fake {
    calls: Vec<Call>,
    busy: bool,
    allowed: bool,
    light: u8,
    accepted: bool,
    wifi: bool,
}
thread_local! { static FAKE: RefCell<Fake> = RefCell::new(Fake::default()); }
fn record(call: Call) {
    FAKE.with_borrow_mut(|f| f.calls.push(call));
}
mod abi {
    use super::*;
    pub const REMOTE_CLICK: u8 = 8;
    pub const REMOTE_TURN: u8 = 9;
    pub const REMOTE_SHORTCUT: u8 = 10;
    pub const ACCEPTED: u8 = 0;
    #[repr(u8)]
    pub enum BleRestartPhase {
        Allowed,
        Arm,
        Execute,
    }
    pub struct BleScene {
        pub r#where: u8,
        pub visit: u32,
        pub page_shown: u8,
        pub storage_busy: u8,
    }
    #[derive(Clone, Copy)]
    pub struct RemoteTurn {
        pub forward: u8,
        pub chapter: u8,
    }
    pub union EventValue {
        pub remote_turn: RemoteTurn,
        pub reader_menu: u8,
        pub remote_button: u8,
    }
    pub struct Event {
        pub kind: u8,
        pub value: EventValue,
    }
    pub struct Runtime {
        pub wifi_active: unsafe extern "C" fn() -> u8,
        pub render_try_lock: unsafe extern "C" fn() -> *mut c_void,
        pub ble_restart: unsafe extern "C" fn(u8, u8) -> u8,
        pub delay_ms: unsafe extern "C" fn(u32),
    }
    impl Runtime {
        pub unsafe fn power_saving(&self, enabled: u8) {
            record(Call::Power(enabled));
        }
        pub unsafe fn frontlight_state(&self) -> u8 {
            record(Call::Light);
            FAKE.with_borrow(|f| f.light)
        }
    }
    pub struct FontOwner;
    impl FontOwner {
        pub unsafe fn get(&self) -> *const c_void {
            std::ptr::dangling::<u128>().cast()
        }
    }
    pub struct Resources {
        pub font_owner: FontOwner,
        pub popup_draw: unsafe extern "C" fn(u16),
    }
    impl Resources {
        pub unsafe fn popup_draw(&self, id: u16) {
            (self.popup_draw)(id);
        }
        pub unsafe fn translate(&self, id: u16) -> u16 {
            record(Call::Translate(id));
            id
        }
    }
}
mod ble_runtime {
    use super::*;
    #[derive(Default)]
    pub struct Scene {
        pub location: u8,
        pub visit: u32,
        pub page_shown: bool,
        pub storage_busy: bool,
        pub wifi_on: bool,
        pub sleeping: bool,
        pub local_key: bool,
        pub book_indexing: bool,
    }
    pub unsafe fn xp_ble_tick(s: &Scene) -> bool {
        assert!(!s.sleeping && !s.local_key && !s.book_indexing);
        record(Call::Tick(
            s.location,
            s.visit,
            s.page_shown,
            s.storage_busy,
            s.wifi_on,
        ));
        true
    }
    pub unsafe fn xp_ble_before_screen(timeout: u32, reader: bool) -> bool {
        record(Call::Before(timeout, reader));
        reader
    }
}
mod font_runtime {
    use super::*;
    pub unsafe fn release(owner: *const c_void, operation: u8) {
        record(Call::Release(owner as usize, operation));
    }
}
struct App {
    runtime: &'static abi::Runtime,
    resources: &'static abi::Resources,
}
static RUNTIME: abi::Runtime = abi::Runtime {
    wifi_active: wifi,
    render_try_lock: try_lock,
    ble_restart: restart,
    delay_ms: delay,
};
static RESOURCES: abi::Resources = abi::Resources {
    font_owner: abi::FontOwner,
    popup_draw: popup,
};
static APP: App = App {
    runtime: &RUNTIME,
    resources: &RESOURCES,
};
fn app() -> Option<&'static App> {
    Some(&APP)
}
struct RenderGuard {
    token: *mut c_void,
    runtime: &'static abi::Runtime,
}
impl Drop for RenderGuard {
    fn drop(&mut self) {
        assert_eq!(self.token as usize, 1);
        assert!(std::ptr::eq(self.runtime, &RUNTIME));
        record(Call::Unlock);
    }
}
unsafe fn xp_app_event(event: *const abi::Event) -> u8 {
    let e = &*event;
    let (a, b) = match e.kind {
        abi::REMOTE_TURN => (e.value.remote_turn.forward, e.value.remote_turn.chapter),
        abi::REMOTE_SHORTCUT => (e.value.reader_menu, 0),
        abi::REMOTE_CLICK => (e.value.remote_button, 0),
        _ => panic!("unexpected event"),
    };
    record(Call::Event(e.kind, a, b));
    if FAKE.with_borrow(|f| f.accepted) {
        0
    } else {
        1
    }
}
unsafe extern "C" fn wifi() -> u8 {
    record(Call::Wifi);
    u8::from(FAKE.with_borrow(|f| f.wifi))
}
unsafe extern "C" fn try_lock() -> *mut c_void {
    record(Call::TryLock);
    if FAKE.with_borrow(|f| f.busy) {
        std::ptr::null_mut()
    } else {
        std::ptr::dangling_mut::<c_void>()
    }
}
unsafe extern "C" fn restart(phase: u8, light: u8) -> u8 {
    record(Call::Restart(phase, light));
    u8::from(FAKE.with_borrow(|f| f.allowed))
}
unsafe extern "C" fn delay(ms: u32) {
    record(Call::Delay(ms));
}
unsafe extern "C" fn popup(id: u16) {
    assert_eq!(id, xp_core::i18n::ids::STR_LOADING_POPUP);
    record(Call::Popup);
}
#[path = "../../rust/crates/xp-ffi/src/app/ble_ports.rs"]
pub(crate) mod ble_ports;
fn take() -> Vec<Call> {
    FAKE.with_borrow_mut(|f| std::mem::take(&mut f.calls))
}
#[test]
fn tick_reads_live_wifi_and_preserves_scene() {
    let view = abi::BleScene {
        r#where: 3,
        visit: 91,
        page_shown: 1,
        storage_busy: 1,
    };
    for wifi in [false, true, false] {
        FAKE.with_borrow_mut(|f| f.wifi = wifi);
        assert_eq!(unsafe { RUNTIME.ble_tick(&view) }, 1);
        assert_eq!(take(), [Call::Wifi, Call::Tick(3, 91, true, true, wifi)]);
    }
    for reader in [0, 1] {
        assert_eq!(unsafe { RUNTIME.ble_before_screen_change(reader) }, reader);
        assert_eq!(take(), [Call::Before(0, reader != 0)]);
    }
}
#[test]
fn cache_release_never_waits_and_unlocks_after_mutation() {
    FAKE.with_borrow_mut(|f| f.busy = true);
    assert!(!unsafe { ble_ports::release() });
    assert_eq!(take(), [Call::TryLock]);
    FAKE.with_borrow_mut(|f| f.busy = false);
    assert!(unsafe { ble_ports::release() });
    assert_eq!(take(), [Call::TryLock, Call::Release(16, 1), Call::Unlock]);
}
#[test]
fn every_action_preserves_payload_and_admission_result() {
    let expected = [
        (9, 1, 0),
        (9, 0, 0),
        (9, 1, 1),
        (9, 0, 1),
        (10, 1, 0),
        (10, 0, 0),
        (8, 1, 0),
        (8, 2, 0),
    ];
    for accepted in [false, true] {
        FAKE.with_borrow_mut(|f| f.accepted = accepted);
        for (index, (kind, a, b)) in expected.iter().enumerate() {
            assert_eq!(unsafe { ble_ports::deliver(index as u8 + 1) }, accepted);
            assert_eq!(take(), [Call::Event(*kind, *a, *b)]);
        }
    }
    for action in [0, 9, 255] {
        assert!(!unsafe { ble_ports::deliver(action) });
        assert!(take().is_empty());
    }
}
#[test]
fn releasing_speed_does_not_force_power_saving() {
    unsafe { ble_ports::hold(false) };
    assert!(take().is_empty());
    unsafe { ble_ports::hold(true) };
    assert_eq!(take(), [Call::Power(0)]);
}
#[test]
fn restart_preserves_sleep_precedence_and_live_light() {
    unsafe { ble_ports::restart() };
    assert_eq!(take(), [Call::Restart(0, 0)]);
    for light in [0, 1] {
        FAKE.with_borrow_mut(|f| {
            f.allowed = true;
            f.light = light;
        });
        unsafe { ble_ports::restart() };
        assert_eq!(
            take(),
            [
                Call::Restart(0, 0),
                Call::Light,
                Call::Restart(1, light),
                Call::Translate(xp_core::i18n::ids::STR_LOADING_POPUP),
                Call::Popup,
                Call::Delay(50),
                Call::Restart(2, 0)
            ]
        );
    }
}

mod app {
    pub(crate) use crate::ble_ports;
}
mod runtime_host_bridge {
    struct Holder {
        hold: unsafe extern "C" fn(bool),
    }
    unsafe extern "C" fn publish(held: bool) {
        super::record(super::Call::Hold(held));
    }
    static HOLDER: Holder = Holder { hold: publish };
    fn svc() -> &'static Holder {
        &HOLDER
    }
    include!(concat!(env!("OUT_DIR"), "/hold.rs"));
    pub unsafe fn invoke(held: bool) {
        hold(held);
    }
}
#[test]
fn native_hold_publication_precedes_rust_power_policy() {
    unsafe { runtime_host_bridge::invoke(true) };
    assert_eq!(take(), [Call::Hold(true), Call::Power(0)]);
    unsafe { runtime_host_bridge::invoke(false) };
    assert_eq!(take(), [Call::Hold(false)]);
}
