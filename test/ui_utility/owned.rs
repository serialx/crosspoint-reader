use std::{
    alloc::{alloc_zeroed, dealloc, Layout as Allocation},
    cell::{Cell, RefCell},
    rc::Rc,
};
use xp_ui::{
    model::{self, Input, Metrics},
    scene::{Command, Rect, Refresh, Scene},
    utility::{self, owner::*},
};

thread_local! {
    static FAIL: Cell<usize> = const {Cell::new(usize::MAX)};
    static REQUESTS: RefCell<Vec<(bool, usize)>> = const {RefCell::new(Vec::new())};
}
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(l: Allocation) -> *mut u8 {
        REQUESTS.with(|r| r.borrow_mut().push((true, l.size())));
        if FAIL.with(|f| {
            let n = f.get();
            f.set(n.saturating_sub(1));
            n == 0
        }) {
            std::ptr::null_mut()
        } else {
            unsafe { alloc_zeroed(l) }
        }
    }
    unsafe fn release(p: *mut u8, l: Allocation) {
        REQUESTS.with(|r| r.borrow_mut().push((false, l.size())));
        unsafe { dealloc(p, l) }
    }
    fn owner(l: Allocation) -> *mut u8 {
        Self::allocate(l)
    }
}
#[derive(Default)]
struct Facts {
    locked: Cell<bool>,
    fail: Cell<bool>,
    refresh: Cell<usize>,
    finish: Cell<usize>,
    requests: Cell<usize>,
    log: RefCell<Vec<Log>>,
    hits: RefCell<Vec<xp_ui::scene::Interaction>>,
    commands: RefCell<Vec<Command>>,
    texts: RefCell<Vec<(u32, Vec<u8>)>>,
    touch: Cell<bool>,
    translation: RefCell<Vec<u8>>,
    on_update: RefCell<Option<Box<dyn Fn()>>>,
}
struct Platform(Rc<Facts>);
impl Metrics for Platform {
    fn width(&mut self, _: i32, text: &[u8], _: u8) -> i32 {
        text.len() as i32 * 8
    }
    fn line_height(&mut self, _: i32) -> i32 {
        20
    }
}
unsafe impl Host for Platform {
    type Memory = Heap;
    fn lock(&self) {
        assert!(!self.0.locked.replace(true));
    }
    fn unlock(&self) {
        assert!(self.0.locked.replace(false));
    }
    fn layout(&self) -> utility::Layout {
        utility::Layout {
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
            popup_padding: 16,
            popup_gap: 8,
            popup_side_margin: 20,
            row_height: 40,
            min_touch: 40,
            space_sm: 4,
            space_md: 8,
            space_lg: 16,
            hints_height: 40,
            touch: self.0.touch.get() as u8,
            ..Default::default()
        }
    }
    fn with_translation<R>(&self, _: u16, f: impl FnOnce(&[u8]) -> R) -> R {
        let text = self.0.translation.borrow();
        f(if text.is_empty() { b"Label" } else { &text })
    }
    fn format_number(&self, _: u16, value: i32, unsigned: bool, out: &mut [u8]) {
        let s = if unsigned {
            (value as u32).to_string()
        } else {
            value.to_string()
        };
        let n = s.len().min(out.len() - 1);
        out[..n].copy_from_slice(&s.as_bytes()[..n]);
        out[n] = 0;
    }
    fn with_metrics<R>(&self, f: impl FnOnce(&mut dyn Metrics) -> R) -> R {
        f(&mut Platform(self.0.clone()))
    }
    fn text_height(&self, text: &[u8], _: i32, _: i16, _: u8, _: u8) -> i16 {
        if text.is_empty() {
            0
        } else {
            20
        }
    }
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool {
        assert!(!self.0.locked.get());
        if self.0.fail.replace(false) {
            return false;
        }
        if scene.validate(|h, g| resources.valid(h, g)).is_err() {
            return false;
        }
        *self.0.hits.borrow_mut() = scene.interactions.to_vec();
        *self.0.commands.borrow_mut() = scene.commands.to_vec();
        for c in scene.commands {
            if c.resource != 0 {
                resources.paint(
                    c,
                    Rect {
                        x: 0,
                        y: 0,
                        w: 528,
                        h: 792,
                    },
                );
            }
        }
        true
    }
    fn text(&self, c: &Command, _: Rect, text: &[u8]) {
        self.0.texts.borrow_mut().push((c.resource, text.to_vec()));
    }
    fn footer(&self, _: &Command, _: Rect, _: [Label; 4]) {}
    fn refresh(&self, _: Refresh) {
        assert!(!self.0.locked.get());
        self.0.refresh.set(self.0.refresh.get() + 1);
    }
    fn request_update(&self, _: bool) {
        self.0.requests.set(self.0.requests.get() + 1);
        if let Some(hook) = self.0.on_update.borrow().as_ref() {
            hook();
        }
    }
    fn haptic(&self) {}
    fn finish(&self) {
        self.0.finish.set(self.0.finish.get() + 1);
    }
    fn log(&self, l: Log) {
        self.0.log.borrow_mut().push(l);
    }
}
fn platform() -> (Platform, Rc<Facts>) {
    FAIL.with(|f| f.set(usize::MAX));
    REQUESTS.with(|r| r.borrow_mut().clear());
    let f = Rc::new(Facts::default());
    (Platform(f.clone()), f)
}
fn interval() -> Interval {
    Interval {
        title: 1,
        format: None,
        boundary: None,
        initial: 20,
        minimum: 1,
        maximum: 100,
        small_step: 1,
        large_step: 10,
        edge_buttons: false,
        reader: false,
    }
}
fn release(button: u32) -> Input {
    Input {
        released: button,
        ..Default::default()
    }
}

