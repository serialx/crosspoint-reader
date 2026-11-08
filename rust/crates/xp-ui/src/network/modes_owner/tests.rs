extern crate std;
use super::*;
use crate::network::stable_owner::StableOwner;
use crate::{
    list::Style,
    scene::{Error, Rect},
    settings::view::Row,
};
use std::{
    cell::{Cell, RefCell},
    rc::Rc,
    vec,
    vec::Vec,
};

std::thread_local! {
    static FAIL_OWNER: Cell<bool> = const { Cell::new(false) };
    static ALLOCATIONS: Cell<usize> = const { Cell::new(0) };
}
#[derive(Clone, Debug, PartialEq)]
enum Event {
    Enter,
    Exit,
    Update,
    Paint,
    Refresh,
    Haptic,
    Complete(bool, u8),
    Scroll(i32),
    Scene(bool),
    Drop,
}
struct Record {
    events: RefCell<Vec<Event>>,
    commands: RefCell<Vec<Command>>,
    locked: Cell<bool>,
    count: u16,
    layout: Cell<Layout>,
    build_ok: Cell<bool>,
    paint_ok: Cell<bool>,
    target: Cell<*const Modes<TestHost>>,
    during_paint: Cell<Option<Input>>,
    during_refresh: Cell<Option<Input>>,
}
impl Record {
    fn event(&self, event: Event) {
        assert!(!self.locked.get(), "physical operation under ModelLock");
        self.events.borrow_mut().push(event);
    }
    fn reenter(&self, input: Option<Input>) {
        if let Some(input) = input {
            unsafe { &*self.target.get() }.input(input);
        }
    }
}
struct TestHost(Rc<Record>);
impl Drop for TestHost {
    fn drop(&mut self) {
        self.0.event(Event::Drop);
    }
}
struct Metrics(bool);
impl Resources for Metrics {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, i: u16) -> Result<Row, Error> {
        if !self.0 {
            return Err(Error::Invalid);
        }
        let h = u32::from(i) * 4 + 1;
        Ok(Row {
            label: h,
            subtitle: h + 1,
            icon: h + 3,
            enabled: 1,
            ..Row::default()
        })
    }
    fn width(&mut self, _: u32, _: i32, _: u8) -> Result<i32, Error> {
        Ok(60)
    }
    fn height(&mut self, _: u32, _: i32, _: u8, _: u8, _: i16) -> Result<i32, Error> {
        Ok(24)
    }
    fn line(&mut self, _: i32) -> Result<i32, Error> {
        Ok(24)
    }
}
unsafe impl Host for TestHost {
    fn allocate_owner(layout: Allocation) -> *mut u8 {
        ALLOCATIONS.with(|n| n.set(n.get() + 1));
        if FAIL_OWNER.with(|fail| fail.replace(false)) {
            ptr::null_mut()
        } else {
            unsafe { alloc::alloc::alloc_zeroed(layout) }
        }
    }
    fn lock(&self) {
        assert!(!self.0.locked.replace(true));
    }
    fn unlock(&self) {
        assert!(self.0.locked.replace(false));
    }
    fn enter(&self) {
        self.0.event(Event::Enter);
    }
    fn exit(&self) {
        self.0.event(Event::Exit);
    }
    fn count(&self) -> u16 {
        self.0.count
    }
    fn layout(&self) -> Layout {
        assert!(!self.0.locked.get());
        self.0.layout.get()
    }
    fn with_resources<R>(&self, f: impl FnOnce(&mut dyn Resources) -> R) -> R {
        f(&mut Metrics(self.0.build_ok.get()))
    }
    fn paint(&self, scene: &Scene<'_>) -> bool {
        self.0.event(Event::Paint);
        *self.0.commands.borrow_mut() = scene.commands.to_vec();
        self.0.reenter(self.0.during_paint.take());
        self.0.paint_ok.get()
    }
    fn refresh(&self, _: Refresh) {
        self.0.event(Event::Refresh);
        self.0.reenter(self.0.during_refresh.take());
    }
    fn request_update(&self) {
        self.0.event(Event::Update);
    }
    fn haptic(&self) {
        self.0.event(Event::Haptic);
    }
    fn complete(&self, cancel: bool, mode: u8) {
        self.0.event(Event::Complete(cancel, mode));
    }
    fn scroll_diagnostic(&self, delta: i32) {
        self.0.event(Event::Scroll(delta));
    }
    fn scene_diagnostic(&self, invalid: bool) {
        self.0.event(Event::Scene(invalid));
    }
}
fn record(count: u16) -> Rc<Record> {
    let screen = Rect {
        x: 0,
        y: 0,
        w: 528,
        h: 792,
    };
    Rc::new(Record {
        events: RefCell::new(Vec::new()),
        commands: RefCell::new(Vec::new()),
        locked: Cell::new(false),
        count,
        layout: Cell::new(Layout {
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
        }),
        build_ok: Cell::new(true),
        paint_ok: Cell::new(true),
        target: Cell::new(ptr::null()),
        during_paint: Cell::new(None),
        during_refresh: Cell::new(None),
    })
}
fn owner(count: u16) -> (StableOwner<Modes<TestHost>>, Rc<Record>) {
    let r = record(count);
    let o = StableOwner::from(Modes::try_new(TestHost(r.clone())).unwrap());
    r.target.set(o.pointer().as_ptr());
    o.enter();
    (o, r)
}
fn input(released: u32) -> Input {
    Input {
        released,
        ..Input::default()
    }
}
fn completions(r: &Record) -> Vec<Event> {
    r.events
        .borrow()
        .iter()
        .filter(|e| matches!(e, Event::Complete(..)))
        .cloned()
        .collect()
}
#[test]
fn constructor_failure_and_single_allocation() {
    ALLOCATIONS.with(|n| n.set(0));
    FAIL_OWNER.with(|f| f.set(true));
    let r = record(3);
    assert!(Modes::try_new(TestHost(r.clone())).is_none());
    assert_eq!(*r.events.borrow(), vec![Event::Drop]);
    let (o, r) = owner(3);
    for _ in 0..3 {
        assert!(o.render());
    }
    o.exit();
    drop(o);
    assert_eq!(
        r.events
            .borrow()
            .iter()
            .filter(|e| **e == Event::Drop)
            .count(),
        1
    );
    ALLOCATIONS.with(|n| assert_eq!(n.get(), 2));
}
#[test]
fn confirm_cancel_and_native_haptic_counts() {
    for count in [3, 4] {
        let (o, r) = owner(count);
        o.input(input(model::CONFIRM));
        assert_eq!(completions(&r), vec![Event::Complete(false, 0)]);
        let (o, r) = owner(count);
        o.input(input(model::BACK));
        assert_eq!(completions(&r), vec![Event::Complete(true, 0)]);
        for synthesized in [false, true] {
            let (o, r) = owner(count);
            assert!(o.render());
            let hit = o.model(|m| m.hits.first_row);
            o.input(Input {
                touch_flags: model::TOUCH_RELEASED,
                touch_x: hit.x + 1,
                touch_y: hit.y + 1,
                action: if synthesized { 1 } else { 0 },
                ..Input::default()
            });
            assert_eq!(completions(&r), vec![Event::Complete(false, 0)]);
            assert_eq!(
                r.events
                    .borrow()
                    .iter()
                    .filter(|e| **e == Event::Haptic)
                    .count(),
                if synthesized { 2 } else { 1 }
            );
        }
    }
}
#[test]
fn rejection_suppresses_held_edges_and_keeps_back() {
    for fail_build in [false, true] {
        let (o, r) = owner(3);
        assert!(o.render());
        r.build_ok.set(!fail_build);
        r.paint_ok.set(fail_build);
        assert!(!o.render());
        assert!(o.model(|m| m.gate.failed() && !m.ready));
        o.input(Input {
            held: model::CONFIRM,
            touch_flags: model::TOUCH_HELD,
            ..input(model::CONFIRM)
        });
        assert!(completions(&r).is_empty());
        o.input(input(model::BACK));
        assert_eq!(completions(&r), vec![Event::Complete(true, 0)]);
        r.events.borrow_mut().clear();
        o.input(Input {
            held: model::CONFIRM,
            touch_flags: model::TOUCH_PRESSED,
            ..Input::default()
        });
        r.build_ok.set(true);
        r.paint_ok.set(true);
        assert!(o.render());
        o.input(Input {
            touch_flags: model::TOUCH_RELEASED,
            ..input(model::CONFIRM)
        });
        assert!(completions(&r).is_empty());
        o.input(input(model::CONFIRM));
        assert_eq!(completions(&r), vec![Event::Complete(false, 0)]);
        let logs: Vec<_> = r
            .events
            .borrow()
            .iter()
            .filter(|e| matches!(e, Event::Scene(_)))
            .cloned()
            .collect();
        assert!(logs.is_empty());
    }
}
#[test]
fn stale_render_keeps_input_and_requests_repaint() {
    let (o, r) = owner(3);
    assert!(o.render());
    r.events.borrow_mut().clear();
    r.during_paint.set(Some(Input {
        pressed: model::NAV_NEXT,
        held: model::NAV_NEXT,
        now_ms: 100,
        ..Input::default()
    }));
    assert!(o.render());
    assert!(o.model(|m| m.state.selected == 1 && !m.ready && !m.gate.failed()));
    assert_eq!(r.events.borrow().last(), Some(&Event::Refresh));
    assert!(r.events.borrow().contains(&Event::Update));
    assert!(o.render());
    o.input(input(model::CONFIRM));
    assert_eq!(completions(&r), vec![Event::Complete(false, 1)]);
}
#[test]
fn refresh_does_not_hold_model_lock() {
    let (o, r) = owner(3);
    r.during_refresh.set(Some(input(model::CONFIRM)));
    assert!(o.render());
    assert_eq!(completions(&r), vec![Event::Complete(false, 0)]);
    let events = r.events.borrow();
    assert!(
        events.iter().position(|e| *e == Event::Refresh).unwrap()
            < events
                .iter()
                .position(|e| matches!(e, Event::Complete(..)))
                .unwrap()
    );
}
#[test]
fn stable_target_survives_owner_moves_and_drops_once() {
    let (o, r) = owner(3);
    let target = o.pointer();
    let mut parents = vec![o];
    unsafe { target.as_ref() }.render();
    let o = parents.pop().unwrap();
    o.exit();
    drop(o);
    assert_eq!(
        r.events
            .borrow()
            .iter()
            .filter(|e| **e == Event::Drop)
            .count(),
        1
    );
}
#[test]
fn all_mode_geometry_fits_native_inline_capacity() {
    let mut maximum = 0;
    for count in [3, 4] {
        for touch in [0, 1] {
            for (w, h) in [(528, 792), (792, 528), (480, 800), (800, 480)] {
                for selected in 0..count {
                    for selection in 0..4 {
                        let (o, r) = owner(count);
                        let mut l = r.layout.get();
                        l.list.screen.w = w;
                        l.list.screen.h = h;
                        l.list.area = l.list.screen;
                        l.touch = touch;
                        l.list.selection = selection;
                        r.layout.set(l);
                        o.model(|m| m.state = ListState::new(i32::from(selected)));
                        assert!(o.render());
                        maximum = maximum.max(r.commands.borrow().len());
                        assert!(o.model(|m| m.ready));
                    }
                }
            }
        }
    }
    assert!(maximum <= 24);
    assert!(maximum > 16);
    let mut short = [Command::default(); 1];
    let mut hits = hits::Layout::default();
    assert!(matches!(
        super::super::modes::build(
            &mut short,
            &mut hits,
            ListState::new(0),
            record(3).layout.get(),
            &mut Metrics(true),
            3
        ),
        Err(Error::Capacity)
    ));
}

#[test]
#[ignore = "strict Miri negative control: must report use after free"]
fn negative_destroyed_target() {
    let (o, _r) = owner(3);
    let target = o.pointer();
    drop(o);
    unsafe { target.as_ref() }.input(input(model::CONFIRM));
}
#[test]
#[ignore = "strict Miri negative control: must reject overlapping model loans"]
fn negative_overlapping_model_loan() {
    let (o, _r) = owner(3);
    let loan = unsafe { &mut *o.model.get() };
    o.input(input(model::CONFIRM));
    loan.state.selected = 2;
}
