//! Production Application ordering, using IDs until StableOwner integration.
use super::*;
use xp_ui::app::{Activity, Application, Services, Transition};
enum Child {
    Confirmation(Box<Dialog<Platform, Confirmation<Heap>>>),
    Interval(Box<Dialog<Platform, Interval>>),
    Message(Box<Message<Platform>>),
}
#[derive(Debug, PartialEq)]
enum Result {
    Confirmation(ConfirmationResult),
    Interval(IntervalResult),
    Message(MessageResult),
}
impl Child {
    fn exit(&self) {
        match self {
            Self::Confirmation(d) => d.exit(),
            Self::Interval(d) => d.exit(),
            Self::Message(d) => d.exit(),
        }
    }
    fn result(&self) -> Option<Result> {
        match self {
            Self::Confirmation(d) => d.take_result().map(Result::Confirmation),
            Self::Interval(d) => d.take_result().map(Result::Interval),
            Self::Message(d) => {
                d.dismiss();
                d.take_result().map(Result::Message)
            }
        }
    }
}
struct Slots {
    child: RefCell<Option<Child>>,
    locked: Cell<bool>,
    events: RefCell<Vec<&'static str>>,
    delivered: RefCell<Option<Result>>,
}
struct Node {
    id: u8,
    slots: Rc<Slots>,
}
impl Activity for Node {
    type Target = u8;
    fn target(&self) -> u8 {
        self.id
    }
    fn is_reader(&self) -> bool {
        false
    }
}
impl Drop for Node {
    fn drop(&mut self) {
        assert!(self.slots.locked.get());
        if self.id == 1 {
            drop(self.slots.child.borrow_mut().take().unwrap());
            self.slots.events.borrow_mut().push("destroy child");
        } else {
            self.slots.events.borrow_mut().push("destroy parent");
        }
    }
}
struct Guard(Rc<Slots>);
impl Drop for Guard {
    fn drop(&mut self) {
        self.0.locked.set(false);
        self.0.events.borrow_mut().push("unlock");
    }
}
impl Services<Node> for Rc<Slots> {
    type RenderGuard = Guard;
    type Result = Option<Result>;
    fn render_lock(&self) -> Guard {
        assert!(!self.locked.replace(true));
        Guard(self.clone())
    }
    fn before_screen_change(&self, _: bool) -> bool {
        true
    }
    fn publish(&self, target: Option<u8>, _: u32) {
        assert!(self.locked.get());
        if target == Some(0) {
            self.events.borrow_mut().push("publish parent");
        }
    }
    fn clear_header(&self) {}
    fn enter(&self, _: u8) {
        assert!(!self.locked.get());
    }
    fn exit(&self, target: u8) {
        assert!(self.locked.get());
        if target == 1 {
            self.child.borrow().as_ref().unwrap().exit();
        }
    }
    fn with_result(&self, target: u8, f: impl FnOnce(Self::Result)) {
        assert_eq!(target, 1);
        assert!(self.locked.get());
        let result = self.child.borrow().as_ref().unwrap().result();
        self.events.borrow_mut().push("extract");
        f(result);
    }
    fn deliver_result(&self, parent: u8, result: Self::Result) {
        assert_eq!(parent, 0);
        assert!(!self.locked.get());
        assert!(self.child.borrow().is_none());
        self.events.borrow_mut().push("deliver");
        *self.delivered.borrow_mut() = result;
    }
    fn home(&self) -> Option<Node> {
        None
    }
    fn request_update(&self) {}
    fn missing_pop(&self) {
        panic!("missing pop")
    }
    fn stack_allocation_failed(&self) {
        panic!("stack allocation")
    }
    fn prepare_sleep(&self, _: u8) {}
}
#[test]
fn all_semantic_results_survive_child_destruction_and_synchronous_parent_delivery() {
    for kind in 0..3 {
        for cancel in [false, true] {
            let (h, _) = platform();
            let (child, expected) = match kind {
                0 => {
                    let d = Dialog::confirmation(h, b"Heading", b"Body", b"", b"").unwrap();
                    assert!(d.enter());
                    assert!(d.render());
                    if !cancel {
                        d.input(Input {
                            pressed: model::NAV_NEXT,
                            ..Default::default()
                        });
                    }
                    d.input(release(if cancel { model::BACK } else { model::CONFIRM }));
                    d.input(Input::default());
                    (
                        Child::Confirmation(d),
                        Result::Confirmation(if cancel {
                            ConfirmationResult::Cancelled
                        } else {
                            ConfirmationResult::Confirmed
                        }),
                    )
                }
                1 => {
                    let d = Dialog::interval(h, interval()).unwrap();
                    assert!(d.enter());
                    assert!(d.render());
                    d.input(release(if cancel { model::BACK } else { model::CONFIRM }));
                    (
                        Child::Interval(d),
                        Result::Interval(if cancel {
                            IntervalResult::Cancelled
                        } else {
                            IntervalResult::Interval(20)
                        }),
                    )
                }
                _ => {
                    let d = Message::try_new(h, b"Message", 0, Refresh::Fast).unwrap();
                    assert!(d.enter(0));
                    (
                        Child::Message(d),
                        Result::Message(MessageResult::MessageDismissed),
                    )
                }
            };
            let slots = Rc::new(Slots {
                child: RefCell::new(Some(child)),
                locked: Cell::new(false),
                events: RefCell::new(Vec::new()),
                delivered: RefCell::new(None),
            });
            let app = Application::new();
            app.replace(
                Node {
                    id: 0,
                    slots: slots.clone(),
                },
                &slots,
            );
            app.queue(Transition::Push(Node {
                id: 1,
                slots: slots.clone(),
            }));
            assert!(app.drain(&slots));
            slots.events.borrow_mut().clear();
            app.queue(Transition::Pop);
            assert!(app.drain(&slots));
            assert_eq!(
                *slots.events.borrow(),
                [
                    "extract",
                    "destroy child",
                    "publish parent",
                    "unlock",
                    "deliver"
                ]
            );
            assert_eq!(*slots.delivered.borrow(), Some(expected));
            let guard = slots.render_lock();
            drop(app);
            drop(guard);
            assert_eq!(
                slots
                    .events
                    .borrow()
                    .iter()
                    .filter(|e| **e == "destroy parent")
                    .count(),
                1
            );
        }
    }
}