#[test]
fn confirmation_confirm_cancel_and_one_shot_result() {
    for confirm in [false, true] {
        let (h, f) = platform();
        let d = Dialog::confirmation(h, b"Heading", b"Body", b"", b"").unwrap();
        assert!(d.enter());
        assert!(d.render());
        if confirm {
            d.input(Input {
                pressed: model::NAV_NEXT,
                ..Default::default()
            });
        }
        d.input(release(if confirm { model::CONFIRM } else { model::BACK }));
        d.input(Input::default());
        assert_eq!(
            d.take_result(),
            Some(if confirm {
                ConfirmationResult::Confirmed
            } else {
                ConfirmationResult::Cancelled
            })
        );
        assert_eq!(d.take_result(), None);
        assert_eq!(f.finish.get(), 1);
        d.exit();
    }
}
#[test]
fn interval_value_cancel_and_fixed_arena() {
    for cancel in [false, true] {
        let (h, f) = platform();
        let d = Dialog::interval(h, interval()).unwrap();
        assert!(d.enter());
        assert!(d.render());
        d.input(Input {
            pressed: model::RIGHT,
            ..Default::default()
        });
        d.input(release(if cancel { model::BACK } else { model::CONFIRM }));
        assert_eq!(
            d.take_result(),
            Some(if cancel {
                IntervalResult::Cancelled
            } else {
                IntervalResult::Interval(21)
            })
        );
        d.exit();
        assert_eq!(f.finish.get(), 1);
        REQUESTS.with(|r| {
            let r = r.borrow();
            assert_eq!(r.iter().filter(|e| **e == (true, 816)).count(), 1);
            assert_eq!(r.iter().filter(|e| **e == (false, 816)).count(), 1);
        });
    }
}
#[test]
fn failed_paint_has_no_publication_and_held_release_is_suppressed() {
    let (h, f) = platform();
    let d = Dialog::confirmation(h, b"H", b"B", b"", b"").unwrap();
    assert!(d.enter());
    f.fail.set(true);
    assert!(!d.render());
    assert!(!d.ready());
    assert_eq!(f.refresh.get(), 0);
    d.input(Input {
        held: model::CONFIRM,
        ..Default::default()
    });
    assert!(d.render());
    assert!(d.ready());
    d.input(release(model::CONFIRM));
    assert_eq!(d.take_result(), None);
    d.input(release(model::BACK));
    d.input(Input::default());
    assert_eq!(d.take_result(), Some(ConfirmationResult::Cancelled));
}
#[test]
fn failed_interval_paint_can_cancel_before_first_publication() {
    let (h, f) = platform();
    let d = Dialog::interval(h, interval()).unwrap();
    assert!(d.enter());
    f.fail.set(true);
    assert!(!d.render());
    d.input(release(model::BACK));
    assert_eq!(d.take_result(), Some(IntervalResult::Cancelled));
}
#[test]
fn owner_and_arena_failures_drop_cleanly() {
    for slot in 0..4 {
        let (h, _) = platform();
        FAIL.with(|f| f.set(slot));
        if let Some(d) = Dialog::confirmation(h, b"H", b"B", b"", b"") {
            assert!(!d.enter());
            assert_eq!(d.take_result(), Some(ConfirmationResult::Cancelled));
        }
    }
    let (h, _) = platform();
    FAIL.with(|f| f.set(0));
    assert!(Dialog::interval(h, interval()).is_none());
    let (h, _) = platform();
    FAIL.with(|f| f.set(0));
    assert!(Message::try_new(h, b"Message", 0, Refresh::Fast).is_none());
}
#[test]
fn message_is_synchronous_and_only_external_exit_returns_a_result() {
    let (h, f) = platform();
    let d = Message::try_new(h, b"Message", 0, Refresh::Half).unwrap();
    assert!(d.enter(0));
    assert_eq!(f.refresh.get(), 1);
    assert_eq!(f.commands.borrow().len(), 2);
    assert_eq!(d.take_result(), None);
    d.input(release(model::BACK));
    d.input(release(model::CONFIRM));
    d.render();
    assert_eq!(d.take_result(), None);
    assert_eq!(f.finish.get(), 0);
    assert_eq!(f.refresh.get(), 1);
    d.exit();
    assert_eq!(d.take_result(), Some(MessageResult::MessageDismissed));
    assert_eq!(d.take_result(), None);
    d.exit();
    assert_eq!(d.take_result(), None);
}
#[test]
fn message_failed_paint_recovery_requires_explicit_entry() {
    let (h, f) = platform();
    let d = Message::try_new(h, b"Message", 0, Refresh::Fast).unwrap();
    f.fail.set(true);
    assert!(!d.enter(0));
    assert_eq!(f.refresh.get(), 0);
    d.render();
    assert_eq!(f.requests.get(), 0);
    assert!(d.enter(0));
    assert_eq!(f.refresh.get(), 1);
    d.exit();
}
#[test]
fn exited_dialog_cannot_borrow_freed_arena() {
    let (h, _) = platform();
    let d = Dialog::interval(h, interval()).unwrap();
    assert!(d.enter());
    assert!(d.render());
    d.exit();
    assert!(!d.render());
    d.input(release(model::CONFIRM));
    assert_eq!(d.take_result(), None);
}

