//! Real Application ordering with host ID targets; no retained raw owner target.
use super::tests::{owner, press_value, url_owner, Heap, Platform};
use super::{Keyboard, Result};
use crate::{
    app::{Activity, Application, Services, Transition},
    model::{Input, BACK},
};
extern crate std;
use std::{
    boxed::Box,
    cell::{Cell, RefCell},
    rc::Rc,
    vec::Vec,
};
struct Slots {
    child: RefCell<Option<Box<Keyboard<Platform>>>>,
    locked: Cell<bool>,
    events: RefCell<Vec<&'static str>>,
    delivered: RefCell<Option<Result<Heap>>>,
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
            let child = self.slots.child.borrow_mut().take().unwrap();
            child.exit();
            drop(child);
            self.slots.events.borrow_mut().push("destroy child");
        } else {
            self.slots.events.borrow_mut().push("destroy parent");
        }
    }
}
struct RenderGuard(Rc<Slots>);
impl Drop for RenderGuard {
    fn drop(&mut self) {
        self.0.locked.set(false);
        self.0.events.borrow_mut().push("unlock");
    }
}
impl Services<Node> for Rc<Slots> {
    type RenderGuard = RenderGuard;
    type Result = Option<Result<Heap>>;
    fn render_lock(&self) -> RenderGuard {
        assert!(!self.locked.replace(true));
        RenderGuard(self.clone())
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
    fn exit(&self, _: u8) {
        assert!(self.locked.get());
    }
    fn with_result(&self, target: u8, consume: impl FnOnce(Self::Result)) {
        assert_eq!(target, 1);
        assert!(self.locked.get());
        let result = self.child.borrow().as_ref().unwrap().take_result();
        consume(result);
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
fn semantic_result_delivery_follows_destroy_publish_unlock_and_parent_drops_once() {
    for (text, cancel) in [
        (b"".as_slice(), false),
        (b"maximum text".as_slice(), false),
        (b"cancel".as_slice(), true),
    ] {
        let slots = Rc::new(Slots {
            child: RefCell::new(None),
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
        let (k, _) = if cancel {
            owner(text, 0)
        } else {
            url_owner(text)
        };
        assert!(k.render());
        *slots.child.borrow_mut() = Some(k);
        app.queue(Transition::Push(Node {
            id: 1,
            slots: slots.clone(),
        }));
        assert!(app.drain(&slots));
        {
            let child = slots.child.borrow();
            let k = child.as_ref().unwrap();
            if cancel {
                k.input(
                    Input {
                        released: BACK,
                        ..Input::default()
                    },
                    None,
                );
            } else {
                press_value(k, 13);
            }
        }
        slots.events.borrow_mut().clear();
        app.queue(Transition::Pop);
        assert!(app.drain(&slots));
        assert_eq!(
            *slots.events.borrow(),
            ["destroy child", "publish parent", "unlock", "deliver"]
        );
        let delivered = slots.delivered.borrow_mut().take().unwrap();
        assert_eq!(delivered.is_cancelled(), cancel);
        assert_eq!(delivered.text(), if cancel { None } else { Some(text) });
        let guard = slots.render_lock();
        drop(app);
        drop(guard);
        assert_eq!(
            slots
                .events
                .borrow()
                .iter()
                .filter(|event| **event == "destroy parent")
                .count(),
            1
        );
    }
}
