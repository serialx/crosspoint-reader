extern crate std;
use crate as xp_ui;
use std::{
    alloc::{alloc_zeroed, dealloc, Layout as Allocation},
    cell::{Cell, RefCell},
    ffi::c_void,
    rc::Rc,
};
use std::{boxed::Box, thread_local, vec::Vec};
use xp_ui::{
    keyboard::{
        owner::{
            storage::{next_bytes, Memory},
            Host, Keyboard, Lock, Resources,
        },
        Layouts, Row, WireKey,
    },
    keyboard_view::{Layout, Metrics, PaintKey},
    model::{self, Input},
    scene::{Command, Rect, Scene},
};

thread_local! {
    static FAIL_AT: Cell<usize> = const { Cell::new(usize::MAX) };
    static OWNER_FAIL: Cell<bool> = const { Cell::new(false) };
    static FAIL: Cell<bool> = const { Cell::new(false) };
    static EVENTS: RefCell<Vec<(bool,usize)>> = const { RefCell::new(Vec::new()) };
    static MODEL_LOCKED: Cell<bool> = const { Cell::new(false) };
}
pub(super) struct Heap;
unsafe impl Memory for Heap {
    fn owner(l: Allocation) -> *mut u8 {
        if OWNER_FAIL.with(|x| x.replace(false)) {
            std::ptr::null_mut()
        } else {
            unsafe { alloc_zeroed(l) }
        }
    }
    fn allocate(layout: Allocation) -> *mut u8 {
        MODEL_LOCKED.with(|x| assert!(!x.get(), "allocation in model lock"));
        EVENTS.with(|x| x.borrow_mut().push((true, layout.size())));
        let fail_at = FAIL_AT.with(|x| {
            let n = x.get();
            x.set(n.saturating_sub(1));
            n == 0
        });
        if FAIL.with(|x| x.replace(false)) || fail_at {
            std::ptr::null_mut()
        } else {
            unsafe { alloc_zeroed(layout) }
        }
    }
    unsafe fn release(p: *mut u8, l: Allocation) {
        MODEL_LOCKED.with(|x| assert!(!x.get(), "release in model lock"));
        EVENTS.with(|x| x.borrow_mut().push((false, l.size())));
        unsafe { dealloc(p, l) };
    }
}
pub(super) struct Facts {
    line: Cell<i32>,
    paint_fail: Cell<bool>,
    errors: RefCell<Vec<&'static str>>,
    refresh: Cell<usize>,
    paints: Cell<usize>,
    physical: Cell<usize>,
    locks: RefCell<Vec<Lock>>,
    requests: Cell<usize>,
}
impl Default for Facts {
    fn default() -> Self {
        Self {
            line: Cell::new(20),
            paint_fail: Cell::new(false),
            errors: RefCell::new(Vec::new()),
            refresh: Cell::new(0),
            paints: Cell::new(0),
            physical: Cell::new(0),
            locks: RefCell::new(Vec::new()),
            requests: Cell::new(0),
        }
    }
}
pub(super) struct Platform(Rc<Facts>);
unsafe extern "C" fn row(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, r: u8) -> Row {
    Row {
        count: u8::from(r == 0),
        ..Row::default()
    }
}
unsafe extern "C" fn key(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, _: u8, _: u8) -> WireKey {
    WireKey {
        label: c"é".as_ptr(),
        output: c"é".as_ptr(),
        alternate: c"É".as_ptr(),
        value: 1000,
        kind: 0,
        units: 2,
    }
}
unsafe extern "C" fn measure(_: *mut c_void, _: i32, _: *const u8, n: u32, mode: u8) -> i32 {
    if mode == 2 {
        0
    } else {
        n as i32 * 8
    }
}
unsafe extern "C" fn line(context: *mut c_void, _: i32) -> i32 {
    unsafe { &*context.cast::<Facts>() }.line.get()
}
unsafe impl Host for Platform {
    type Memory = Heap;
    fn lock(&self, kind: Lock) {
        if kind == Lock::Text {
            MODEL_LOCKED.with(|x| assert!(!x.get(), "reversed lock order"));
        }
        if kind == Lock::Model {
            MODEL_LOCKED.with(|x| assert!(!x.replace(true)));
        }
        self.0.locks.borrow_mut().push(kind);
    }
    fn unlock(&self, kind: Lock) {
        assert_eq!(self.0.locks.borrow_mut().pop(), Some(kind));
        if kind == Lock::Model {
            MODEL_LOCKED.with(|x| x.set(false));
        }
    }
    fn settings(&self) -> (u16, u8) {
        (1, 0)
    }
    fn layout(&self) -> Layout {
        Layout {
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
        }
    }
    fn with_layouts<R>(&self, f: impl FnOnce(&Layouts) -> R) -> R {
        f(&Layouts {
            context: std::ptr::null_mut(),
            row: Some(row),
            key: Some(key),
        })
    }
    fn with_metrics<R>(&self, f: impl FnOnce(&Metrics) -> R) -> R {
        assert!(
            self.0.locks.borrow().is_empty(),
            "font measurement under a lock"
        );
        f(&Metrics {
            context: Rc::as_ptr(&self.0).cast_mut().cast(),
            measure: Some(measure),
            line_height: Some(line),
        })
    }
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool {
        let before = self.0.physical.get();
        for (resource, generation) in [(0, 1), (1, 2), (0x105, 1), (u32::MAX, 1)] {
            assert!(!resources.valid(resource, generation));
            resources.paint(
                &Command {
                    resource,
                    generation,
                    ..Command::default()
                },
                scene_clip(),
            );
        }
        assert_eq!(self.0.physical.get(), before);
        for command in scene.commands {
            if command.kind == 4 {
                assert!(resources.valid(command.resource, command.generation));
                resources.paint(command, scene_clip());
            }
        }
        self.0.paints.set(self.0.paints.get() + 1);
        !self.0.paint_fail.get()
    }
    fn chrome(&self, _: &Command, clip: Rect, title: &[u8]) {
        assert_eq!(clip, scene_clip());
        assert_eq!(title.last(), Some(&0));
        self.0.physical.set(self.0.physical.get() + 1);
    }
    fn key(&self, _: &Command, _: &PaintKey, _: u8, _: bool, _: bool) {
        self.0.physical.set(self.0.physical.get() + 1);
    }
    fn text(&self, _: &Command, _: Rect, text: &[u8]) {
        assert_eq!(text.last(), Some(&0));
        self.0.physical.set(self.0.physical.get() + 1);
    }
    fn refresh(&self) {
        self.0.refresh.set(self.0.refresh.get() + 1);
    }
    fn request_update(&self) {
        self.0.requests.set(self.0.requests.get() + 1);
    }
    fn haptic(&self, _: bool) {}
    fn error(&self, message: &'static str, _: u32) {
        self.0.errors.borrow_mut().push(message);
    }
}
fn scene_clip() -> Rect {
    Rect {
        x: 0,
        y: 0,
        w: 528,
        h: 792,
    }
}
pub(super) fn owner(initial: &[u8], maximum: usize) -> (Box<Keyboard<Platform>>, Rc<Facts>) {
    let facts = Rc::new(Facts::default());
    (
        Keyboard::try_new(Platform(facts.clone()), b"Enter Text", initial, maximum, 0).unwrap(),
        facts,
    )
}
fn select() -> Input {
    Input {
        pressed: model::CONFIRM,
        released: model::CONFIRM,
        ..Input::default()
    }
}