#[test]
fn owned_scenes_match_existing_checked_native_view() {
    use std::ffi::{c_char, c_void};
    use xp_ui::ffi::{Buffer, Metrics as WireMetrics, SceneView};
    #[repr(C)]
    struct Texts {
        context: *mut c_void,
        text: unsafe extern "C" fn(*mut c_void, u32) -> *const c_char,
        height: unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8, u8) -> i16,
    }
    unsafe extern "C" {
        fn xp_ui_utility_view(
            s: *const utility::State,
            l: *const utility::Layout,
            m: *const WireMetrics,
            t: *const Texts,
            b: *const Buffer,
            f: *mut utility::Feedback,
            out: *mut SceneView,
        ) -> u8;
    }
    unsafe extern "C" fn text(_: *mut c_void, h: u32) -> *const c_char {
        match h {
            1 => c"Heading",
            2 => c"Body",
            3 | 4 => c"Label",
            5 => c"20",
            6 => c"1",
            7 => c"100",
            8 => c"Label 1",
            9 => c"Label 10",
            26 => c"-",
            27 => c"+",
            _ => c"",
        }
        .as_ptr()
    }
    unsafe extern "C" fn height(_: *mut c_void, _: u32, _: i32, _: i16, _: u8, _: u8) -> i16 {
        20
    }
    unsafe extern "C" fn width(_: *mut c_void, _: i32, _: *const u8, n: u16, _: u8) -> i32 {
        i32::from(n) * 8
    }
    unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
        20
    }
    for touch in [false, true] {
        for is_interval in [false, true] {
            let (h, f) = platform();
            f.touch.set(touch);
            let layout = h.layout();
            if is_interval {
                let d = Dialog::interval(h, interval()).unwrap();
                assert!(d.enter());
                assert!(d.render());
            } else {
                let d = Dialog::confirmation(h, b"Heading", b"Body", b"", b"").unwrap();
                assert!(d.enter());
                assert!(d.render());
            }
            let mut state = if is_interval {
                interval().state()
            } else {
                utility::State::default()
            };
            assert!(state.init());
            let mut commands = [Command::default(); 18];
            let mut hits = [xp_ui::scene::Interaction::default(); 5];
            let buffer = Buffer {
                commands: commands.as_mut_ptr(),
                hits: hits.as_mut_ptr(),
                command_capacity: if is_interval { 18 } else { 10 },
                hit_capacity: if is_interval { 5 } else { 3 },
            };
            let metrics = WireMetrics {
                context: std::ptr::null_mut(),
                width: Some(width),
                line_height: Some(line),
            };
            let texts = Texts {
                context: std::ptr::null_mut(),
                text,
                height,
            };
            let mut feedback = utility::Feedback::default();
            let mut scene: SceneView = unsafe { std::mem::zeroed() };
            assert_eq!(
                unsafe {
                    xp_ui_utility_view(
                        &state,
                        &layout,
                        &metrics,
                        &texts,
                        &buffer,
                        &mut feedback,
                        &mut scene,
                    )
                },
                1
            );
            assert_eq!(
                *f.commands.borrow(),
                commands[..scene.command_count as usize]
            );
            assert_eq!(*f.hits.borrow(), hits[..scene.interaction_count as usize]);
        }
    }
}

