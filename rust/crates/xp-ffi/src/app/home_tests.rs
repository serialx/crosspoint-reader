#![deny(clippy::type_complexity)]

use super::*;
use core::ptr;
use std::vec::Vec;
use xp_ui::{ffi::SceneView, model::Input, scene::Rect};

#[derive(Default)]
struct State {
    locked: bool,
    model: bool,
    input: Input,
    logs: Vec<u8>,
    paints: usize,
    children: usize,
    delivered: usize,
}
std::thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
fn state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|s| f(&mut s.borrow_mut()))
}
unsafe extern "C" fn noop() {}
unsafe extern "C" fn ignored(_: *mut c_void) {}
unsafe extern "C" fn no(_: *mut c_void) -> u8 {
    0
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    0
}
unsafe extern "C" fn create(_: *const abi::Launch) -> *mut c_void {
    child()
}
fn child() -> *mut c_void {
    Box::into_raw(Box::new(0_u8)).cast()
}
unsafe extern "C" fn destroy(p: *mut c_void) {
    state(|s| {
        assert!(s.locked);
        s.children += 1;
    });
    unsafe { drop(Box::from_raw(p.cast::<u8>())) };
}
unsafe extern "C" fn enter(_: *mut c_void) {
    state(|s| assert!(!s.locked));
}
unsafe extern "C" fn exit(_: *mut c_void) {
    state(|s| assert!(s.locked));
}
unsafe extern "C" fn legacy_render(_: *mut c_void, _: *mut abi::RenderToken) {}
unsafe extern "C" fn result(_: *mut c_void, sink: abi::ResultSink, context: *mut c_void) {
    state(|s| assert!(s.locked));
    unsafe { sink(context, ptr::null()) };
}
unsafe extern "C" fn delivery(_: *mut c_void, _: *const c_void) {
    state(|s| {
        assert!(!s.locked);
        s.delivered += 1;
    });
}
unsafe extern "C" fn shot(_: *const c_void, _: abi::ScreenshotSink, _: *mut c_void) {}
unsafe extern "C" fn facts(_: *const c_void, _: u8, _: u8) -> abi::LegacyFacts {
    unsafe { core::mem::zeroed() }
}
unsafe extern "C" fn lock() -> *mut c_void {
    state(|s| {
        assert!(!s.locked);
        s.locked = true;
    });
    NonNull::<u8>::dangling().as_ptr().cast()
}
unsafe extern "C" fn unlock(_: *mut c_void) {
    state(|s| {
        assert!(s.locked);
        s.locked = false;
    });
}
unsafe extern "C" fn model_lock() {
    state(|s| {
        assert!(!s.model);
        s.model = true;
    });
}
unsafe extern "C" fn model_unlock() {
    state(|s| {
        assert!(s.model);
        s.model = false;
    });
}
unsafe extern "C" fn before(_: u8) -> u8 {
    1
}
unsafe extern "C" fn zero() -> u8 {
    0
}
unsafe extern "C" fn feedback(_: u8) {}
unsafe extern "C" fn diagnostic(kind: u8, _: u32, _: u32) {
    state(|s| {
        if kind == 0 {
            assert!(!s.locked);
        }
        if kind == 1 {
            assert!(s.locked);
        }
        s.logs.push(kind);
    });
}
unsafe extern "C" fn snapshot(_: u8, _: u8, _: u8, out: *mut abi::MappedSnapshot) {
    unsafe {
        out.write(core::mem::zeroed());
        (*out).input = state(|s| s.input);
    }
}
unsafe extern "C" fn geometry() -> abi::DisplayGeometry {
    abi::DisplayGeometry {
        width: 528,
        height: 792,
        orientation: 0,
        frame_available: 0,
    }
}
unsafe extern "C" fn settings() -> abi::HomeSettings {
    abi::HomeSettings {
        grid: 0,
        plugins: 0,
        opds: 0,
        library_metadata: 0,
    }
}
unsafe extern "C" fn recents(_: abi::BookSink, _: *mut c_void) -> u8 {
    1
}
unsafe extern "C" fn theme(out: *mut abi::Theme) {
    unsafe {
        out.write(core::mem::zeroed());
    }
    let t = unsafe { &mut *out };
    t.screen = Rect {
        x: 0,
        y: 0,
        w: 528,
        h: 792,
    };
    t.content = t.screen;
    t.safe = t.screen;
    t.title_font = 12;
    t.small_font = 10;
    t.tiny_font = 8;
    t.body_font = 12;
    t.top_padding = 10;
    t.header_height = 50;
    t.home_top = 60;
    t.tile_height = 250;
    t.cover_height = 180;
    t.side_padding = 20;
    t.spacing = 10;
    t.menu_top = 12;
    t.menu_height = 42;
    t.menu_gap = 6;
    t.hints_height = 40;
    t.scroll_width = 4;
    t.scroll_offset = 6;
    t.columns = 3;
    t.tab_height = 40;
}
unsafe extern "C" fn translate(_: u16) -> abi::Text {
    abi::Text::from_bytes(b"label")
}
unsafe extern "C" fn width(text: abi::Text, _: i32, _: u8) -> i32 {
    text.len as i32 * 6
}
unsafe extern "C" fn line(_: i32) -> i32 {
    20
}
unsafe extern "C" fn text_height(_: abi::Text, _: abi::TextLayout) -> i32 {
    20
}
unsafe extern "C" fn truncate(
    text: abi::Text,
    _: abi::TextLayout,
    sink: abi::TextSink,
    ctx: *mut c_void,
) -> u8 {
    unsafe { sink(ctx, text) }
}
unsafe extern "C" fn region(_: Rect) -> usize {
    0
}
unsafe extern "C" fn paint(scene: *const SceneView, _: *const xp_ui::ffi::Paint) -> u8 {
    state(|s| {
        assert!(s.locked);
        s.paints += 1;
    });
    assert!(unsafe { (*scene).command_count } > 0);
    1
}
unsafe extern "C" fn refresh(_: u8) {}