#[test]
fn native_byte_growth_and_overflow_fallback() {
    assert_eq!(next_bytes(8, 9), 16);
    assert_eq!(next_bytes(8, 31), 31);
    assert_eq!(next_bytes(u32::MAX / 2 + 1, u32::MAX), u32::MAX);
}
#[test]
fn failed_edit_keeps_bytes_cursor_then_retries() {
    let (keyboard, facts) = owner(b"", 0);
    assert!(keyboard.render());
    FAIL.with(|x| x.set(true));
    keyboard.input(select(), None);
    keyboard.with_text(|s, b| {
        assert_eq!(s.cursor, 0);
        assert_eq!(b, b"");
    });
    assert_eq!(
        facts.errors.borrow().as_slice(),
        ["Cannot grow keyboard text: %u bytes"]
    );
    keyboard.input(select(), None);
    keyboard.with_text(|s, b| {
        assert_eq!(s.cursor, 2);
        assert_eq!(b, "é".as_bytes());
    });
}
#[test]
fn partial_scene_and_failed_paint_never_publish() {
    let (keyboard, facts) = owner(b"abc", 0);
    FAIL.with(|x| x.set(true));
    assert!(!keyboard.render());
    assert!(!keyboard.ready());
    facts.paint_fail.set(true);
    assert!(!keyboard.render());
    assert!(!keyboard.ready());
    assert_eq!(facts.refresh.get(), 0);
    facts.paint_fail.set(false);
    assert!(keyboard.render());
    assert!(keyboard.ready());
    keyboard.input(select(), None);
    keyboard.with_text(|_, b| assert_eq!(b, b"abc"));
    keyboard.input(select(), None);
    keyboard.with_text(|_, b| assert_eq!(b, "abcé".as_bytes()));
}
#[test]
fn repeated_open_close_and_cancel_release_resources() {
    EVENTS.with(|x| x.borrow_mut().clear());
    for _ in 0..4 {
        let (k, _) = owner(b"abc", 0);
        assert!(k.render());
        k.input(
            Input {
                released: model::BACK,
                ..Input::default()
            },
            None,
        );
        assert!(k.take_result().unwrap().is_cancelled());
        k.exit();
        drop(k);
    }
    EVENTS.with(|x| {
        let e = x.borrow();
        assert_eq!(
            e.iter().filter(|e| e.0).map(|e| e.1).sum::<usize>(),
            e.iter().filter(|e| !e.0).map(|e| e.1).sum::<usize>()
        );
    });
}