#[test]
fn repeated_entry_preserves_allocate_before_release_and_failure_logs() {
    let (h, f) = platform();
    let d = Dialog::interval(h, interval()).unwrap();
    assert!(d.enter());
    assert!(d.render());
    REQUESTS.with(|r| r.borrow_mut().clear());
    assert!(d.enter());
    REQUESTS.with(|r| assert_eq!(*r.borrow(), [(true, 816), (false, 816)]));
    FAIL.with(|n| n.set(0));
    assert!(!d.enter());
    assert!(!d.ready());
    assert_eq!(d.take_result(), Some(IntervalResult::Cancelled));
    assert_eq!(
        &f.log.borrow()[f.log.borrow().len() - 2..],
        [Log::Enter, Log::Arena(816)]
    );
    d.exit();
}

#[test]
fn message_commands_match_native_coordinate_oracle() {
    unsafe extern "C" {
        fn xp_ui_message_view(
            m: *const xp_ui::ffi::Metrics,
            s: Rect,
            font: i32,
            style: u8,
            text: *const u8,
            n: u16,
            out: *mut Command,
            capacity: u16,
        ) -> u8;
    }
    unsafe extern "C" fn width(
        _: *mut std::ffi::c_void,
        _: i32,
        _: *const u8,
        n: u16,
        _: u8,
    ) -> i32 {
        i32::from(n) * 8
    }
    unsafe extern "C" fn line(_: *mut std::ffi::c_void, _: i32) -> i32 {
        20
    }
    for style in 0..4 {
        let (h, f) = platform();
        let screen = h.layout().screen;
        let d = Message::try_new(h, b"Message", style, Refresh::Fast).unwrap();
        assert!(d.enter(0));
        let metrics = xp_ui::ffi::Metrics {
            context: std::ptr::null_mut(),
            width: Some(width),
            line_height: Some(line),
        };
        let mut commands = [Command::default(); 2];
        assert_eq!(
            unsafe {
                xp_ui_message_view(
                    &metrics,
                    screen,
                    0,
                    style,
                    c"Message".as_ptr().cast(),
                    7,
                    commands.as_mut_ptr(),
                    2,
                )
            },
            2
        );
        assert_eq!(*f.commands.borrow(), commands);
    }
}

