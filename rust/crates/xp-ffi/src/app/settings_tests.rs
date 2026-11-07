use super::*;
use core::{alloc::Layout, ffi::CStr, ptr};
use std::{collections::BTreeMap, vec::Vec};
use xp_ui::{
    ffi::{Paint, SceneView},
    model::{self, Input},
    scene::{Command, Rect},
    settings::view,
};
#[derive(Default)]
struct State {
    locked: bool,
    model: bool,
    fail_child: bool,
    fail_shell: bool,
    fail_arena: bool,
    changed: bool,
    reloads: usize,
    destroyed: usize,
    loan: bool,
    returning: bool,
    input: Input,
    sections: Vec<u8>,
    labels: Vec<Vec<u8>>,
    logs: Vec<Vec<u8>>,
    live: BTreeMap<usize, (*mut u8, usize)>,
}
std::thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
fn state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|s| f(&mut s.borrow_mut()))
}
pub(super) fn allocate_shell(layout: Layout) -> *mut u8 {
    if state(|s| core::mem::take(&mut s.fail_shell)) {
        return ptr::null_mut();
    }
    unsafe { std::alloc::alloc_zeroed(layout) }
}
unsafe extern "C" fn create(launch: *const abi::Launch) -> *mut c_void {
    let launch = unsafe { &*launch };
    assert_eq!(launch.kind, abi::SETTINGS_SECTION);
    state(|s| s.sections.push(unsafe { launch.value.settings_section }));
    if state(|s| s.fail_child) {
        state(|s| s.logs.push(b"OOM: SettingsActivity".to_vec()));
        return ptr::null_mut();
    }
    Box::into_raw(Box::new(0u8)).cast()
}
unsafe extern "C" fn destroy(p: *mut c_void) {
    state(|s| {
        assert!(s.locked || !s.returning);
        assert!(!s.loan);
        s.destroyed += 1;
    });
    unsafe { drop(Box::from_raw(p.cast::<u8>())) };
}
unsafe extern "C" fn nothing() {}
unsafe extern "C" fn ignored(_: *mut c_void) {}
unsafe extern "C" fn no(_: *mut c_void) -> u8 {
    0
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    0
}
unsafe extern "C" fn shot(_: *const c_void, _: abi::ScreenshotSink, _: *mut c_void) {}
unsafe extern "C" fn render(_: *mut c_void, _: *mut abi::RenderToken) {}
unsafe extern "C" fn result(_: *mut c_void, sink: abi::ResultSink, context: *mut c_void) {
    // The default native result contains no payload. No object borrow reaches sink.
    state(|s| s.returning = true);
    unsafe { sink(context, ptr::null()) };
}
unsafe extern "C" fn deliver(_: *mut c_void, _: *const c_void) {
    panic!("section result must be owned")
}
unsafe extern "C" fn selection(
    _: *mut c_void,
    _: abi::DefinitionPop,
    _: abi::DefinitionSelectionSink,
    _: *mut c_void,
) {
    panic!("definition")
}
unsafe extern "C" fn accept(_: *mut c_void, _: abi::DefinitionSelection) -> u8 {
    panic!("definition")
}
unsafe extern "C" fn facts(_: *const c_void, _: u8, _: u8) -> abi::LegacyFacts {
    settings::facts()
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
unsafe extern "C" fn yes(_: u8) -> u8 {
    1
}
unsafe extern "C" fn zero() -> u8 {
    0
}
unsafe extern "C" fn refresh(_: u8) {}
unsafe extern "C" fn snapshot(_: u8, full: u8, _: u8, out: *mut abi::MappedSnapshot) {
    assert_eq!(full, 1);
    let mut value: abi::MappedSnapshot = unsafe { core::mem::zeroed() };
    value.input = state(|s| s.input);
    unsafe { out.write(value) };
}
unsafe extern "C" fn translate(_: u16) -> abi::Text {
    if state(|s| s.returning) {
        state(|s| {
            assert!(!s.locked);
            assert!(s.destroyed > 0);
        });
        assert!(matches!(
            app().unwrap().navigation.current(),
            Some(Target::Settings(_))
        ));
        assert!(app()
            .unwrap()
            .signals
            .with(|s| matches!(s.target, Some(Target::Settings(_)))));
    }
    abi::Text::from_terminated(if state(|s| s.changed) {
        b"Changed\0"
    } else {
        b"Setting\0"
    })
}
unsafe extern "C" fn allocate(_: u8, size: usize) -> *mut u8 {
    if state(|s| core::mem::take(&mut s.fail_arena)) {
        return ptr::null_mut();
    }
    let p = unsafe { std::alloc::alloc_zeroed(Layout::from_size_align(size, 8).unwrap()) };
    state(|s| {
        s.live.insert(p.addr(), (p, size));
    });
    p
}
unsafe extern "C" fn release(_: u8, p: *mut u8) {
    let (_, size) = state(|s| s.live.remove(&p.addr()).unwrap());
    unsafe { std::alloc::dealloc(p, Layout::from_size_align(size, 8).unwrap()) };
}
unsafe extern "C" fn width(text: abi::Text, _: i32, _: u8) -> i32 {
    text.len as i32 * 8
}
unsafe extern "C" fn height(_: abi::Text, _: abi::TextLayout) -> i32 {
    20
}
unsafe extern "C" fn line(_: i32) -> i32 {
    20
}
unsafe extern "C" fn text(_: *const Command, _: Rect, value: abi::Text) {
    state(|s| {
        assert!(s.loan);
        s.labels.push(unsafe { value.bytes().unwrap() }.to_vec());
    });
}
unsafe extern "C" fn footer(_: *const abi::Text) {}
unsafe extern "C" fn paint(scene: *const SceneView, resources: *const Paint) -> u8 {
    let (scene, resources) = unsafe { (&*scene, &*resources) };
    state(|s| {
        assert!(s.locked);
        s.loan = true;
        s.labels.clear();
    });
    for command in
        unsafe { core::slice::from_raw_parts(scene.commands, scene.command_count.into()) }
    {
        if command.resource != 0 {
            assert_eq!(
                unsafe {
                    resources.resource_valid.unwrap()(
                        resources.context,
                        command.resource,
                        command.generation,
                    )
                },
                1
            );
            unsafe {
                resources.resource.unwrap()(resources.context, ptr::null(), command, command.rect)
            };
        }
    }
    state(|s| s.loan = false);
    1
}
unsafe extern "C" fn log(_: u8, _: abi::Text, message: abi::Text) {
    state(|s| s.logs.push(unsafe { message.bytes().unwrap() }.to_vec()));
}
#[no_mangle]
unsafe extern "C" fn xp_settings_shell_snapshot(out: *mut view::Layout) {
    let mut layout = view::Layout::default();
    layout.list.screen = Rect {
        x: 0,
        y: 0,
        w: 528,
        h: 792,
    };
    layout.list.area = layout.list.screen;
    layout.safe = layout.list.screen;
    layout.list.row_height = 48;
    layout.header_height = 40;
    layout.hints_height = 40;
    unsafe { out.write(layout) };
}
#[no_mangle]
unsafe extern "C" fn xp_settings_shell_paint(
    _: *const Command,
    _: Rect,
    title: *const core::ffi::c_char,
    icon: u8,
) {
    assert!(icon <= 6);
    assert!(!title.is_null());
    let _ = unsafe { CStr::from_ptr(title) };
}
#[no_mangle]
extern "C" fn xp_settings_shell_theme_reload() {
    state(|s| s.reloads += 1);
}
fn build_app() -> &'static App {
    let legacy = abi::Legacy {
        create,
        destroy,
        enter: ignored,
        r#loop: ignored,
        exit: ignored,
        render,
        facts,
        with_result: result,
        deliver_result: deliver,
        prepare_sleep: ignored,
        home_gesture: no,
        forced_refresh: no,
        reader_turn: no_turn,
        screenshot_info: shot,
        with_definition_selection_result: selection,
        deliver_definition_selection_result: accept,
        definition_selection_install_failed: ignored,
    };
    let mut resources = test_resources::resources();
    resources.buffer_allocate = allocate;
    resources.buffer_release = release;
    resources.paint_scene = paint;
    resources.refresh = refresh;
    resources.text_draw = text;
    resources.text_box_height = height;
    resources.translate = translate;
    resources.text_width = width;
    resources.line_height = line;
    resources.footer_draw = footer;
    let mut runtime = test_resources::runtime();
    runtime.render_lock = lock;
    runtime.render_unlock = unlock;
    runtime.model_lock = model_lock;
    runtime.model_unlock = model_unlock;
    runtime.ble_before_screen_change = yes;
    runtime.header_back_clear = nothing;
    runtime.notify_render = nothing;
    runtime.frontlight_present = zero;
    runtime.touch_feedback = refresh;
    runtime.input_mapped_snapshot = snapshot;
    let mut epub = keyboard_tests::dialog_leaves();
    epub.log = log;
    let platform = abi::Platform {
        legacy: Box::into_raw(Box::new(legacy)),
        resources: Box::into_raw(Box::new(resources)),
        runtime: Box::into_raw(Box::new(runtime)),
        epub: Box::into_raw(Box::new(epub)),
    };
    dialog_tests::record_tables(&platform);
    let mut out = core::mem::MaybeUninit::uninit();
    assert_eq!(unsafe { xp_app_init(&platform, out.as_mut_ptr()) }, abi::OK);
    app().unwrap()
}
fn paint_root(app: &App) {
    let _lock = app.render_lock();
    let mut token = abi::RenderToken {
        lock: ptr::null_mut(),
    };
    unsafe { xp_app_render(&mut token) };
}
#[test]
fn settings_real_app_all_sections_pending_return_and_teardown() {
    let _serial = dialog_tests::APP_LOCK.lock().unwrap();
    for section in 0..=6 {
        let app = build_app();
        app.replace(settings::create(app, section).unwrap());
        let Some(Target::Settings(root)) = app.navigation.current() else {
            panic!("root")
        };
        assert!(state(|s| s.sections.is_empty()));
        assert_eq!(unsafe { super::facts() }.current_kind, abi::SETTINGS);
        assert!(!app.navigation.has_reader());
        paint_root(app);
        app.activity_loop();
        if section < 6 {
            assert_eq!(state(|s| s.sections.clone()), [section]);
            app.navigation.drain(app);
            assert!(matches!(
                app.navigation.current(),
                Some(Target::SettingsSection(_))
            ));
            assert_eq!(app.navigation.parent_count(), 1);
            state(|s| s.changed = true);
            app.navigation.queue(Transition::Pop);
            app.navigation.drain(app);
            assert!(matches!(app.navigation.current(),Some(Target::Settings(p)) if p==root));
            assert!(!unsafe { root.as_ref() }.ready());
            assert_eq!(unsafe { root.as_ref() }.selected(), i32::from(section));
            state(|s| s.returning = false);
            paint_root(app);
            assert!(state(|s| s.labels.iter().all(|v| v == b"Changed")));
            state(|s| s.returning = false);
        } else {
            assert!(state(|s| s.sections.is_empty()));
        }
        // Replace drives exit under RenderLock, releases the arena and drops the stable owner.
        let other = app.adopt(Box::into_raw(Box::new(0u8)).cast()).unwrap();
        app.replace(other);
        app.navigation.drain(app);
        assert_eq!(state(|s| s.reloads), 1);
        assert!(state(|s| s.live.is_empty()));
        dialog_tests::drop_app();
        state(|s| *s = State::default());
    }
}
#[test]
fn settings_real_app_failed_child_retains_selected_and_owner_failure_is_atomic() {
    let _serial = dialog_tests::APP_LOCK.lock().unwrap();
    let app = build_app();
    state(|s| s.fail_shell = true);
    assert!(settings::create(app, 6).is_none());
    assert!(app.navigation.current().is_none());
    app.replace(settings::create(app, 5).unwrap());
    state(|s| s.fail_child = true);
    app.activity_loop();
    let Some(Target::Settings(root)) = app.navigation.current() else {
        panic!("root")
    };
    assert_eq!(unsafe { root.as_ref() }.selected(), 5);
    assert_eq!(app.navigation.pending_kind(), TransitionKind::None);
    assert!(state(|s| s.logs.last().unwrap() == b"OOM: SettingsActivity"));
    state(|s| s.fail_child = false);
    paint_root(app);
    state(|s| {
        s.input = Input {
            released: model::CONFIRM,
            ..Input::default()
        }
    });
    app.activity_loop();
    app.navigation.drain(app);
    assert!(matches!(
        app.navigation.current(),
        Some(Target::SettingsSection(_))
    ));
    app.navigation.queue(Transition::Pop);
    app.navigation.drain(app);
    state(|s| s.returning = false);
    app.replace(app.adopt(Box::into_raw(Box::new(0u8)).cast()).unwrap());
    app.navigation.drain(app);
    dialog_tests::drop_app();
    assert!(state(|s| s.live.is_empty()));
    state(|s| *s = State::default());
}