#[test]
fn every_scene_capacity_failure_recovers_without_partial_publication() {
    let expected = [
        4, 768, 96, 512, 1536, 192, 3072, 384, 6144, 768, 8192, 1024, 2048, 4096,
    ];
    let (k, f) = owner(b"abc", 0);
    EVENTS.with(|x| x.borrow_mut().clear());
    f.line.set(0);
    assert!(!k.render());
    assert!(!k.ready());
    EVENTS.with(|x| {
        assert_eq!(
            x.borrow()
                .iter()
                .filter(|x| x.0)
                .map(|x| x.1)
                .collect::<Vec<_>>(),
            expected
        )
    });
    f.line.set(20);
    assert!(k.render());
    drop(k);
    for fail in 0..expected.len() {
        let (k, f) = owner(b"abc", 0);
        EVENTS.with(|x| x.borrow_mut().clear());
        f.line.set(0);
        FAIL_AT.with(|x| x.set(fail));
        assert!(!k.render());
        assert!(!k.ready());
        assert_eq!(f.refresh.get(), 0);
        FAIL_AT.with(|x| x.set(usize::MAX));
        f.line.set(20);
        assert!(k.render());
        assert!(k.ready());
        k.with_text(|s, text| {
            assert_eq!(s.cursor, 3);
            assert_eq!(text, b"abc");
        });
    }
}
#[test]
fn shell_title_and_initial_text_failure_clean_up() {
    OWNER_FAIL.with(|x| x.set(true));
    let f = Rc::new(Facts::default());
    assert!(Keyboard::try_new(Platform(f.clone()), b"title", b"abc", 0, 0).is_none());
    assert_eq!(f.errors.borrow().len(), 1);
    for n in 0..2 {
        EVENTS.with(|x| x.borrow_mut().clear());
        FAIL_AT.with(|x| x.set(n));
        let f = Rc::new(Facts::default());
        assert!(Keyboard::try_new(Platform(f.clone()), b"title", b"abc", 0, 0).is_none());
        assert_eq!(f.errors.borrow().len(), 1);
        FAIL_AT.with(|x| x.set(usize::MAX));
    }
    let f = Rc::new(Facts::default());
    assert!(Keyboard::try_new(Platform(f), b"t", b"", u32::MAX as usize + 1, 0).is_none());
}
#[test]
fn native_replacement_allocates_before_free_and_exit_order() {
    let (k, f) = owner(b"abc", 0);
    EVENTS.with(|x| x.borrow_mut().clear());
    f.line.set(0);
    assert!(!k.render());
    EVENTS.with(|x| {
        let e = x.borrow();
        for (old, new) in [
            (768, 1536),
            (1536, 3072),
            (3072, 6144),
            (6144, 8192),
            (96, 192),
            (192, 384),
            (384, 768),
            (512, 1024),
            (1024, 2048),
            (2048, 4096),
        ] {
            assert!(e.windows(2).any(|pair| pair == [(true, new), (false, old)]));
        }
    });
    EVENTS.with(|x| x.borrow_mut().clear());
    k.exit();
    EVENTS.with(|x| {
        assert_eq!(
            *x.borrow(),
            [
                (false, 4),
                (false, 4096),
                (false, 768),
                (false, 8192),
                (false, 4)
            ]
        )
    });
}
#[test]
fn max_length_and_held_failure_do_not_repeat_the_edit() {
    let (k, f) = owner(b"", 2);
    assert!(k.render());
    k.input(select(), None);
    k.input(select(), None);
    k.with_text(|s, b| {
        assert_eq!(b, "é".as_bytes());
        assert_eq!(s.cursor, 2);
    });
    assert!(f.errors.borrow().is_empty());
    let (k, f) = owner(b"", 0);
    assert!(k.render());
    k.input(
        Input {
            pressed: model::CONFIRM,
            held: model::CONFIRM,
            ..Input::default()
        },
        None,
    );
    FAIL.with(|x| x.set(true));
    let hold = Input {
        held: model::CONFIRM,
        held_ms: 501,
        now_ms: 501,
        ..Input::default()
    };
    k.input(hold, None);
    k.input(hold, None);
    k.input(
        Input {
            released: model::CONFIRM,
            ..Input::default()
        },
        None,
    );
    k.with_text(|s, b| {
        assert!(b.is_empty());
        assert_eq!(s.cursor, 0);
    });
    assert_eq!(f.errors.borrow().len(), 1);
}

