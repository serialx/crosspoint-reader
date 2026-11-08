use super::super::{keyboard_tests as kb, Owner, Target, APP};
use super::*;
use core::ptr;
use std::{cell::RefCell, vec, vec::Vec};
use xp_ui::{
    app::Services as _,
    network::{
        wifi_physical::{
            Bytes, CredentialStorage, CredentialView, Diagnostic, LastStorage, PaintData,
        },
        wifi_view,
    },
    scene::{Command, Rect},
};
#[derive(Clone, Debug, PartialEq)]
struct ResultValue {
    connected: bool,
    ssid: Vec<u8>,
    ip: Vec<u8>,
}
struct State {
    fail_owner: bool,
    resource: bool,
    input: Input,
    long: Option<(i32, i32)>,
    scan: i32,
    connection: i32,
    events: Vec<&'static str>,
    received: usize,
    connects: Vec<(Vec<u8>, Vec<u8>)>,
    result: Option<ResultValue>,
    delivered: Vec<ResultValue>,
}
impl Default for State {
    fn default() -> Self {
        Self {
            fail_owner: false,
            resource: false,
            input: Input::default(),
            long: None,
            scan: -1,
            connection: 0,
            events: Vec::new(),
            received: 0,
            connects: Vec::new(),
            result: None,
            delivered: Vec::new(),
        }
    }
}
std::thread_local! {static STATE:RefCell<State>=RefCell::new(State::default());}
fn state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|s| f(&mut s.borrow_mut()))
}
fn event(e: &'static str) {
    state(|s| s.events.push(e));
}
pub(super) fn allocate_owner(layout: core::alloc::Layout) -> *mut u8 {
    if state(|s| core::mem::take(&mut s.fail_owner)) {
        ptr::null_mut()
    } else {
        unsafe { std::alloc::alloc(layout) }
    }
}
pub(crate) unsafe extern "C" fn now() -> u32 {
    1000
}
pub(crate) unsafe extern "C" fn delay(_: u32) {}
pub(crate) unsafe extern "C" fn input_snapshot(out: *mut Input) {
    event("input");
    unsafe { out.write(state(|s| s.input)) };
}
pub(crate) unsafe extern "C" fn long_point(kind: u8, x: *mut i32, y: *mut i32) -> u8 {
    assert_eq!(kind, 0);
    event("long");
    if let Some((tx, ty)) = state(|s| s.long.take()) {
        unsafe {
            x.write(tx);
            y.write(ty)
        };
        1
    } else {
        0
    }
}
pub(crate) fn native_dropped() {
    event("native_drop");
}
pub(super) fn received(target: NonNull<Wifi>, text: Option<&[u8]>) {
    kb::wifi_received(text);
    let app = app().unwrap();
    assert!(matches!(app.signals.with(|s|s.target),Some(Target::Wifi(p))if p==target));
    assert!(matches!(app.navigation.current(),Some(Target::Wifi(p))if p==target));
    state(|s| s.received += 1);
    event("keyboard_deliver");
}
pub(crate) fn with_result(_: *mut c_void, sink: abi::ResultSink, context: *mut c_void) {
    assert!(kb::wifi_locked());
    let value = state(|s| s.result.clone()).unwrap();
    let view = abi::XpAppResultView {
        original: (&value as *const ResultValue).cast(),
        value: abi::XpAppResultPayload {
            wifi: abi::XpAppWifiResult {
                ssid: abi::Text::from_bytes(&value.ssid),
                ip: abi::Text::from_bytes(&value.ip),
                connected: value.connected.into(),
            },
        },
        kind: 1,
        cancelled: (!value.connected).into(),
    };
    unsafe { sink(context, &view) };
}
pub(crate) fn legacy_deliver(parent: *mut c_void, result: *const c_void) {
    assert!(!kb::wifi_locked());
    assert!(
        matches!(app().unwrap().navigation.current(),Some(Target::Legacy(p))if p.as_ptr()==parent)
    );
    state(|s| {
        s.delivered
            .push(unsafe { &*result.cast::<ResultValue>() }.clone())
    });
    event("result_deliver");
}
macro_rules! c{($($name:ident:$ty:ty),*;$ret:ty;$body:expr)=>{{unsafe extern "C" fn callback($($name:$ty),*)->$ret{let body:fn($($ty),*)->$ret=$body;body($($name),*)}callback}};}
#[no_mangle]
static XP_WIFI_PHYSICAL: Physical = Physical {
    allow_auto: c!(a:*mut c_void;u8;|_|0),
    resource_lock: c!(a:*mut c_void,b:u8;();|_,take|state(|s|{if take!=0{assert!(!s.resource);s.resource=true;}else{assert!(s.resource);s.resource=false;}})),
    release_resources: c!(a:*mut c_void;();|_|event("sdk_release")),
    layout: c!(a:*mut c_void,b:*mut wifi_view::Layout;();|_,out|{let mut l=wifi_view::Layout::default();let r=Rect{x:0,y:0,w:480,h:800};l.base.safe=r;l.base.list.screen=r;l.base.list.area=r;l.base.list.row_height=40;l.base.header_height=40;l.base.tab_height=24;l.base.spacing=8;unsafe{out.write(l)};}),
    width: c!(a:*mut c_void,b:*const core::ffi::c_char,c:i32,d:u8;i32;|_,text,_,_|unsafe{CStr::from_ptr(text)}.to_bytes().len()as i32*8),
    height: c!(a:*mut c_void,b:*const core::ffi::c_char,c:i32,d:u8,e:u8,f:i16;i32;|_,_,_,_,_,_|20),
    line: c!(a:*mut c_void,b:i32;i32;|_,_|20),
    paint: c!(a:*mut c_void,b:*const Command,c:Rect,d:*const PaintData;();|_,_,_,_|()),
    read_mac: c!(a:*mut u8;i32;|out|{unsafe{ptr::write_bytes(out,0,6)};0}),
    last_ssid_drop: c!(a:*mut LastStorage;();|_|()),
    last_ssid: c!(a:*mut LastStorage;Bytes;|_|Bytes::EMPTY),
    credential_lookup: c!(a:*mut CredentialStorage,b:Bytes,c:*mut CredentialView;u8;|_,_,out|{unsafe{out.write(CredentialView{ssid:Bytes::EMPTY,password:Bytes::EMPTY})};0}),
    credential_drop: c!(a:*mut CredentialStorage;();|_|()),
    store_load: c!(;();||{assert!(kb::wifi_locked());event("store_load");}),
    store_save: c!(a:Bytes,b:Bytes;();|_,_|{assert!(kb::wifi_locked()&&state(|s|s.resource));event("save");}),
    store_remove: c!(a:Bytes;();|_|()),
    store_last: c!(a:Bytes;();|_|{assert!(kb::wifi_locked()&&!state(|s|s.resource));event("last");}),
    store_count: c!(;u32;||0),
    store_has: c!(a:Bytes;u8;|_|0),
    station_mode: c!(;();||()),
    disconnect: c!(;();||event("disconnect")),
    scan_begin: c!(;();||state(|s|s.scan= -1)),
    scan_complete: c!(;i32;||state(|s|s.scan)),
    scan_ssid: c!(a:u32,b:*mut core::ffi::c_char;();|_,_|panic!("scan row")),
    scan_rssi: c!(a:u32;i32;|_|panic!("scan RSSI")),
    scan_encrypted: c!(a:u32;u8;|_|panic!("scan encryption")),
    scan_delete: c!(;();||state(|s|s.scan= -2)),
    connect: c!(a:*const core::ffi::c_char,b:*const core::ffi::c_char,c:u8;();|ssid,password,use_password|{state(|s|s.connects.push((unsafe{CStr::from_ptr(ssid)}.to_bytes().to_vec(),if use_password!=0{unsafe{CStr::from_ptr(password)}.to_bytes().to_vec()}else{Vec::new()})));}),
    connection_status: c!(;i32;||state(|s|s.connection)),
    connection_ip: c!(a:*mut core::ffi::c_char;();|out|unsafe{ptr::copy_nonoverlapping(c"192.0.2.1".as_ptr(),out,10)}),
    connected_diagnostic: c!(;();||()),
    clock_available: c!(;u8;||0),
    clock_synced: c!(;u8;||0),
    clock_sync: c!(;u8;||0),
    clock_mark_and_save: c!(;();||()),
    trusted_time_start: c!(;();||()),
    plugin_drain: c!(a:*mut c_void;();|_|()),
    diagnostic: c!(a:Diagnostic,b:Bytes,c:u32;();|_,_,_|()),
    prepare_result_retire_zc1: c!(a:*mut c_void,b:u8,c:Bytes,d:Bytes;();|_,connected,ssid,ip|state(|s|s.result=Some(ResultValue{connected:connected!=0,ssid:unsafe{ssid.as_slice()}.to_vec(),ip:unsafe{ip.as_slice()}.to_vec()}))),
};
#[test]
fn native_host_keeps_three_pointer_sized_words() {
    assert_eq!(
        core::mem::size_of::<NativeHost>(),
        3 * core::mem::size_of::<usize>()
    );
}
fn install(app: &'static App) -> NonNull<Wifi> {
    let owner = create(kb::wifi_context()).unwrap();
    let p = owner.pointer();
    app.navigation.queue(Transition::Push(Owner::Wifi(owner)));
    assert!(app.navigation.drain(app));
    p
}
fn tick(app: &App, input: Input) {
    state(|s| s.input = input);
    app.activity_loop();
}
#[test]
fn real_owned_keyboard_returns_after_destroy_publish_unlock_then_wifi_connects_and_saves() {
    let _serial = super::super::dialog_tests::APP_LOCK.lock().unwrap();
    let app = kb::wifi_app();
    kb::wifi_parent(app);
    let target = install(app);
    assert_eq!(facts(target).prevent_sleep, 1);
    state(|s| s.scan = 0);
    tick(app, Input::default());
    kb::wifi_paint(app);
    kb::wifi_text(b"manual-hidden");
    tick(
        app,
        Input {
            released: xp_ui::model::CONFIRM,
            ..Input::default()
        },
    );
    assert!(matches!(
        app.navigation.current(),
        Some(Target::Keyboard(_))
    ));
    kb::wifi_paint(app);
    kb::wifi_key(
        app,
        Input {
            pressed: xp_ui::model::CONFIRM,
            released: xp_ui::model::CONFIRM,
            ..Input::default()
        },
    );
    assert!(matches!(app.navigation.current(),Some(Target::Wifi(p))if p==target));
    kb::wifi_text(b"correct-pass");
    tick(app, Input::default());
    assert!(matches!(
        app.navigation.current(),
        Some(Target::Keyboard(_))
    ));
    kb::wifi_paint(app);
    kb::wifi_key(
        app,
        Input {
            pressed: xp_ui::model::CONFIRM,
            released: xp_ui::model::CONFIRM,
            ..Input::default()
        },
    );
    assert_eq!(state(|s| s.received), 2);
    tick(app, Input::default());
    assert_eq!(
        state(|s| s.connects.clone()),
        vec![(b"manual-hidden".to_vec(), b"correct-pass".to_vec())]
    );
    let descriptors = kb::wifi_descriptor();
    assert_eq!(
        descriptors
            .iter()
            .map(|(_, max, kind)| (*max, *kind))
            .collect::<Vec<_>>(),
        vec![(32, 0), (64, 0)]
    );
    assert_eq!(
        descriptors[0].0,
        unsafe { CStr::from_ptr(i18n::get(0, ids::STR_ENTER_WIFI_SSID)) }.to_bytes()
    );
    state(|s| s.connection = 3);
    tick(app, Input::default());
    kb::wifi_paint(app);
    tick(
        app,
        Input {
            pressed: xp_ui::model::CONFIRM,
            ..Input::default()
        },
    );
    assert!(matches!(app.navigation.current(), Some(Target::Legacy(_))));
    assert_eq!(
        state(|s| s.delivered.clone()),
        vec![ResultValue {
            connected: true,
            ssid: b"manual-hidden".to_vec(),
            ip: b"192.0.2.1".to_vec()
        }]
    );
    let events = state(|s| s.events.clone());
    assert!(
        events.iter().position(|e| *e == "sdk_release")
            < events.iter().position(|e| *e == "native_drop")
    );
    assert!(
        events.iter().position(|e| *e == "native_drop")
            < events.iter().position(|e| *e == "result_deliver")
    );
    super::super::dialog_tests::drop_app();
}
#[test]
fn allocation_rejection_keeps_the_native_owner_and_keyboard_launch_failure_cancels_safely() {
    let _serial = super::super::dialog_tests::APP_LOCK.lock().unwrap();
    let app = kb::wifi_app();
    kb::wifi_parent(app);
    let native = kb::wifi_context();
    state(|s| s.fail_owner = true);
    assert!(create(native).is_none());
    assert!(!state(|s| s.events.contains(&"native_drop")));
    kb::wifi_destroy(native);
    let target = install(app);
    state(|s| s.scan = 0);
    tick(app, Input::default());
    kb::wifi_paint(app);
    for shell in [false, true] {
        kb::wifi_fail_keyboard(shell);
        tick(
            app,
            Input {
                released: xp_ui::model::CONFIRM,
                ..Input::default()
            },
        );
        assert!(matches!(app.navigation.current(),Some(Target::Wifi(p))if p==target));
        kb::wifi_paint(app);
    }
    tick(
        app,
        Input {
            pressed: xp_ui::model::BACK,
            ..Input::default()
        },
    );
    assert_eq!(state(|s| s.delivered.clone()).len(), 1);
    assert!(!state(|s| s.delivered[0].connected));
    super::super::dialog_tests::drop_app();
}
#[test]
fn long_touch_is_classified_before_raw_snapshot_and_uses_the_recorded_point() {
    let _serial = super::super::dialog_tests::APP_LOCK.lock().unwrap();
    let app = kb::wifi_app();
    let native = kb::wifi_context();
    let host = NativeHost {
        app,
        object: native,
        adopted: Cell::new(false),
    };
    state(|s| {
        s.input = Input {
            touch_flags: xp_ui::model::TOUCH_HELD,
            ..Input::default()
        };
        s.long = Some((123, 234));
    });
    let (input, long) = host.sample_input(true);
    assert!(long);
    assert_eq!(input.touch_flags, TOUCH_RELEASED);
    assert_eq!((input.touch_x, input.touch_y), (123, 234));
    assert_eq!(state(|s| s.events.clone()), vec!["long", "input"]);
    kb::wifi_destroy(native);
    drop(host);
    super::super::dialog_tests::drop_app();
    unsafe {
        assert!((*APP.0.get()).is_none());
    }
}

