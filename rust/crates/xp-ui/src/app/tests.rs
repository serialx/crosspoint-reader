extern crate std;

use super::*;
use std::{rc::Rc, vec, vec::Vec};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Event {
    Enter(u8),
    Exit(u8),
    Drop(u8),
    Result(u8),
    Deliver(u8, u8),
    Publish(Option<u8>, u32),
    Clear,
    Update,
    Missing,
    Oom,
    Prepare(u8),
}

type Log = Rc<RefCell<Vec<Event>>>;
struct Owner {
    id: u8,
    log: Log,
}
impl Activity for Owner {
    type Target = u8;
    fn target(&self) -> u8 {
        self.id
    }
    fn is_reader(&self) -> bool {
        self.id == 1
    }
}
impl Drop for Owner {
    fn drop(&mut self) {
        self.log.borrow_mut().push(Event::Drop(self.id));
    }
}

struct Guard(Rc<Cell<bool>>);
impl Drop for Guard {
    fn drop(&mut self) {
        assert!(self.0.replace(false));
    }
}

struct Host<'a> {
    app: &'a Application<Owner>,
    log: Log,
    locked: Rc<Cell<bool>>,
    radio_ready: Cell<bool>,
    enter_push: Cell<Option<u8>>,
    result_push: Cell<Option<u8>>,
}
impl<'a> Host<'a> {
    fn new(app: &'a Application<Owner>) -> Self {
        Self {
            app,
            log: Rc::new(RefCell::new(Vec::new())),
            locked: Rc::new(Cell::new(false)),
            radio_ready: Cell::new(true),
            enter_push: Cell::new(None),
            result_push: Cell::new(None),
        }
    }
    fn owner(&self, id: u8) -> Owner {
        Owner {
            id,
            log: self.log.clone(),
        }
    }
    fn record(&self, event: Event) {
        // Queries from callbacks must never encounter an active mutable borrow.
        let _ = (self.app.current(), self.app.parent_count());
        self.log.borrow_mut().push(event);
    }
    fn events(&self) -> Vec<Event> {
        core::mem::take(&mut *self.log.borrow_mut())
    }
}
impl Services<Owner> for Host<'_> {
    type RenderGuard = Guard;
    type Result = u8;
    fn render_lock(&self) -> Guard {
        assert!(!self.locked.replace(true));
        Guard(self.locked.clone())
    }
    fn before_screen_change(&self, _: bool) -> bool {
        assert!(!self.locked.get());
        self.radio_ready.get()
    }
    fn publish(&self, target: Option<u8>, visit: u32) {
        assert!(self.locked.get());
        self.record(Event::Publish(target, visit));
    }
    fn clear_header(&self) {
        assert!(self.locked.get());
        self.record(Event::Clear);
    }
    fn enter(&self, target: u8) {
        assert!(!self.locked.get());
        self.record(Event::Enter(target));
        if let Some(child) = self.enter_push.take() {
            self.app.queue(Transition::Push(self.owner(child)));
        }
    }
    fn exit(&self, target: u8) {
        assert!(self.locked.get());
        assert!(self.app.current().is_some() || self.app.parent_count() != 0);
        self.record(Event::Exit(target));
    }
    fn take_result(&self, target: u8) -> u8 {
        assert!(self.locked.get());
        self.record(Event::Result(target));
        target
    }
    fn deliver_result(&self, parent: u8, result: u8) {
        assert!(!self.locked.get());
        self.record(Event::Deliver(parent, result));
        if let Some(child) = self.result_push.take() {
            self.app.queue(Transition::Push(self.owner(child)));
        }
    }
    fn home(&self) -> Option<Owner> {
        assert!(!self.locked.get());
        Some(self.owner(0))
    }
    fn request_update(&self) {
        self.record(Event::Update);
    }
    fn missing_pop(&self) {
        self.record(Event::Missing);
    }
    fn stack_allocation_failed(&self) {
        self.record(Event::Oom);
    }
    fn prepare_sleep(&self, target: u8) {
        assert!(self.locked.get());
        self.record(Event::Prepare(target));
    }
}

#[test]
fn prepare_sleep_visits_retained_parents_then_current() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    app.replace(host.owner(1), &host);
    for id in [2, 3] {
        app.queue(Transition::Push(host.owner(id)));
        app.drain(&host);
    }
    host.events();
    app.prepare_sleep(&host);
    assert_eq!(
        host.events(),
        vec![Event::Prepare(1), Event::Prepare(2), Event::Prepare(3)]
    );
}