pub(super) fn url_owner(initial: &[u8]) -> (Box<Keyboard<Platform>>, Rc<Facts>) {
    let f = Rc::new(Facts::default());
    (
        Keyboard::try_new(Platform(f.clone()), b"URL", initial, 0, 2).unwrap(),
        f,
    )
}
fn point(k: &Keyboard<Platform>, value: i16) -> (i16, i16) {
    k.host.with_layouts(|layouts| {
        k.model(|p| {
            let mut keys = [xp_ui::keyboard_view::PlacedKey::default(); 16];
            for row in 0..5 {
                let n =
                    xp_ui::keyboard_view::row_keys(&p.state, layouts, &p.layout, row, &mut keys);
                for key in &keys[..n as usize] {
                    if key.value == value {
                        return (key.hit.x + key.hit.w / 2, key.hit.y + key.hit.h / 2);
                    }
                }
            }
            panic!("missing key")
        })
    })
}
fn touch(k: &Keyboard<Platform>, point: (i16, i16), flags: u8, now: u32) {
    k.input(
        Input {
            touch_x: point.0,
            touch_y: point.1,
            touch_flags: flags,
            now_ms: now,
            ..Input::default()
        },
        None,
    );
}
pub(super) fn press_value(k: &Keyboard<Platform>, value: i16) {
    let p = point(k, value);
    touch(k, p, model::TOUCH_PRESSED | model::TOUCH_HELD, 0);
    touch(k, p, model::TOUCH_RELEASED, 100);
}
#[test]
fn owned_result_moves_out_and_survives_child_destruction() {
    for initial in [b"".as_slice(), b"max length"] {
        let (k, _) = url_owner(initial);
        assert!(k.render());
        press_value(&k, 13);
        let original = k.with_text(|_, b| b.as_ptr());
        let result = k.take_result().unwrap();
        assert!(!result.is_cancelled());
        assert_eq!(result.text(), Some(initial));
        assert_eq!(result.text().unwrap().as_ptr(), original);
        k.exit();
        drop(k);
        assert_eq!(result.text(), Some(initial));
        drop(result);
    }
}
#[test]
fn pending_url_contact_uses_painted_layout_until_publication() {
    let (k, _) = url_owner(b"pending URL");
    assert!(k.render());
    let old_url = (303, 736);
    press_value(&k, -3);
    k.with_text(|s, _| assert_eq!(s.url_panel, 1));
    touch(&k, old_url, model::TOUCH_PRESSED | model::TOUCH_HELD, 200);
    assert!(k.render());
    touch(&k, old_url, model::TOUCH_RELEASED, 300);
    k.with_text(|s, b| {
        assert_eq!(s.url_panel, 1);
        assert_eq!(b, b"pending URL");
        assert_eq!(s.active_key, i16::MIN);
        assert_eq!(s.active_index, -1);
    });
    assert!(k.take_result().is_none());
}
#[test]
fn contact_spanning_shorter_publication_does_not_accept_stale_enter() {
    let (k, _) = url_owner(b"held Enter");
    assert!(k.render());
    assert!(k.render());
    touch(
        &k,
        point(&k, 13),
        model::TOUCH_PRESSED | model::TOUCH_HELD,
        0,
    );
    // Hardware selects URL without consuming the existing touch contact.
    {
        let _guard = super::Guard::new(&k.host, Lock::Text);
        k.host
            .with_layouts(|l| unsafe { (*k.edit.get()).state.sync(l, -3) });
    }
    k.input(
        Input {
            pressed: model::CONFIRM,
            held: model::CONFIRM,
            touch_flags: model::TOUCH_HELD,
            now_ms: 20,
            ..Input::default()
        },
        None,
    );
    k.input(
        Input {
            released: model::CONFIRM,
            touch_flags: model::TOUCH_HELD,
            now_ms: 40,
            ..Input::default()
        },
        None,
    );
    assert!(k.render());
    touch(&k, (0, 0), model::TOUCH_RELEASED, 100);
    k.with_text(|s, b| {
        assert_eq!(s.url_panel, 1);
        assert_eq!(s.active_key, i16::MIN);
        assert_eq!(s.active_index, -1);
        assert_eq!(b, b"held Enter");
    });
    assert!(k.take_result().is_none());
}
#[test]
fn password_key_tap_has_no_touch_scratch_and_unmask_releases_display() {
    let f = Rc::new(Facts::default());
    let k = Keyboard::try_new(Platform(f.clone()), b"Password", b"abc", 0, 1).unwrap();
    assert!(k.render());
    EVENTS.with(|x| x.borrow_mut().clear());
    press_value(&k, 1000);
    EVENTS.with(|x| {
        assert_eq!(
            x.borrow()
                .iter()
                .filter(|e| e.0)
                .map(|e| e.1)
                .collect::<Vec<_>>(),
            [8]
        )
    });
    {
        let _guard = super::Guard::new(&k.host, Lock::Text);
        unsafe {
            (*k.edit.get()).state.password_visible = 1;
        }
    }
    assert!(k.render());
    assert_eq!(unsafe { (*k.render.get()).display.capacity() }, 0);
}

