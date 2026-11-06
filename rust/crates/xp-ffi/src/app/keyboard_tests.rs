use super::*;
use core::alloc::Layout as AllocLayout;
use std::{
    alloc::{alloc_zeroed, dealloc},
    collections::BTreeMap,
    vec::Vec,
};
use xp_ui::{
    ffi::{Paint, SceneView},
    keyboard::{Layouts, Row, WireKey},
    keyboard_view::{Layout, Metrics, PaintKey},
    model::{self, Input},
    scene::{Command, Rect},
};
#[path = "keyboard_test_leaves.rs"]
mod leaves;

#[derive(Default)]
struct State {
    locked: bool,
    model: bool,
    input: Input,
    live: BTreeMap<usize, (*mut u8, usize)>,
    fail_after: Option<usize>,
    shell_fail: bool,
    entered: usize,
    destroyed: usize,
    closed: usize,
    parent_drops: usize,
    releases: usize,
    paints: usize,
    expected: Vec<u8>,
    edit_pointer: *const u8,
    delivered: usize,
    cancelled: bool,
    paint_failure: bool,
    errors: Vec<(u8, Vec<u8>, Vec<u8>)>,
}
std::thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
fn state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|s| f(&mut s.borrow_mut()))
}
pub(super) fn allocate_shell(layout: AllocLayout) -> *mut u8 {
    if state(|s| core::mem::take(&mut s.shell_fail)) {
        return ptr::null_mut();
    }
    unsafe { alloc_zeroed(layout) }
}
use core::ptr;
struct Context {
    initial: Vec<u8>,
    title: Vec<u8>,
    keyboard: bool,
    entered: bool,
    text_locked: Cell<bool>,
}
fn context(initial: &[u8], keyboard: bool) -> *mut c_void {
    Box::into_raw(Box::new(Context {
        initial: initial.to_vec(),
        title: b"Title".to_vec(),
        keyboard,
        entered: false,
        text_locked: Cell::new(false),
    }))
    .cast()
}
unsafe fn ctx<'a>(p: *mut c_void) -> &'a mut Context {
    unsafe { &mut *p.cast::<Context>() }
}
unsafe extern "C" fn destroy(p: *mut c_void) {
    let c = unsafe { Box::from_raw(p.cast::<Context>()) };
    if c.keyboard {
        state(|s| {
            if c.entered {
                assert!(s.locked);
                s.closed += 1;
            }
            s.destroyed += 1;
        });
    } else {
        state(|s| s.parent_drops += 1);
    }
}
unsafe extern "C" fn enter(p: *mut c_void) {
    state(|s| assert!(!s.locked));
    if unsafe { ctx(p).keyboard } {
        unsafe { ctx(p).entered = true };
        state(|s| s.entered += 1);
    }
}
unsafe extern "C" fn exit(_: *mut c_void) {
    state(|s| assert!(s.locked));
}
unsafe extern "C" fn nothing() {}
unsafe extern "C" fn ignored(_: *mut c_void) {}
unsafe extern "C" fn no(_: *mut c_void) -> u8 {
    0
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    0
}
unsafe extern "C" fn create(_: *const abi::Launch) -> *mut c_void {
    panic!("factory")
}
unsafe extern "C" fn old_render(_: *mut c_void, _: *mut abi::RenderToken) {
    panic!("legacy render")
}
unsafe extern "C" fn old_result(_: *mut c_void, _: abi::ResultSink, _: *mut c_void) {
    panic!("legacy result")
}
unsafe extern "C" fn delivery(_: *mut c_void, _: *const c_void) {
    panic!("native projection stub")
}
unsafe extern "C" fn shot(_: *const c_void, _: abi::ScreenshotSink, _: *mut c_void) {
    panic!("shot")
}
unsafe extern "C" fn facts(p: *const c_void, _: u8, _: u8) -> abi::LegacyFacts {
    abi::LegacyFacts {
        kind: 255,
        reader: 0,
        remote_input: 0,
        prevent_sleep: u8::from(unsafe { ctx(p.cast_mut()).keyboard }),
        exclusive_storage: 0,
        skip_delay: 0,
        input_over_page: 0,
    }
}
unsafe extern "C" fn render_lock() -> *mut c_void {
    state(|s| {
        assert!(!s.locked);
        s.locked = true;
    });
    NonNull::<u8>::dangling().as_ptr().cast()
}
unsafe extern "C" fn render_unlock(_: *mut c_void) {
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
unsafe extern "C" fn frontlight() -> u8 {
    0
}
unsafe extern "C" fn snapshot(poll: u8, full: u8, _: u8, out: *mut abi::MappedSnapshot) {
    assert_eq!(poll, 0);
    assert_eq!(full, 1);
    unsafe {
        out.write(core::mem::zeroed());
        (*out).input = state(|s| s.input);
    }
}
unsafe extern "C" fn allocate(_: u8, bytes: usize) -> *mut u8 {
    if state(|s| {
        assert!(!s.model);
        match &mut s.fail_after {
            Some(0) => true,
            Some(n) => {
                *n -= 1;
                false
            }
            None => false,
        }
    }) {
        return ptr::null_mut();
    }
    let p = unsafe { alloc_zeroed(AllocLayout::from_size_align(bytes, 8).unwrap()) };
    state(|s| {
        s.live.insert(p.addr(), (p, bytes));
    });
    p
}
unsafe extern "C" fn release(_: u8, p: *mut u8) {
    let (_, bytes) = state(|s| {
        assert!(!s.model);
        s.live.remove(&p.addr()).unwrap()
    });
    unsafe {
        dealloc(p, AllocLayout::from_size_align(bytes, 8).unwrap());
    }
}
unsafe extern "C" fn paint(scene: *const SceneView, p: *const Paint) -> u8 {
    if state(|s| s.paint_failure) {
        return 0;
    }
    let (scene, p) = unsafe { (&*scene, &*p) };
    for command in
        unsafe { core::slice::from_raw_parts(scene.commands, scene.command_count as usize) }
    {
        if command.resource != 0 {
            assert_eq!(
                unsafe {
                    (p.resource_valid.unwrap())(p.context, command.resource, command.generation)
                },
                1
            );
            unsafe {
                (p.resource.unwrap())(
                    p.context,
                    ptr::null(),
                    command,
                    Rect {
                        x: 0,
                        y: 0,
                        w: 528,
                        h: 792,
                    },
                )
            };
        }
    }
    state(|s| s.paints += 1);
    1
}
unsafe extern "C" fn refresh(_: u8) {}
unsafe extern "C" fn text(_: *const Command, _: Rect, t: abi::Text) {
    assert_eq!(unsafe { *t.data.add(t.len) }, 0);
}
pub(super) unsafe extern "C" fn log(level: u8, origin: abi::Text, t: abi::Text) {
    let text = unsafe { t.bytes() }.unwrap();
    assert!(!text.is_empty());
    state(|s| {
        s.errors.push((
            level,
            unsafe { origin.bytes() }.unwrap().to_vec(),
            text.to_vec(),
        ))
    });
}
unsafe extern "C" fn feedback(_: u8) {}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_launch(p: *mut c_void, out: *mut keyboard::Launch) {
    let c = unsafe { ctx(p) };
    unsafe {
        out.write(keyboard::Launch {
            title: abi::Text::from_bytes(&c.title),
            initial: abi::Text::from_bytes(&c.initial),
            maximum: 0,
            input_type: 0,
        });
    }
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_release_launch(p: *mut c_void) {
    unsafe {
        ctx(p).initial.clear();
        ctx(p).title.clear();
    }
    state(|s| s.releases += 1);
}
unsafe extern "C" fn row(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, r: u8) -> Row {
    Row {
        count: u8::from(r == 0),
        ..Row::default()
    }
}
unsafe extern "C" fn key(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, _: u8, _: u8) -> WireKey {
    WireKey {
        label: c"OK".as_ptr(),
        output: ptr::null(),
        alternate: ptr::null(),
        value: 13,
        kind: 1,
        units: 2,
    }
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_layouts() -> Layouts {
    Layouts {
        context: ptr::null_mut(),
        row: Some(row),
        key: Some(key),
    }
}
unsafe extern "C" fn measure(_: *mut c_void, _: i32, _: *const u8, n: u32, mode: u8) -> i32 {
    if mode == 2 {
        0
    } else {
        n as i32 * 8
    }
}
unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
    20
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_metrics(_: *mut c_void) -> Metrics {
    Metrics {
        context: ptr::null_mut(),
        measure: Some(measure),
        line_height: Some(line),
    }
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_snapshot(
    _: *mut c_void,
    out: *mut Layout,
    configured: *mut u16,
    language: *mut u8,
) {
    if !out.is_null() {
        unsafe {
            out.write(Layout {
                screen: Rect {
                    x: 0,
                    y: 0,
                    w: 528,
                    h: 792,
                },
                content: Rect {
                    x: 0,
                    y: 0,
                    w: 528,
                    h: 792,
                },
                font: 0,
                small_font: 0,
                top_padding: 0,
                header_height: 40,
                spacing: 8,
                vertical_offset: 0,
                key_spacing: 6,
                key_height: 56,
                hints_height: 40,
                side_width: 0,
                text_percent: 80,
                field_normal: 1,
                field_cursor: 2,
                field_end: 0,
                centered: 0,
                touch: 1,
                x3: 0,
                language: 0,
                strings: [0; 18],
            });
        }
    }
    if !configured.is_null() {
        unsafe { configured.write(1) }
    }
    if !language.is_null() {
        unsafe { language.write(0) }
    }
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_text_lock(p: *mut c_void, acquire: u8) {
    let c = unsafe { ctx(p) };
    if acquire != 0 {
        state(|s| assert!(!s.model));
        assert!(!c.text_locked.replace(true));
    } else {
        assert!(c.text_locked.replace(false));
    }
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_draw_key(
    _: *mut c_void,
    _: *const Command,
    _: *const PaintKey,
    _: u8,
    _: u8,
    _: u8,
) {
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_chrome(_: *mut c_void, _: *const Command, _: Rect, t: abi::Text) {
    assert_eq!(unsafe { *t.data.add(t.len) }, 0);
}
#[no_mangle]
unsafe extern "C" fn xp_keyboard_deliver(
    _: *const abi::Legacy,
    parent: *mut c_void,
    text: abi::Text,
    cancel: u8,
) {
    assert!(
        matches!(app().unwrap().navigation.current(),Some(Target::Legacy(p)) if p.as_ptr()==parent)
    );
    state(|s| {
        assert!(!s.locked);
        assert_eq!(s.entered, s.closed);
        s.delivered += 1;
        s.cancelled = cancel != 0;
        if cancel == 0 {
            assert_eq!(unsafe { text.bytes() }.unwrap(), s.expected);
            assert_eq!(text.data, s.edit_pointer);
            assert!(s.live.contains_key(&text.data.addr()));
        }
    });
}
fn build_app() -> &'static App {
    let legacy = Box::leak(Box::new(abi::Legacy {
        create,
        destroy,
        enter,
        r#loop: ignored,
        exit,
        render: old_render,
        facts,
        with_result: old_result,
        deliver_result: delivery,
        prepare_sleep: ignored,
        home_gesture: no,
        forced_refresh: no,
        reader_turn: no_turn,
        screenshot_info: shot,
    }));
    let mut resources = test_resources::resources();
    resources.buffer_allocate = allocate;
    resources.buffer_release = release;
    resources.paint_scene = paint;
    resources.refresh = refresh;
    resources.text_draw = text;
    let mut runtime = test_resources::runtime();
    runtime.render_lock = render_lock;
    runtime.render_unlock = render_unlock;
    runtime.model_lock = model_lock;
    runtime.model_unlock = model_unlock;
    runtime.ble_before_screen_change = before;
    runtime.header_back_clear = nothing;
    runtime.notify_render = nothing;
    runtime.input_mapped_snapshot = snapshot;
    runtime.frontlight_present = frontlight;
    runtime.touch_feedback = feedback;
    let platform = abi::Platform {
        legacy,
        resources: Box::leak(Box::new(resources)),
        runtime: Box::leak(Box::new(runtime)),
        epub: Box::leak(Box::new(leaves::leaves())),
    };
    let mut compatibility = core::mem::MaybeUninit::uninit();
    assert_eq!(
        unsafe { xp_app_init(&platform, compatibility.as_mut_ptr()) },
        abi::OK
    );
    app().unwrap()
}
fn paint_current(app: &App) {
    let _guard = app.render_lock();
    let mut token = abi::RenderToken {
        lock: ptr::null_mut(),
    };
    unsafe { xp_app_render(&mut token) };
}
fn send_input(app: &App, input: Input) {
    state(|s| s.input = input);
    app.activity_loop();
}

#[test]
fn keyboard_real_app_install_return_cancel_failure_and_drop_once() {
    assert_eq!(
        unsafe { keyboard::xp_app_keyboard_push(ptr::null_mut()) },
        0
    );
    let app = build_app();
    // Pin every production error, including byte-count boundaries, before narrowing formatting.
    {
        use xp_ui::keyboard::owner::Host;
        let keyboard = keyboard::create(app, NonNull::new(context(b"", true)).unwrap()).unwrap();
        for (message, expected) in [
            (
                "Cannot build scene in %s",
                "Cannot build scene in KeyboardEntry",
            ),
            ("Invalid scene in %s", "Invalid scene in KeyboardEntry"),
            (
                "Cannot allocate keyboard scene",
                "Cannot allocate keyboard scene",
            ),
        ] {
            keyboard.host().error(message, 0);
            assert_eq!(
                state(|s| s.errors.pop().unwrap()),
                (1, b"RUI".to_vec(), expected.as_bytes().to_vec())
            );
        }
        for bytes in [0, 1, 42, u32::MAX] {
            for (message, prefix) in [
                (
                    "Cannot allocate keyboard touch text: %u bytes",
                    "Cannot allocate keyboard touch text: ",
                ),
                (
                    "Cannot grow keyboard text: %u bytes",
                    "Cannot grow keyboard text: ",
                ),
            ] {
                keyboard.host().error(message, bytes);
                assert_eq!(
                    state(|s| s.errors.pop().unwrap()),
                    (
                        1,
                        b"RUI".to_vec(),
                        std::format!("{prefix}{bytes} bytes").into_bytes()
                    )
                );
            }
        }
    }
    let parent = context(b"", false);
    app.replace(app.adopt(parent).unwrap());
    for initial in [b"".as_slice(), b"maximum text"] {
        for cancel in [false, true] {
            let child = context(initial, true);
            state(|s| s.expected = initial.to_vec());
            assert_eq!(unsafe { keyboard::xp_app_keyboard_push(child) }, 1);
            assert!(app.navigation.drain(app));
            assert_eq!(unsafe { facts(child, 0, 0) }.prevent_sleep, 1);
            state(|s| {
                s.edit_pointer = s
                    .live
                    .values()
                    .find(|(_, n)| *n == initial.len() + 1)
                    .unwrap()
                    .0
            });
            paint_current(app);
            send_input(
                app,
                Input {
                    pressed: if cancel { model::BACK } else { model::CONFIRM },
                    released: if cancel { model::BACK } else { model::CONFIRM },
                    ..Input::default()
                },
            );
            state(|s| {
                assert_eq!(s.cancelled, cancel);
                assert!(s.live.is_empty());
            });
            assert!(
                matches!(app.navigation.current(),Some(Target::Legacy(p)) if p.as_ptr()==parent)
            );
        }
    }
    // Real native buffer backend: partial scene failures publish nothing and retry.
    for n in 0..5 {
        state(|s| s.expected = b"retry-text".to_vec());
        assert_eq!(
            unsafe { keyboard::xp_app_keyboard_push(context(b"retry-text", true)) },
            1
        );
        assert!(app.navigation.drain(app));
        state(|s| {
            s.edit_pointer = s.live.values().find(|(_, n)| *n == 11).unwrap().0;
            s.fail_after = Some(n);
        });
        let paints = state(|s| s.paints);
        paint_current(app);
        assert_eq!(state(|s| s.paints), paints);
        send_input(
            app,
            Input {
                pressed: model::CONFIRM,
                released: model::CONFIRM,
                ..Input::default()
            },
        );
        assert!(matches!(
            app.navigation.current(),
            Some(Target::Keyboard(_))
        ));
        state(|s| s.fail_after = None);
        paint_current(app);
        send_input(app, Input::default());
        send_input(
            app,
            Input {
                pressed: model::CONFIRM,
                released: model::CONFIRM,
                ..Input::default()
            },
        );
        state(|s| assert!(s.live.is_empty()));
    }
    // No production Rust parent exists yet. This host receiver delegates every
    // lifecycle operation to App and retains its actual typed result after pop.
    for cancel in [false, true] {
        let capture = RustParent {
            app,
            result: RefCell::new(None),
        };
        assert_eq!(
            unsafe { keyboard::xp_app_keyboard_push(context(b"semantic", true)) },
            1
        );
        assert!(app.navigation.drain(&capture));
        // Capture edit storage before rendering allocates an equal-sized snapshot.
        let pointer = state(|s| s.live.values().find(|(_, n)| *n == 9).unwrap().0);
        paint_current(app);
        let Some(Target::Keyboard(k)) = app.navigation.current() else {
            panic!("keyboard")
        };
        assert!(keyboard::input(
            unsafe { k.as_ref() },
            Input {
                pressed: if cancel { model::BACK } else { model::CONFIRM },
                released: if cancel { model::BACK } else { model::CONFIRM },
                ..Input::default()
            }
        ));
        app.navigation.queue(Transition::Pop);
        assert!(app.navigation.drain(&capture));
        let result = capture.result.borrow_mut().take().unwrap().unwrap();
        assert_eq!(result.is_cancelled(), cancel);
        if !cancel {
            assert_eq!(result.text(), Some(b"semantic".as_slice()));
            assert_eq!(result.text().unwrap().as_ptr(), pointer);
        }
        drop(result);
        state(|s| assert!(s.live.is_empty()));
    }
    // Each partially initialized resource state is destroyed through the real pop.
    for n in 0..2 {
        state(|s| s.fail_after = Some(n));
        assert_eq!(
            unsafe { keyboard::xp_app_keyboard_push(context(b"failure", true)) },
            1
        );
        assert!(app.navigation.drain(app));
        state(|s| {
            s.fail_after = None;
            assert!(s.live.is_empty());
            assert!(s.cancelled);
        });
    }
    // Rejected shell adoption leaves its native facade with the manager.
    let rejected = context(b"rejected", true);
    state(|s| s.shell_fail = true);
    assert_eq!(unsafe { keyboard::xp_app_keyboard_push(rejected) }, 0);
    unsafe { destroy(rejected) };
    // Replacing a pending keyboard frees it without enter or an extra child.
    let before = state(|s| s.destroyed);
    assert_eq!(
        unsafe { keyboard::xp_app_keyboard_push(context(b"pending", true)) },
        1
    );
    app.navigation
        .queue(Transition::Push(app.adopt(context(b"", false)).unwrap()));
    assert_eq!(state(|s| s.destroyed), before + 1);
    assert!(app.navigation.drain(app));
    app.replace(app.adopt(context(b"", false)).unwrap());
    assert!(app.navigation.drain(app));
    assert_eq!(state(|s| s.parent_drops), 2);
}

struct RustParent<'a> {
    app: &'a App,
    result: RefCell<Option<Option<keyboard::Result>>>,
}
impl Services<Owner> for RustParent<'_> {
    type RenderGuard = RenderGuard;
    type Result = ActivityResult;
    fn render_lock(&self) -> RenderGuard {
        self.app.render_lock()
    }
    fn before_screen_change(&self, r: bool) -> bool {
        self.app.before_screen_change(r)
    }
    fn publish(&self, t: Option<Target>, v: u32) {
        self.app.publish(t, v)
    }
    fn clear_header(&self) {
        self.app.clear_header()
    }
    fn enter(&self, t: Target) {
        self.app.enter(t)
    }
    fn exit(&self, t: Target) {
        self.app.exit(t)
    }
    fn with_result(&self, t: Target, f: impl FnOnce(ActivityResult)) {
        self.app.with_result(t, f)
    }
    fn deliver_result(&self, parent: Target, result: ActivityResult) {
        state(|s| {
            assert!(!s.locked);
            assert_eq!(s.entered, s.closed);
        });
        assert!(
            matches!((parent,self.app.signals.with(|s|s.target)),(Target::Legacy(a),Some(Target::Legacy(b))) if a==b)
        );
        let ActivityResult::Keyboard(result) = result else {
            panic!("semantic result")
        };
        *self.result.borrow_mut() = Some(result);
    }
    fn home(&self) -> Option<Owner> {
        panic!("home")
    }
    fn request_update(&self) {
        self.app.request_update()
    }
    fn missing_pop(&self) {
        panic!("pop")
    }
    fn stack_allocation_failed(&self) {
        panic!("stack")
    }
    fn prepare_sleep(&self, t: Target) {
        self.app.prepare_sleep(t)
    }
}