#[test]
fn bootstrap_push_pop_preserves_parent_and_result_order() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    app.replace(host.owner(1), &host);
    assert_eq!(
        host.events(),
        vec![Event::Publish(Some(1), 0), Event::Enter(1)]
    );
    app.queue(Transition::Push(host.owner(2)));
    app.drain(&host);
    assert_eq!(app.current(), Some(2));
    assert_eq!(app.parent_count(), 1);
    assert!(app.has_reader());
    assert_eq!(
        host.events(),
        vec![Event::Clear, Event::Publish(Some(2), 1), Event::Enter(2)]
    );
    app.queue(Transition::Pop);
    app.drain(&host);
    assert_eq!(app.current(), Some(1));
    assert_eq!(app.parent_count(), 0);
    assert_eq!(
        host.events(),
        vec![
            Event::Result(2),
            Event::Exit(2),
            Event::Drop(2),
            Event::Clear,
            Event::Publish(Some(1), 2),
            Event::Deliver(1, 2),
            Event::Update,
        ]
    );
}

#[test]
fn replace_exits_current_then_parents_in_reverse_order() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    app.replace(host.owner(1), &host);
    for id in [2, 3] {
        app.queue(Transition::Push(host.owner(id)));
        app.drain(&host);
    }
    host.events();
    app.replace(host.owner(4), &host);
    assert_eq!(app.current(), Some(3));
    app.drain(&host);
    assert_eq!(
        host.events(),
        vec![
            Event::Exit(3),
            Event::Drop(3),
            Event::Clear,
            Event::Exit(2),
            Event::Drop(2),
            Event::Exit(1),
            Event::Drop(1),
            Event::Publish(Some(4), 3),
            Event::Enter(4),
        ]
    );
    assert!(!app.has_reader());
}

#[test]
fn enter_and_result_callbacks_can_queue_transitions() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    host.enter_push.set(Some(2));
    app.replace(host.owner(1), &host);
    app.drain(&host);
    host.events();
    host.result_push.set(Some(3));
    app.queue(Transition::Pop);
    app.drain(&host);
    assert_eq!(app.current(), Some(3));
    assert_eq!(app.parent_count(), 1);
    assert_eq!(
        host.events(),
        vec![
            Event::Result(2),
            Event::Exit(2),
            Event::Drop(2),
            Event::Clear,
            Event::Publish(Some(1), 2),
            Event::Deliver(1, 2),
            Event::Clear,
            Event::Publish(Some(3), 3),
            Event::Enter(3),
        ]
    );
}

#[test]
fn deferred_radio_keeps_owners_and_sleep_bypasses_it() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    app.replace(host.owner(1), &host);
    host.events();
    host.radio_ready.set(false);
    app.queue(Transition::Replace(host.owner(2)));
    app.drain(&host);
    assert_eq!(app.current(), Some(1));
    assert_eq!(app.pending_kind(), TransitionKind::Replace);
    assert!(host.events().is_empty());
    app.queue(Transition::Sleep(host.owner(3)));
    app.drain(&host);
    assert_eq!(
        host.events(),
        vec![
            Event::Drop(2),
            Event::Exit(1),
            Event::Drop(1),
            Event::Clear,
            Event::Publish(Some(3), 1),
            Event::Enter(3),
        ]
    );
}

#[test]
fn empty_pop_is_ignored_and_last_pop_enters_home_once() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    app.queue(Transition::Pop);
    app.drain(&host);
    assert_eq!(host.events(), vec![Event::Missing]);
    assert_eq!(app.visit(), 0);
    app.replace(host.owner(1), &host);
    host.events();
    app.queue(Transition::Pop);
    app.drain(&host);
    assert_eq!(
        host.events(),
        vec![
            Event::Result(1),
            Event::Exit(1),
            Event::Drop(1),
            Event::Clear,
            Event::Publish(None, 1),
            Event::Publish(Some(0), 1),
            Event::Enter(0),
        ]
    );
    assert_eq!(app.current(), Some(0));
    assert_eq!(app.visit(), 1);
}

#[test]
fn replacing_pending_owner_never_enters_or_exits_it() {
    let app = Application::new().unwrap();
    let host = Host::new(&app);
    app.queue(Transition::Push(host.owner(1)));
    app.queue(Transition::Replace(host.owner(2)));
    app.queue(Transition::Pop);
    assert_eq!(host.events(), vec![Event::Drop(1), Event::Drop(2)]);
    assert_eq!(app.pending_kind(), TransitionKind::Pop);
}