#[path = "owned_lifecycle.rs"]
mod lifecycle;

#[test]
fn interval_labels_keep_native_byte_truncation_and_boundary_choice() {
    let (h, f) = platform();
    *f.translation.borrow_mut() = vec![b'x'; 100];
    let mut spec = interval();
    spec.initial = 100;
    spec.boundary = Some(1);
    spec.reader = true;
    let d = Dialog::interval(h, spec).unwrap();
    assert!(d.is_reader());
    assert!(d.enter());
    assert!(d.render());
    let texts = f.texts.borrow();
    for (handle, length) in [
        (utility::READOUT, 63),
        (utility::MAXIMUM, 31),
        (utility::HINT1, 63),
        (utility::HINT2, 63),
    ] {
        assert_eq!(
            texts.iter().find(|(h, _)| *h == handle).unwrap().1,
            vec![b'x'; length]
        );
    }
}
#[test]
fn invalid_interval_and_message_inputs_preserve_native_failure_kind() {
    let (h, f) = platform();
    let mut spec = interval();
    spec.minimum = 101;
    let d = Dialog::interval(h, spec).unwrap();
    assert!(!d.enter());
    assert_eq!(*f.log.borrow(), [Log::Enter, Log::Arena(816)]);
    assert_eq!(d.take_result(), Some(IntervalResult::Cancelled));
    for text in [vec![b'x'; 4096], b"embedded\0nul".to_vec()] {
        let (h, f) = platform();
        let d = Message::try_new(h, &text, 0, Refresh::Fast).unwrap();
        assert!(!d.enter(0));
        assert_eq!(*f.log.borrow(), [Log::Enter, Log::MessageBuild]);
        assert_eq!(f.refresh.get(), 0);
    }
    let (h, _) = platform();
    FAIL.with(|f| f.set(1));
    assert!(Message::try_new(h, b"text", 0, Refresh::Fast).is_none());
}

#[test]
fn paint_rejection_between_invalidation_and_effect_dispatch_discards_pending_result() {
    let (h, f) = platform();
    f.touch.set(true);
    let d = Dialog::interval(h, interval()).unwrap();
    assert!(d.enter());
    assert!(d.render());
    let hit = *f
        .hits
        .borrow()
        .iter()
        .find(|h| h.action == utility::OK)
        .unwrap();
    let slot = Rc::new(RefCell::new(Some(d)));
    let weak = Rc::downgrade(&slot);
    let facts = Rc::downgrade(&f);
    *f.on_update.borrow_mut() = Some(Box::new(move || {
        facts.upgrade().unwrap().fail.set(true);
        assert!(!weak.upgrade().unwrap().borrow().as_ref().unwrap().render());
    }));
    slot.borrow().as_ref().unwrap().input(Input {
        touch_flags: model::TOUCH_RELEASED,
        touch_x: hit.rect.x + hit.rect.w / 2,
        touch_y: hit.rect.y + hit.rect.h / 2,
        ..Default::default()
    });
    assert!(!slot.borrow().as_ref().unwrap().ready());
    assert_eq!(slot.borrow().as_ref().unwrap().take_result(), None);
    assert_eq!(f.finish.get(), 0);
    *f.on_update.borrow_mut() = None;
}
