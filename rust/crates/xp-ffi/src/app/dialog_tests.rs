use super::keyboard_tests::dialog_leaves;
use super::*;
use core::alloc::Layout as AllocLayout;
use core::ptr;
use std::{collections::BTreeMap, vec::Vec};
use xp_ui::{ffi::Metrics, model, utility::Layout};
use xp_ui::{
    ffi::{Paint, SceneView},
    model::Input,
    scene::{Command, Rect},
};
#[derive(Default)]
struct State {
    locked: bool,
    model: bool,
    destroyed: usize,
    paints: usize,
    logs: Vec<u8>,
    live: BTreeMap<usize, (*mut u8, usize)>,
    fail_after: Option<usize>,
    shell_fail: bool,
    input: Input,
    delivered: Vec<(u8, u32)>,
    paint_fail: bool,
    ops: Vec<(bool, usize)>,
    expected_destroyed: usize,
}
std::thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
fn state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.with(|s| f(&mut s.borrow_mut()))
}
struct Context {
    text: Vec<u8>,
    kind: u8,
}
fn context() -> *mut c_void {
    Box::into_raw(Box::new(Context {
        text: b"message".to_vec(),
        kind: 2,
    }))
    .cast()
}
unsafe extern "C" fn create(_: *const abi::Launch) -> *mut c_void {
    context()
}
unsafe extern "C" fn destroy(p: *mut c_void) {
    unsafe { drop(Box::from_raw(p.cast::<Context>())) };
    state(|s| s.destroyed += 1);
}
unsafe extern "C" fn ignored(_: *mut c_void) {}
unsafe extern "C" fn nothing() {}
unsafe extern "C" fn no(_: *mut c_void) -> u8 {
    0
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    0
}
unsafe extern "C" fn shot(_: *const c_void, _: abi::ScreenshotSink, _: *mut c_void) {
    panic!("shot")
}
unsafe extern "C" fn old_render(_: *mut c_void, _: *mut abi::RenderToken) {
    panic!("legacy render")
}
unsafe extern "C" fn old_result(_: *mut c_void, _: abi::ResultSink, _: *mut c_void) {
    panic!("legacy result")
}
unsafe extern "C" fn delivery(_: *mut c_void, _: *const c_void) {
    panic!("native projection")
}
unsafe extern "C" fn facts(_: *const c_void, _: u8, _: u8) -> abi::LegacyFacts {
    unsafe { core::mem::zeroed() }
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
unsafe extern "C" fn feedback(_: u8) {}
fn build_app() -> &'static App {
    let legacy = Box::into_raw(Box::new(abi::Legacy {
        create,
        destroy,
        enter: ignored,
        r#loop: ignored,
        exit: ignored,
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
    resources.text_box_height = height;
    resources.translate = translate;
    let mut runtime = test_resources::runtime();
    runtime.render_lock = render_lock;
    runtime.render_unlock = render_unlock;
    runtime.input_mapped_snapshot = snapshot;
    runtime.model_lock = model_lock;
    runtime.model_unlock = model_unlock;
    runtime.ble_before_screen_change = before;
    runtime.header_back_clear = nothing;
    runtime.notify_render = nothing;
    runtime.frontlight_present = frontlight;
    runtime.touch_feedback = feedback;
    let platform = abi::Platform {
        legacy,
        resources: Box::into_raw(Box::new(resources)),
        runtime: Box::into_raw(Box::new(runtime)),
        epub: Box::into_raw(Box::new(dialog_leaves())),
    };
    super::dialog_tests::record_tables(&platform);
    let mut compatibility = core::mem::MaybeUninit::uninit();
    assert_eq!(
        unsafe { xp_app_init(&platform, compatibility.as_mut_ptr()) },
        abi::OK
    );
    app().unwrap()
}
#[test]
fn dialog_real_app_install_results_failures_and_drop_once() {
    let _serial = APP_LOCK.lock().unwrap();
    let app = build_app();
    let launch = abi::Launch {
        kind: abi::MESSAGE,
        value: abi::LaunchValue {
            message: abi::MessageLaunch {
                text: abi::Text::from_bytes(b"message"),
                style: 0,
            },
        },
    };
    let owner = app.create(&launch).unwrap();
    assert!(
        !matches!(owner.target(), Target::Legacy(_)),
        "MESSAGE must install the Rust owner directly"
    );
    drop(owner);
    assert_eq!(state(|s| s.destroyed), 1);

    let parent = context();
    app.replace(app.adopt(parent).unwrap());
    for kind in 0..=1 {
        for cancel in [false, true] {
            for _ in 0..3 {
                let child = context();
                unsafe {
                    (*child.cast::<Context>()).kind = kind;
                }
                let before = state(|s| s.destroyed);
                assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 1);
                assert!(app.navigation.drain(app));
                assert!(matches!(app.navigation.current(), Some(Target::Dialog(_))));
                let arena = if kind == 0 { 464 } else { 816 };
                assert!(state(|s| s.live.values().any(|(_, n)| *n == arena)));
                paint_current(app);
                state(|s| s.expected_destroyed = before + 1);
                close_input(app, kind, cancel);
                assert!(
                    matches!(app.navigation.current(),Some(Target::Legacy(p)) if p.as_ptr()==parent)
                );
                assert_eq!(
                    state(|s| s.delivered.pop().unwrap()),
                    if cancel {
                        (0, 0)
                    } else if kind == 0 {
                        (1, 0)
                    } else {
                        (2, 5)
                    }
                );
            }
        }
    }
    // Held input across a failed first paint cannot activate an unpublished scene.
    for kind in 0..=1 {
        let child = context();
        unsafe {
            (*child.cast::<Context>()).kind = kind;
        }
        assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 1);
        assert!(app.navigation.drain(app));
        state(|s| s.paint_fail = true);
        paint_current(app);
        send_input(
            app,
            Input {
                held: model::CONFIRM,
                ..Input::default()
            },
        );
        state(|s| s.paint_fail = false);
        paint_current(app);
        send_input(
            app,
            Input {
                released: model::CONFIRM,
                ..Input::default()
            },
        );
        assert!(matches!(app.navigation.current(), Some(Target::Dialog(_))));
        send_input(app, Input::default());
        state(|s| s.expected_destroyed = s.destroyed + 1);
        close_input(app, kind, true);
    }
    // Arena replacement allocates before release, including failed reentry.
    for kind in 0..=1 {
        let child = context();
        unsafe {
            (*child.cast::<Context>()).kind = kind;
        }
        assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 1);
        assert!(app.navigation.drain(app));
        let Some(Target::Dialog(target)) = app.navigation.current() else {
            panic!("dialog")
        };
        state(|s| s.ops.clear());
        target.enter();
        let n = if kind == 0 { 464 } else { 816 };
        assert_eq!(state(|s| s.ops.clone()), [(true, n), (false, n)]);
        state(|s| {
            s.fail_after = Some(0);
            s.expected_destroyed = s.destroyed + 1;
        });
        target.enter();
        state(|s| s.fail_after = None);
        assert!(app.navigation.drain(app));
        assert_eq!(state(|s| s.delivered.last().copied()), Some((0, 0)));
    }
    // Scalar results are extracted by the production App before teardown and moved
    // to an owned Rust receiver, without calling the native projection leaf.
    for kind in 0..=2 {
        for cancel in [false, true] {
            let capture = RustParent {
                app,
                result: RefCell::new(None),
            };
            let child = context();
            unsafe {
                (*child.cast::<Context>()).kind = kind;
            }
            assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 1);
            assert!(app.navigation.drain(&capture));
            paint_current(app);
            let Some(Target::Dialog(target)) = app.navigation.current() else {
                panic!("dialog")
            };
            if kind == 2 {
                target.input(Input {
                    released: model::BACK | model::CONFIRM,
                    ..Input::default()
                });
                assert_eq!(app.navigation.pending_kind(), TransitionKind::None);
                app.navigation.queue(Transition::Pop);
            } else {
                if kind == 0 && !cancel {
                    target.input(Input {
                        pressed: model::NAV_NEXT,
                        ..Input::default()
                    });
                }
                target.input(Input {
                    released: if cancel { model::BACK } else { model::CONFIRM },
                    ..Input::default()
                });
                target.input(Input::default());
            }
            let delivered = state(|s| s.delivered.len());
            assert!(app.navigation.drain(&capture));
            let expected = if kind == 2 {
                dialog::Result::MessageDismissed
            } else if cancel {
                dialog::Result::Cancelled
            } else if kind == 0 {
                dialog::Result::Confirmed
            } else {
                dialog::Result::Interval(5)
            };
            assert_eq!(capture.result.borrow_mut().take(), Some(Some(expected)));
            assert_eq!(state(|s| s.delivered.len()), delivered);
        }
    }
    // Every fallible shell/text stage leaves rejected ownership with the caller.
    for kind in 0..=2 {
        let child = context();
        unsafe {
            (*child.cast::<Context>()).kind = kind;
        }
        state(|s| s.shell_fail = true);
        assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 0);
        unsafe { destroy(child) };
        for n in 0..if kind == 0 {
            4
        } else if kind == 2 {
            1
        } else {
            0
        } {
            let child = context();
            unsafe {
                (*child.cast::<Context>()).kind = kind;
            }
            state(|s| s.fail_after = Some(n));
            assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 0);
            state(|s| {
                s.fail_after = None;
                assert!(s.live.is_empty());
            });
            unsafe { destroy(child) };
        }
    }
    let child = context();
    let before = state(|s| s.destroyed);
    assert_eq!(unsafe { dialog::xp_app_dialog_push(child) }, 1);
    app.navigation
        .queue(Transition::Push(app.adopt(context()).unwrap()));
    assert_eq!(state(|s| s.destroyed), before + 1);
    assert!(app.navigation.drain(app));
    cleanup();
}