// Raw tables keep their allocation provenance until every production owner is gone.
struct Fixture;
impl Fixture {
    fn new() -> Self {
        state(|s| *s = State::default());
        let legacy = abi::Legacy {
            create,
            destroy,
            enter,
            r#loop: ignored,
            exit,
            render: legacy_render,
            facts,
            with_result: result,
            deliver_result: delivery,
            prepare_sleep: ignored,
            home_gesture: no,
            forced_refresh: no,
            reader_turn: no_turn,
            screenshot_info: shot,
        };
        let mut resources = test_resources::resources();
        resources.settings_snapshot = settings;
        resources.recents_visit = recents;
        resources.theme_snapshot = theme;
        resources.translate = translate;
        resources.text_width = width;
        resources.line_height = line;
        resources.text_wrap = truncate;
        resources.text_truncate = truncate;
        resources.region_size = region;
        resources.text_box_height = text_height;
        resources.paint_scene = paint;
        resources.refresh = refresh;
        let mut runtime = test_resources::runtime();
        runtime.render_lock = lock;
        runtime.render_unlock = unlock;
        runtime.model_lock = model_lock;
        runtime.model_unlock = model_unlock;
        runtime.ble_before_screen_change = before;
        runtime.header_back_clear = noop;
        runtime.notify_render = noop;
        runtime.input_mapped_snapshot = snapshot;
        runtime.frontlight_present = zero;
        runtime.touch_feedback = feedback;
        runtime.diagnostic = diagnostic;
        runtime.display_geometry = geometry;
        let platform = abi::Platform {
            legacy: Box::into_raw(Box::new(legacy)),
            resources: Box::into_raw(Box::new(resources)),
            runtime: Box::into_raw(Box::new(runtime)),
            epub: Box::into_raw(Box::new(super::keyboard_tests::dialog_leaves())),
        };
        super::dialog_tests::record_tables(&platform);
        let mut compatibility = core::mem::MaybeUninit::uninit();
        assert_eq!(
            unsafe { xp_app_init(&platform, compatibility.as_mut_ptr()) },
            abi::OK
        );
        Self
    }
    fn render_input(&self) {
        {
            let _guard = app().unwrap().render_lock();
            let mut token = abi::RenderToken {
                lock: ptr::null_mut(),
            };
            unsafe { xp_app_render(&mut token) };
        }
        app().unwrap().activity_loop();
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        // The application is removed before the service-table allocations.
        let token = unsafe { lock() };
        unsafe { drop((&mut *APP.0.get()).take()) };
        unsafe { unlock(token) };
        super::dialog_tests::drop_app();
    }
}

