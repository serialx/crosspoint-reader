#![cfg(test)]
#![allow(dead_code)]
use std::cell::RefCell;
thread_local! { static CALLS: RefCell<Vec<&'static str>> = const { RefCell::new(Vec::new()) }; }
fn record(name: &'static str) {
    CALLS.with_borrow_mut(|calls| calls.push(name));
}
fn take() -> Vec<&'static str> {
    CALLS.with_borrow_mut(core::mem::take)
}
mod ble_runtime {
    pub struct Status {
        pub compiled_in: bool,
        pub running: bool,
        pub starting: bool,
        pub stopping: bool,
    }
    thread_local! { pub static RELEASE: std::cell::Cell<u8> = const { std::cell::Cell::new(0) }; }
    pub unsafe fn xp_ble_status(out: *mut Status) {
        super::record("status");
        out.write(Status {
            compiled_in: true,
            running: false,
            starting: true,
            stopping: false,
        });
    }
    pub fn xp_ble_note() -> u8 {
        super::record("note");
        7
    }
    pub unsafe fn xp_ble_before_build() -> u8 {
        super::record("build");
        RELEASE.with(|r| r.get())
    }
    pub unsafe fn xp_ble_settle_start() {
        super::record("settle");
    }
    pub unsafe fn xp_ble_holds_heap() -> bool {
        super::record("holds");
        true
    }
    pub fn xp_ble_after_paint() {
        super::record("paint");
    }
    pub unsafe fn xp_ble_switch_off() -> bool {
        super::record("off");
        false
    }
    pub unsafe fn xp_ble_request_connect() {
        super::record("connect");
    }
}
mod plugin {
    pub enum Event {
        ReaderExit = 1,
        ReaderSession = 2,
    }
    #[derive(Clone, Copy)]
    pub struct Subscriber(pub u8);
    pub unsafe fn xp_plugin_any(table: *const [Subscriber; 8], event: u32) -> bool {
        super::record("any");
        (*table).iter().any(|s| s.0 & (1 << event) != 0)
    }
}
mod settings {
    pub fn xp_setting_line_compression(sd: bool, spacing: u8) -> f32 {
        super::record("compression");
        assert_eq!(sd, super::FAMILY.with(|f| !f.get().is_empty()));
        assert_eq!(spacing, 3);
        0.875
    }
}
mod abi {
    use super::plugin::Subscriber;
    #[derive(Default)]
    pub struct XpEpubRadio {
        pub compiled_in: u8,
        pub running: u8,
        pub starting: u8,
        pub stopping: u8,
        pub note: u8,
    }
    pub struct XpEpubPeer {
        pub name: [u8; 64],
        pub addr: [u8; 18],
    }
    pub struct Text {
        pub len: usize,
        pub data: &'static [u8],
    }
    impl Text {
        pub unsafe fn bytes(&self) -> Option<&[u8]> {
            Some(self.data)
        }
    }
    pub struct XpEpubSettings {
        pub font: i32,
        pub font_family: Text,
        pub line_spacing: u8,
        pub font_size: u8,
    }
    pub struct XpEpubRenderSpec {
        pub font: i32,
        pub compression: f32,
        pub width: u16,
        pub height: u16,
    }
    pub struct XpEpubLeaves {
        pub radio_peer: unsafe extern "C" fn(*mut XpEpubPeer),
        pub subscribers: unsafe extern "C" fn() -> *const Subscriber,
        pub settings_snapshot: unsafe extern "C" fn(u8, *mut XpEpubSettings),
        pub settings_render_spec: unsafe extern "C" fn(u16, u16, *mut XpEpubRenderSpec),
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/epub/ports.rs"]
mod ports;
unsafe extern "C" fn peer(out: *mut abi::XpEpubPeer) {
    record("peer");
    out.write(abi::XpEpubPeer {
        name: [1; 64],
        addr: [2; 18],
    });
}
thread_local! { static SUBSCRIBERS: std::cell::UnsafeCell<[plugin::Subscriber; 8]> = const { std::cell::UnsafeCell::new([plugin::Subscriber(0); 8]) }; }
unsafe extern "C" fn subscribers() -> *const plugin::Subscriber {
    record("view");
    SUBSCRIBERS.with(|s| s.get().cast())
}
unsafe extern "C" fn snapshot(resolve: u8, out: *mut abi::XpEpubSettings) {
    record("snapshot");
    assert_eq!(resolve, 1);
    out.write(abi::XpEpubSettings {
        font: 0,
        font_family: FAMILY.with(|f| abi::Text {
            len: f.get().len(),
            data: f.get(),
        }),
        font_size: 15,
        line_spacing: 3,
    });
}
unsafe extern "C" fn spec(width: u16, height: u16, out: *mut abi::XpEpubRenderSpec) {
    record("spec");
    out.write(abi::XpEpubRenderSpec {
        font: 0,
        compression: 0.0,
        width,
        height,
    });
}
fn leaves() -> abi::XpEpubLeaves {
    abi::XpEpubLeaves {
        radio_peer: peer,
        subscribers,
        settings_snapshot: snapshot,
        settings_render_spec: spec,
    }
}
#[test]
fn radio_preserves_status_note_peer_order_and_optional_peer() {
    let mut out = abi::XpEpubRadio::default();
    unsafe {
        leaves().radio_status(&mut out, core::ptr::null_mut());
    }
    assert_eq!(take(), ["status", "note"]);
    assert_eq!(
        (
            out.compiled_in,
            out.running,
            out.starting,
            out.stopping,
            out.note
        ),
        (1, 0, 1, 0, 7)
    );
    let mut peer = core::mem::MaybeUninit::uninit();
    unsafe {
        leaves().radio_status(&mut out, peer.as_mut_ptr());
    }
    assert_eq!(take(), ["status", "note", "peer"]);
    assert_eq!(unsafe { peer.assume_init() }.name, [1; 64]);
}
#[test]
fn release_boolean_and_void_relays_keep_owner_results() {
    for result in 0..=2 {
        ble_runtime::RELEASE.with(|r| r.set(result));
        assert_eq!(
            unsafe { leaves().radio_before_build() },
            u8::from(result != 0)
        );
        assert_eq!(take(), ["build"]);
    }
    unsafe {
        leaves().radio_settle_start();
        assert_eq!(leaves().radio_holds_heap(), 1);
        leaves().radio_after_paint();
        leaves().radio_switch_off();
        leaves().radio_request_connect();
    }
    assert_eq!(take(), ["settle", "holds", "paint", "off", "connect"]);
}
#[test]
fn subscriptions_borrow_the_current_table_without_caching() {
    unsafe {
        SUBSCRIBERS.with(|s| (*s.get())[7].0 = 1 << 1);
        assert_eq!(leaves().has_exit_subscriber(), 1);
        assert_eq!(leaves().has_session_subscriber(), 0);
        SUBSCRIBERS.with(|s| (*s.get())[7].0 = 1 << 2);
        assert_eq!(leaves().has_exit_subscriber(), 0);
        assert_eq!(leaves().has_session_subscriber(), 1);
    }
    assert_eq!(
        take(),
        ["view", "any", "view", "any", "view", "any", "view", "any"]
    );
}
#[test]
fn render_spec_resolves_font_once_before_compression_and_raw_fields() {
    for (family, loaded, expected, calls) in [
        (
            b"".as_slice(),
            0,
            fonts::BUILTIN_FONT_IDS[1],
            vec!["snapshot", "compression", "spec"],
        ),
        (
            b"SD".as_slice(),
            -77,
            -77,
            vec!["snapshot", "loaded", "compression", "spec"],
        ),
        (
            b"SD".as_slice(),
            0,
            fonts::BUILTIN_FONT_IDS[1],
            vec!["snapshot", "loaded", "compression", "spec"],
        ),
    ] {
        FAMILY.with(|f| f.set(family));
        LOADED.with(|f| f.set(loaded));
        let mut out = core::mem::MaybeUninit::uninit();
        unsafe {
            leaves().settings_render_spec(app_abi::FontOwner(7), 456, 789, out.as_mut_ptr());
        }
        let out = unsafe { out.assume_init() };
        assert_eq!(take(), calls);
        assert_ne!(out.font, 0);
        assert_eq!(
            (out.font, out.compression, out.width, out.height),
            (expected, 0.875, 456, 789)
        );
    }
}

thread_local! {
    static FAMILY: std::cell::Cell<&'static [u8]> = const {std::cell::Cell::new(b"")};
    static LOADED: std::cell::Cell<i32> = const {std::cell::Cell::new(0)};
}
mod app_abi {
    #[derive(Clone, Copy)]
    pub struct FontOwner(pub usize);
    impl FontOwner {
        pub fn get(self) -> *mut core::ffi::c_void {
            self.0 as *mut _
        }
    }
}
mod font_sd {
    pub mod catalog {
        pub unsafe fn loaded_id(owner: *mut core::ffi::c_void, family: &[u8]) -> i32 {
            assert_eq!(owner as usize, 7);
            assert_eq!(family, b"SD");
            crate::record("loaded");
            crate::LOADED.with(|f| f.get())
        }
    }
}
mod fonts {
    use super::{
        abi::{XpEpubLeaves, XpEpubSettings},
        app_abi::FontOwner,
    };
    include!(concat!(env!("OUT_DIR"), "/builtin_font_ids.rs"));
    include!(concat!(env!("OUT_DIR"), "/settings_method.rs"));
}