pub(super) fn allocate_shell(layout: AllocLayout) -> *mut u8 {
    if state(|s| core::mem::take(&mut s.shell_fail)) {
        return ptr::null_mut();
    }
    unsafe { std::alloc::alloc_zeroed(layout) }
}
unsafe extern "C" fn allocate(_: u8, bytes: usize) -> *mut u8 {
    if state(|s| match &mut s.fail_after {
        Some(0) => true,
        Some(n) => {
            *n -= 1;
            false
        }
        None => false,
    }) {
        return ptr::null_mut();
    }
    let p = unsafe { std::alloc::alloc_zeroed(AllocLayout::from_size_align(bytes, 8).unwrap()) };
    state(|s| {
        assert!(!s.model);
        s.live.insert(p.addr(), (p, bytes));
        s.ops.push((true, bytes));
    });
    p
}
unsafe extern "C" fn release(_: u8, p: *mut u8) {
    let (_, n) = state(|s| {
        assert!(!s.model);
        {
            let value = s.live.remove(&p.addr()).unwrap();
            s.ops.push((false, value.1));
            value
        }
    });
    unsafe { std::alloc::dealloc(p, AllocLayout::from_size_align(n, 8).unwrap()) };
}
unsafe extern "C" fn snapshot(_: u8, full: u8, _: u8, out: *mut abi::MappedSnapshot) {
    assert_eq!(full, 1);
    unsafe {
        out.write(core::mem::zeroed());
        (*out).input = state(|s| s.input)
    };
}
unsafe extern "C" fn translate(_: u16) -> abi::Text {
    abi::Text::from_terminated(b"label\0")
}
unsafe extern "C" fn height(_: abi::Text, _: abi::TextLayout) -> i32 {
    20
}
unsafe extern "C" fn text(_: *const Command, _: Rect, t: abi::Text) {
    if t.len != 0 {
        assert_eq!(unsafe { *t.data.add(t.len) }, 0);
    }
}
unsafe extern "C" fn refresh(_: u8) {}
unsafe extern "C" fn paint(scene: *const SceneView, p: *const Paint) -> u8 {
    if state(|s| s.paint_fail) {
        return 0;
    }
    let (scene, p) = unsafe { (&*scene, &*p) };
    for c in unsafe { core::slice::from_raw_parts(scene.commands, scene.command_count as usize) } {
        if c.resource != 0 {
            assert_eq!(
                unsafe { (p.resource_valid.unwrap())(p.context, c.resource, c.generation) },
                1
            );
            unsafe {
                (p.resource.unwrap())(
                    p.context,
                    ptr::null(),
                    c,
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
#[no_mangle]
unsafe extern "C" fn xp_dialog_launch(p: *mut c_void, out: *mut dialog::Launch) {
    let c = unsafe { &*p.cast::<Context>() };
    let none = xp_core::i18n::ids::STR_NONE_OPT;
    unsafe {
        out.write(dialog::Launch {
            kind: c.kind,
            style: 0,
            refresh: 1,
            message_font: 1,
            name: abi::Text::from_terminated(b"custom interval\0"),
            heading: abi::Text::from_bytes(b"title"),
            body: abi::Text::from_bytes(&c.text),
            cancel: abi::Text::from_bytes(b"cancel"),
            confirm: abi::Text::from_bytes(b"confirm"),
            interval: dialog::Interval {
                title: 0,
                format: none,
                boundary: none,
                initial: 5,
                minimum: 1,
                maximum: 10,
                small_step: 1,
                large_step: 3,
                edge_buttons: 1,
                reader: 0,
            },
        })
    };
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_release_launch(p: *mut c_void) {
    unsafe { (&mut *p.cast::<Context>()).text.clear() };
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_layout(_: *mut c_void, out: *mut Layout) {
    unsafe {
        out.write(Layout {
            screen: Rect {
                x: 0,
                y: 0,
                w: 528,
                h: 792,
            },
            safe: Rect {
                x: 0,
                y: 0,
                w: 528,
                h: 792,
            },
            body_font: 1,
            small_font: 1,
            title_font: 1,
            readout_font: 1,
            spacing: 8,
            hints_height: 40,
            popup_padding: 12,
            popup_gap: 8,
            popup_side_margin: 20,
            selection_padding: 8,
            space_sm: 8,
            space_md: 16,
            space_lg: 24,
            row_height: 40,
            min_touch: 40,
            touch: 1,
            ..Layout::default()
        })
    };
}
unsafe extern "C" fn width(_: *mut c_void, _: i32, _: *const u8, n: u16, _: u8) -> i32 {
    i32::from(n) * 8
}
unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
    20
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_metrics(_: *mut c_void) -> Metrics {
    Metrics {
        context: ptr::null_mut(),
        width: Some(width),
        line_height: Some(line),
    }
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_footer(_: *mut c_void, _: Rect, labels: *const abi::Text) {
    for t in unsafe { core::slice::from_raw_parts(labels, 4) } {
        assert_eq!(unsafe { *t.data.add(t.len) }, 0);
    }
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_format(_: u16, v: i32, _: u8, out: *mut u8, n: usize) {
    let t = v.to_string();
    let len = t.len().min(n - 1);
    unsafe {
        ptr::copy_nonoverlapping(t.as_ptr(), out, len);
        out.add(len).write(0)
    };
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_log(_: *mut c_void, event: u8, _: u32) {
    state(|s| s.logs.push(event));
}
#[no_mangle]
unsafe extern "C" fn xp_dialog_deliver(
    _: *const abi::Legacy,
    parent: *mut c_void,
    kind: u8,
    value: u32,
) {
    assert!(
        matches!(app().unwrap().navigation.current(),Some(Target::Legacy(p)) if p.as_ptr()==parent)
    );
    assert!(
        matches!(app().unwrap().signals.with(|s|s.target),Some(Target::Legacy(p)) if p.as_ptr()==parent)
    );
    state(|s| {
        assert!(!s.locked);
        assert!(s.live.is_empty());
        assert_eq!(s.destroyed, s.expected_destroyed);
        assert_eq!(s.logs.last(), Some(&1));
        s.delivered.push((kind, value));
    });
}
fn cleanup() {
    drop_app();
    state(|s| {
        assert!(s.live.is_empty());
        *s = State::default();
    });
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
fn close_input(app: &App, kind: u8, cancel: bool) {
    if kind == 0 && !cancel {
        send_input(
            app,
            Input {
                pressed: model::NAV_NEXT,
                ..Input::default()
            },
        );
    }
    send_input(
        app,
        Input {
            released: if cancel { model::BACK } else { model::CONFIRM },
            ..Input::default()
        },
    );
    if matches!(app.navigation.current(), Some(Target::Dialog(_))) {
        send_input(app, Input::default());
    }
}

struct RustParent<'a> {
    app: &'a App,
    result: RefCell<Option<Option<dialog::Result>>>,
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
            assert_eq!(s.logs.last(), Some(&1));
        });
        assert!(
            matches!((parent,self.app.signals.with(|s|s.target)),(Target::Legacy(a),Some(Target::Legacy(b))) if a==b)
        );
        let ActivityResult::Dialog(result) = result else {
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

pub(super) static APP_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
// Both real-App fixtures allocate these four immutable service tables.
type Tables = (
    *mut abi::Legacy,
    *mut abi::Resources,
    *mut abi::Runtime,
    *mut epub::abi::XpEpubLeaves,
);
std::thread_local! { static TABLES: Cell<Option<Tables>> = const {Cell::new(None)}; }
pub(super) fn record_tables(platform: &abi::Platform) {
    TABLES.with(|slot| {
        assert!(slot
            .replace(Some((
                platform.legacy.cast_mut(),
                platform.resources.cast_mut(),
                platform.runtime.cast_mut(),
                platform.epub.cast_mut()
            )))
            .is_none())
    });
}
pub(super) fn drop_app() {
    let (legacy, resources, runtime, epub) = TABLES.with(|slot| slot.take().unwrap());
    unsafe {
        drop((&mut *APP.0.get()).take());
        drop(Box::from_raw(legacy));
        drop(Box::from_raw(resources));
        drop(Box::from_raw(runtime));
        drop(Box::from_raw(epub));
    }
}
