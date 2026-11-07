use super::super::{
    abi, dialog_tests, keyboard_tests, test_resources, xp_app_event, xp_app_init, xp_app_render,
    Owner, Target, APP,
};
use super::*;
use core::{alloc::Layout as Allocation, ptr};
use std::{boxed::Box, cell::RefCell, vec::Vec};
use xp_ui::app::Services as _;
use xp_ui::{
    list::Style,
    model::{self, Input},
};

#[derive(Default)]
struct State {
    locked: bool,
    model: bool,
    shell_fail: bool,
    input: Input,
    events: Vec<&'static str>,
    destroyed: usize,
    delivered: Vec<(bool, u8)>,
    paint_failure: bool,
    during_paint: Option<Input>,
}
std::thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
fn state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|s| f(&mut s.borrow_mut()))
}
pub(super) fn allocate_owner(layout: Allocation) -> *mut u8 {
    if state(|s| core::mem::take(&mut s.shell_fail)) {
        return ptr::null_mut();
    }
    unsafe { std::alloc::alloc_zeroed(layout) }
}
struct Context {
    mode: bool,
    result: (bool, u8),
}
fn context(mode: bool) -> *mut c_void {
    Box::into_raw(Box::new(Context {
        mode,
        result: (false, 0),
    }))
    .cast()
}
unsafe extern "C" fn create(_: *const abi::Launch) -> *mut c_void {
    context(false)
}
unsafe extern "C" fn destroy(p: *mut c_void) {
    state(|s| {
        assert!(s.locked);
        s.destroyed += 1;
        s.events.push("destroy");
    });
    unsafe { drop(Box::from_raw(p.cast::<Context>())) };
}
unsafe extern "C" fn enter(_: *mut c_void) {
    state(|s| {
        assert!(!s.locked);
        s.events.push("enter");
    });
}
unsafe extern "C" fn exit(_: *mut c_void) {
    state(|s| {
        assert!(s.locked);
        s.events.push("exit");
    });
}
unsafe extern "C" fn ignored(_: *mut c_void) {}
unsafe extern "C" fn no(_: *mut c_void) -> u8 {
    0
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    0
}
unsafe extern "C" fn old_render(_: *mut c_void, _: *mut abi::RenderToken) {
    panic!("native mode render reached");
}
unsafe extern "C" fn old_loop(_: *mut c_void) {
    panic!("native mode loop reached");
}
unsafe extern "C" fn facts(p: *const c_void, _: u8, _: u8) -> abi::LegacyFacts {
    let kind = if unsafe { &*p.cast::<Context>() }.mode {
        abi::NETWORK_MODE
    } else {
        abi::FILE_TRANSFER
    };
    abi::LegacyFacts {
        kind,
        reader: 0,
        remote_input: 0,
        prevent_sleep: 0,
        exclusive_storage: 0,
        skip_delay: 0,
        input_over_page: 0,
    }
}
unsafe extern "C" fn result(p: *mut c_void, sink: abi::ResultSink, context: *mut c_void) {
    let copied = unsafe { &*p.cast::<Context>() }.result;
    state(|s| {
        assert!(s.locked);
        s.events.push("take");
    });
    let view = abi::XpAppResultView {
        original: (&copied as *const (bool, u8)).cast(),
        value: abi::XpAppResultPayload {
            unsigned_value: u32::from(copied.1),
        },
        kind: 0,
        cancelled: copied.0.into(),
    };
    unsafe { sink(context, &view) };
}
unsafe extern "C" fn deliver(p: *mut c_void, r: *const c_void) {
    assert!(!unsafe { &*p.cast::<Context>() }.mode);
    assert!(matches!(
        app().unwrap().navigation.current(),
        Some(Target::Legacy(_))
    ));
    let value = unsafe { *r.cast::<(bool, u8)>() };
    state(|s| {
        assert!(!s.locked);
        assert_eq!(s.events.last(), Some(&"unlock"));
        s.delivered.push(value);
        s.events.push("deliver");
    });
}
unsafe extern "C" fn shot(_: *const c_void, sink: abi::ScreenshotSink, c: *mut c_void) {
    unsafe { sink(c, &core::mem::zeroed()) };
}
unsafe extern "C" fn unused_definition(
    _: *mut c_void,
    _: abi::DefinitionPop,
    _: abi::DefinitionSelectionSink,
    _: *mut c_void,
) {
    panic!("definition");
}
unsafe extern "C" fn unused_selection(_: *mut c_void, _: abi::DefinitionSelection) -> u8 {
    panic!("selection");
}
unsafe extern "C" fn lock() -> *mut c_void {
    state(|s| {
        assert!(!s.locked);
        s.locked = true;
        s.events.push("lock");
    });
    NonNull::<u8>::dangling().as_ptr().cast()
}
unsafe extern "C" fn unlock(_: *mut c_void) {
    state(|s| {
        assert!(s.locked);
        s.locked = false;
        s.events.push("unlock");
    });
}
unsafe extern "C" fn model_lock() {
    state(|s| assert!(!core::mem::replace(&mut s.model, true)));
}
unsafe extern "C" fn model_unlock() {
    state(|s| assert!(core::mem::replace(&mut s.model, false)));
}
unsafe extern "C" fn nothing() {}
unsafe extern "C" fn before(_: u8) -> u8 {
    1
}
unsafe extern "C" fn frontlight() -> u8 {
    0
}
unsafe extern "C" fn diagnostic(_: u8, _: u32, _: u32) {}
unsafe extern "C" fn haptic(_: u8) {
    state(|s| s.events.push("haptic"));
}
unsafe extern "C" fn snapshot(_: u8, full: u8, _: u8, out: *mut abi::MappedSnapshot) {
    assert_eq!(full, 1);
    let input = state(|s| s.input);
    unsafe {
        out.write(abi::MappedSnapshot {
            input,
            ..core::mem::zeroed()
        })
    };
}
unsafe extern "C" fn paint(scene: *const SceneView, physical: *const Paint) -> u8 {
    state(|s| {
        assert!(s.locked);
        assert!(!s.model);
        s.events.push("paint");
    });
    let scene = unsafe { &*scene };
    let physical = unsafe { &*physical };
    for command in
        unsafe { core::slice::from_raw_parts(scene.commands, usize::from(scene.command_count)) }
    {
        if command.resource != 0 {
            assert_eq!(
                unsafe {
                    physical.resource_valid.unwrap()(
                        physical.context,
                        command.resource,
                        command.generation,
                    )
                },
                1
            );
            unsafe {
                physical.resource.unwrap()(physical.context, ptr::null(), command, command.rect)
            };
        }
    }
    if let Some(input) = state(|s| s.during_paint.take()) {
        state(|s| s.input = input);
        app().unwrap().activity_loop();
    }
    state(|s| u8::from(!s.paint_failure))
}
unsafe extern "C" fn refresh(_: u8) {
    state(|s| {
        assert!(!s.model);
        s.events.push("refresh");
    });
}
unsafe extern "C" fn layout(_: *mut c_void, out: *mut Layout) {
    let screen = Rect {
        x: 0,
        y: 0,
        w: 528,
        h: 792,
    };
    unsafe {
        out.write(Layout {
            list: Style {
                screen,
                area: screen,
                row_height: 48,
                padding: 8,
                scroll_width: 4,
                ..Style::default()
            },
            top_padding: 12,
            header_height: 36,
            hints_height: 40,
            spacing: 8,
            ..Layout::default()
        })
    };
}
unsafe extern "C" fn row(_: *mut c_void, i: u16, out: *mut DataRow) -> u8 {
    if i >= 3 {
        return 0;
    }
    unsafe {
        out.write(DataRow {
            label: c"Mode".as_ptr(),
            subtitle: c"Description".as_ptr(),
            icon: u32::from(i) + 1,
            ..DataRow::default()
        })
    };
    1
}
unsafe extern "C" fn width(_: *mut c_void, _: *const core::ffi::c_char, _: i32, _: u8) -> i32 {
    60
}
unsafe extern "C" fn height(
    _: *mut c_void,
    _: *const core::ffi::c_char,
    _: i32,
    _: u8,
    _: u8,
    _: i16,
) -> i32 {
    24
}
unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
    24
}
unsafe extern "C" fn resources(p: *mut c_void) -> Services {
    Services {
        context: p,
        row: Some(row),
        width: Some(width),
        height: Some(height),
        line: Some(line),
        count: 3,
        reserved: 0,
    }
}
unsafe extern "C" fn draw(_: *mut c_void, _: *const Command, _: Rect) {
    state(|s| assert!(!s.model));
}
unsafe extern "C" fn scroll(_: i32, _: u16) {}
unsafe extern "C" fn scene_error(_: u8) {
    state(|s| s.events.push("scene-error"));
}
unsafe extern "C" fn prepare(p: *mut c_void, cancelled: u8, mode: u8) {
    state(|s| {
        assert!(!s.model);
        s.events.push("result");
    });
    unsafe { (*p.cast::<Context>()).result = (cancelled != 0, mode) };
}
#[no_mangle]
static XP_NETWORK_MODES_PHYSICAL: Physical = Physical {
    layout,
    resources,
    paint: draw,
    scroll_diagnostic: scroll,
    scene_diagnostic: scene_error,
    prepare_result_retire_n8: prepare,
};
fn setup() -> &'static App {
    state(|s| *s = State::default());
    let legacy = abi::Legacy {
        create,
        destroy,
        enter,
        r#loop: old_loop,
        exit,
        render: old_render,
        facts,
        with_result: result,
        deliver_result: deliver,
        prepare_sleep: ignored,
        home_gesture: no,
        forced_refresh: no,
        reader_turn: no_turn,
        screenshot_info: shot,
        with_definition_selection_result: unused_definition,
        deliver_definition_selection_result: unused_selection,
        definition_selection_install_failed: ignored,
    };
    let mut physical = test_resources::resources();
    physical.paint_scene = paint;
    physical.refresh = refresh;
    let mut runtime = test_resources::runtime();
    runtime.render_lock = lock;
    runtime.render_unlock = unlock;
    runtime.model_lock = model_lock;
    runtime.model_unlock = model_unlock;
    runtime.input_mapped_snapshot = snapshot;
    runtime.ble_before_screen_change = before;
    runtime.header_back_clear = nothing;
    runtime.notify_render = nothing;
    runtime.frontlight_present = frontlight;
    runtime.touch_feedback = haptic;
    runtime.diagnostic = diagnostic;
    let platform = abi::Platform {
        legacy: Box::into_raw(Box::new(legacy)),
        resources: Box::into_raw(Box::new(physical)),
        runtime: Box::into_raw(Box::new(runtime)),
        epub: Box::into_raw(Box::new(keyboard_tests::dialog_leaves())),
    };
    dialog_tests::record_tables(&platform);
    let mut compatibility = core::mem::MaybeUninit::uninit();
    assert_eq!(
        unsafe { xp_app_init(&platform, compatibility.as_mut_ptr()) },
        abi::OK
    );
    app().unwrap()
}
fn adopt(app: &App, mode: bool, push: bool) -> *mut c_void {
    let p = context(mode);
    let event = abi::Event {
        kind: if push {
            abi::ADOPT_PUSH
        } else {
            abi::ADOPT_REPLACE
        },
        value: abi::EventValue { adopt_push: p },
    };
    assert_eq!(unsafe { xp_app_event(&event) }, abi::ACCEPTED);
    app.navigation.drain(app);
    p
}
fn render(app: &App) {
    let guard = app.render_lock();
    let mut token = abi::RenderToken { lock: guard.token };
    unsafe { xp_app_render(&mut token) };
    drop(guard);
}
fn close() {
    let guard = app().unwrap().render_lock();
    // Application drop destroys any retained native owners; fixtures model the
    // same exclusion that Home/replace supplies before dropping these targets.
    unsafe { drop((&mut *APP.0.get()).take()) };
    drop(guard);
    // drop_app now removes the four fixture tables, with no current owner left.
    dialog_tests::drop_app();
}
#[test]
fn mode_real_app_results_render_rejection_and_drop_order() {
    let _serial = dialog_tests::APP_LOCK.lock().unwrap();
    let app = setup();
    adopt(app, false, false);
    for choice in 0..3u8 {
        adopt(app, true, true);
        assert!(matches!(
            app.navigation.current(),
            Some(Target::NetworkModes(_))
        ));
        render(app);
        for _ in 0..choice {
            state(|s| {
                s.input = Input {
                    pressed: model::NAV_NEXT,
                    held: model::NAV_NEXT,
                    now_ms: 100,
                    ..Input::default()
                }
            });
            app.activity_loop();
        }
        state(|s| {
            s.input = Input {
                released: model::CONFIRM,
                ..Input::default()
            }
        });
        app.activity_loop();
        app.navigation.drain(app);
        state(|s| assert_eq!(s.delivered.last(), Some(&(false, choice))));
    }
    adopt(app, true, true);
    state(|s| s.paint_failure = true);
    render(app);
    state(|s| {
        s.input = Input {
            released: model::CONFIRM,
            ..Input::default()
        }
    });
    app.activity_loop();
    assert_eq!(
        app.navigation.pending_kind(),
        xp_ui::app::TransitionKind::None
    );
    state(|s| {
        s.input = Input {
            released: model::BACK,
            ..Input::default()
        }
    });
    app.activity_loop();
    app.navigation.drain(app);
    state(|s| {
        assert_eq!(s.delivered.last(), Some(&(true, 0)));
        assert_eq!(s.destroyed, 4);
    });
    close();
}
#[test]
fn mode_real_app_adoption_failure_keeps_wrapper_and_pending_replace_drops_once() {
    let _serial = dialog_tests::APP_LOCK.lock().unwrap();
    let app = setup();
    adopt(app, false, false);
    let p = context(true);
    state(|s| s.shell_fail = true);
    let event = abi::Event {
        kind: abi::ADOPT_PUSH,
        value: abi::EventValue { adopt_push: p },
    };
    assert_eq!(unsafe { xp_app_event(&event) }, abi::REJECTED);
    state(|s| assert_eq!(s.destroyed, 0));
    unsafe { drop(Box::from_raw(p.cast::<Context>())) };
    let accepted = app.adopt(context(true)).unwrap();
    assert!(matches!(accepted, Owner::NetworkModes(_)));
    app.navigation.queue(Transition::Push(accepted));
    let guard = app.render_lock();
    app.navigation.queue(Transition::None);
    drop(guard);
    state(|s| assert_eq!(s.destroyed, 1));
    close();
}