#[test]
fn home_bootstrap_publish_enter_render_input() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let app = app().unwrap();
    app.replace(app.new_home(0, false).unwrap());
    assert!(matches!(app.navigation.current(), Some(Target::Home(_))));
    assert!(matches!(
        app.signals.with(|s| s.target),
        Some(Target::Home(_))
    ));
    fixture.render_input();
    assert_eq!(state(|s| s.logs.clone()), [0]);
    assert_eq!(state(|s| s.paints), 1);
}

#[test]
fn home_queued_replace_drain_and_reopen() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let app = app().unwrap();
    app.replace(app.new_home(0, false).unwrap());
    fixture.render_input();
    let Target::Home(first) = app.navigation.current().unwrap() else {
        panic!("Home")
    };
    app.replace(app.new_home(0, false).unwrap());
    assert!(matches!(app.navigation.current(), Some(Target::Home(p)) if p == first));
    assert!(app.navigation.drain(app));
    fixture.render_input();
    assert_eq!(state(|s| s.logs.clone()), [0, 1, 0]);
    app.replace(app.adopt(child()).unwrap());
    assert!(app.navigation.drain(app));
    assert!(matches!(app.navigation.current(), Some(Target::Legacy(_))));
    app.replace(app.new_home(0, false).unwrap());
    assert!(app.navigation.drain(app));
    fixture.render_input();
    assert_eq!(state(|s| s.logs.clone()), [0, 1, 0, 1, 0]);
    assert_eq!(state(|s| s.children), 1);
}

#[test]
fn home_retained_target_parent_growth_pop_and_nested_replace() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let app = app().unwrap();
    app.replace(app.new_home(0, false).unwrap());
    let Target::Home(retained) = app.signals.with(|s| s.target).unwrap() else {
        panic!("Home")
    };
    for _ in 0..24 {
        app.navigation
            .queue(Transition::Push(app.adopt(child()).unwrap()));
        assert!(app.navigation.drain(app));
    }
    assert_eq!(app.navigation.parent_count(), 24);
    // The original published target must survive both owner and Vec moves.
    let _ = unsafe { retained.as_ref() }.input(Input::default());
    for _ in 0..24 {
        app.navigation.queue(Transition::Pop);
        assert!(app.navigation.drain(app));
    }
    assert_eq!(app.navigation.parent_count(), 0);
    assert!(matches!(app.signals.with(|s| s.target), Some(Target::Home(p)) if p == retained));
    fixture.render_input();
    assert_eq!(state(|s| s.children), 24);
    assert_eq!(state(|s| s.delivered), 23);
    for _ in 0..3 {
        app.navigation
            .queue(Transition::Push(app.adopt(child()).unwrap()));
        assert!(app.navigation.drain(app));
    }
    app.replace(app.new_home(0, false).unwrap());
    assert!(app.navigation.drain(app));
    fixture.render_input();
    assert_eq!(app.navigation.parent_count(), 0);
    assert_eq!(state(|s| s.children), 27);
    assert_eq!(state(|s| s.logs.clone()), [0, 1, 0]);
}