#[test]
fn exited_owner_rejects_render_without_borrowing_released_text() {
    let (k, _) = owner(b"closed", 0);
    assert!(k.render());
    k.exit();
    assert!(!k.render());
    assert!(!k.ready());
}

impl Keyboard<Platform> {
    fn with_text<R>(&self, consume: impl FnOnce(&xp_ui::keyboard::State, &[u8]) -> R) -> R {
        let _guard = super::Guard::new(&self.host, Lock::Text);
        let edit = unsafe { &*self.edit.get() };
        consume(
            &edit.state,
            &edit.text.as_slice()[..edit.state.length as usize],
        )
    }
}

#[test]
fn deferred_entry_keeps_resources_empty_until_initialize() {
    let facts = Rc::new(Facts::default());
    let keyboard = Keyboard::try_shell(Platform(facts.clone())).unwrap();
    assert!(!keyboard.has_result());
    assert!(!keyboard.render());
    keyboard.input(
        Input {
            released: model::BACK,
            ..Input::default()
        },
        None,
    );
    assert!(!keyboard.has_result());
    assert!(unsafe { keyboard.initialize(b"Title", b"initial", 0, 0) });
    assert!(keyboard.render());
    keyboard.input(Input::default(), None);
    keyboard.input(
        Input {
            released: model::BACK,
            ..Input::default()
        },
        None,
    );
    assert!(keyboard.has_result());
    assert!(keyboard.take_result().unwrap().is_cancelled());
    keyboard.exit();
}

#[test]
fn deferred_entry_failure_can_exit_without_a_result_or_double_free() {
    for fail_at in 0..2 {
        let facts = Rc::new(Facts::default());
        let keyboard = Keyboard::try_shell(Platform(facts.clone())).unwrap();
        FAIL_AT.with(|f| f.set(fail_at));
        assert!(!unsafe { keyboard.initialize(b"Title", b"initial", 0, 0) });
        FAIL_AT.with(|f| f.set(usize::MAX));
        assert!(!keyboard.has_result());
        keyboard.exit();
        drop(keyboard);
        assert_eq!(
            facts.errors.borrow().last(),
            Some(&"Cannot allocate keyboard scene")
        );
    }
}