#[test]
#[ignore = "strict-Miri negative control: destroyed App Wi-Fi target"]
fn negative_destroyed_app_wifi_target() {
    let _serial = super::super::dialog_tests::APP_LOCK.lock().unwrap();
    let app = kb::wifi_app();
    kb::wifi_parent(app);
    let target = install(app);
    state(|s| {
        s.result = Some(ResultValue {
            connected: false,
            ssid: Vec::new(),
            ip: Vec::new(),
        })
    });
    app.navigation.queue(Transition::Pop);
    app.navigation.drain(app);
    unsafe { target.as_ref() }.host().facts();
}
#[test]
#[ignore = "strict-Miri negative control: native result loan after continuation"]
fn negative_expired_native_wifi_result() {
    unsafe extern "C" fn retain(context: *mut c_void, view: *const abi::XpAppResultView) {
        unsafe {
            context
                .cast::<*const ResultValue>()
                .write((*view).original.cast())
        };
    }
    let _serial = super::super::dialog_tests::APP_LOCK.lock().unwrap();
    let app = kb::wifi_app();
    state(|s| {
        s.result = Some(ResultValue {
            connected: true,
            ssid: b"borrowed".to_vec(),
            ip: Vec::new(),
        })
    });
    let mut saved: *const ResultValue = ptr::null();
    {
        let _guard = app.render_lock();
        with_result(
            ptr::null_mut(),
            retain,
            (&mut saved as *mut *const ResultValue).cast(),
        );
    }
    core::hint::black_box(unsafe { &*saved }.ssid.as_slice());
}