#[test]
fn home_superseded_pending_owner_never_enters() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let app = app().unwrap();
    app.replace(app.new_home(0, false).unwrap());
    app.replace(app.new_home(0, false).unwrap());
    app.replace(app.new_home(0, false).unwrap());
    assert_eq!(state(|s| s.logs.clone()), [0]);
    assert!(app.navigation.drain(app));
    fixture.render_input();
    assert_eq!(state(|s| s.logs.clone()), [0, 1, 0]);
}

#[test]
fn home_shell_failure_and_uninstalled_drop_counts() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let probe = super::home_test_allocator::Probe::new(Layout::new::<home::Home>());
    let app = app().unwrap();
    probe.fail_next();
    assert!(app.new_home(0, false).is_none());
    assert_eq!(probe.counts(), (1, 0, 0, 0, 1));
    drop(app.new_home(0, false).unwrap());
    assert_eq!(probe.counts(), (2, 1, 1, 0, 1));
    app.navigation
        .queue(Transition::Replace(app.new_home(0, false).unwrap()));
    app.navigation
        .queue(Transition::Replace(app.new_home(0, false).unwrap()));
    assert_eq!(probe.counts(), (4, 3, 2, 1, 1));
    app.navigation.queue(Transition::None);
    assert_eq!(probe.counts(), (4, 3, 3, 0, 1));
    assert!(state(|s| s.logs.is_empty()));
    drop(fixture);
}

#[test]
fn home_parent_reserve_failure_retry_and_counted_teardown() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let probe = super::home_test_allocator::Probe::new(Layout::new::<home::Home>());
    let app = app().unwrap();
    app.replace(app.new_home(0, false).unwrap());
    let Target::Home(original) = app.navigation.current().unwrap() else {
        panic!("Home")
    };
    let rejected = app.new_home(0, false).unwrap();
    app.navigation.queue(Transition::Push(rejected));
    probe.fail_next();
    assert!(app.navigation.drain(app));
    assert_eq!(app.navigation.parent_count(), 0);
    assert_eq!(state(|s| s.children), 0);
    assert!(matches!(app.signals.with(|s| s.target), Some(Target::Home(p)) if p == original));
    assert_eq!(probe.counts(), (2, 2, 1, 1, 1));
    fixture.render_input();
    for _ in 0..24 {
        app.navigation
            .queue(Transition::Push(app.adopt(child()).unwrap()));
        assert!(app.navigation.drain(app));
    }
    assert_eq!(app.navigation.parent_count(), 24);
    for _ in 0..24 {
        app.navigation.queue(Transition::Pop);
        assert!(app.navigation.drain(app));
    }
    fixture.render_input();
    assert_eq!(probe.counts(), (2, 2, 1, 1, 1));
    app.replace(app.new_home(0, false).unwrap());
    app.replace(app.new_home(0, false).unwrap());
    assert_eq!(probe.counts(), (4, 4, 2, 2, 1));
    assert!(app.navigation.drain(app));
    fixture.render_input();
    assert_eq!(probe.counts(), (4, 4, 3, 1, 1));
    drop(fixture);
    assert_eq!(probe.counts(), (4, 4, 4, 0, 1));
}

#[test]
fn home_result_callback_completes_before_table_teardown() {
    let _serial = super::dialog_tests::APP_LOCK.lock().unwrap();
    let fixture = Fixture::new();
    let probe = super::home_test_allocator::Probe::new(Layout::new::<home::Home>());
    let app = app().unwrap();
    app.replace(app.adopt(child()).unwrap());
    app.navigation
        .queue(Transition::Push(app.new_home(0, false).unwrap()));
    assert!(app.navigation.drain(app));
    fixture.render_input();
    app.navigation.queue(Transition::Pop);
    assert!(app.navigation.drain(app));
    assert_eq!(state(|s| s.delivered), 1);
    assert_eq!(state(|s| s.logs.clone()), [0, 1]);
    assert_eq!(probe.counts(), (1, 1, 1, 0, 0));
    drop(fixture);
    assert_eq!(state(|s| s.children), 1);
}
