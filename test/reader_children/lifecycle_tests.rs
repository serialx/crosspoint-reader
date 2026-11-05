use crate::{
    child_result::{ChapterSelection, ReaderChildResult},
    resources::Text,
};
use std::{
    cell::{Cell, RefCell},
    ptr::NonNull,
    rc::Rc,
};
use xp_ui::app::{Activity, Application, Services, Transition};

#[derive(Default)]
struct Trace {
    locked: Cell<bool>,
    events: RefCell<Vec<&'static str>>,
    delivered: RefCell<Option<ReaderChildResult>>,
}
struct Screen {
    child: bool,
    result: RefCell<Option<ReaderChildResult>>,
    trace: Rc<Trace>,
}
struct Owner(Box<Screen>);
impl Activity for Owner {
    type Target = NonNull<Screen>;
    fn target(&self) -> Self::Target {
        NonNull::from(self.0.as_ref())
    }
    fn is_reader(&self) -> bool {
        !self.0.child
    }
}
impl Drop for Screen {
    fn drop(&mut self) {
        if self.child {
            assert!(self.trace.locked.get());
            assert!(self.result.borrow().is_none());
            self.trace.events.borrow_mut().push("destroy");
        }
    }
}
struct Guard(Rc<Trace>);
impl Drop for Guard {
    fn drop(&mut self) {
        assert!(self.0.locked.replace(false));
        self.0.events.borrow_mut().push("unlock");
    }
}
struct Host(Rc<Trace>);
impl Host {
    fn owner(&self, result: Option<ReaderChildResult>) -> Owner {
        Owner(Box::new(Screen {
            child: result.is_some(),
            result: RefCell::new(result),
            trace: self.0.clone(),
        }))
    }
}
impl Services<Owner> for Host {
    type RenderGuard = Guard;
    type Result = ReaderChildResult;
    fn render_lock(&self) -> Guard {
        assert!(!self.0.locked.replace(true));
        Guard(self.0.clone())
    }
    fn before_screen_change(&self, _: bool) -> bool {
        true
    }
    fn publish(&self, _: Option<NonNull<Screen>>, _: u32) {
        assert!(self.0.locked.get());
        self.0.events.borrow_mut().push("publish");
    }
    fn clear_header(&self) {}
    fn enter(&self, _: NonNull<Screen>) {
        assert!(!self.0.locked.get());
    }
    fn exit(&self, _: NonNull<Screen>) {
        assert!(self.0.locked.get());
        self.0.events.borrow_mut().push("exit");
    }
    fn with_result(&self, target: NonNull<Screen>, consume: impl FnOnce(ReaderChildResult)) {
        assert!(self.0.locked.get());
        let result = unsafe { target.as_ref() }
            .result
            .borrow_mut()
            .take()
            .unwrap();
        consume(result);
    }
    fn deliver_result(&self, _: NonNull<Screen>, result: ReaderChildResult) {
        assert!(!self.0.locked.get());
        self.0.events.borrow_mut().push("deliver");
        *self.0.delivered.borrow_mut() = Some(result);
    }
    fn home(&self) -> Option<Owner> {
        None
    }
    fn request_update(&self) {}
    fn missing_pop(&self) {
        panic!("missing pop")
    }
    fn stack_allocation_failed(&self) {
        panic!("navigation allocation failed")
    }
    fn prepare_sleep(&self, _: NonNull<Screen>) {}
}
#[test]
fn chapter_result_moves_across_destroy_publish_unlock_without_copy() {
    let trace = Rc::new(Trace::default());
    let host = Host(trace.clone());
    let app = Application::new();
    app.replace(host.owner(None), &host);
    let anchor = Text::copy(b"chapter-1023-anchor").unwrap();
    let pointer = anchor.bytes().as_ptr();
    app.queue(Transition::Push(host.owner(Some(
        ReaderChildResult::Chapter(Some(ChapterSelection {
            spine: 1023,
            anchor,
        })),
    ))));
    app.drain(&host);
    trace.events.borrow_mut().clear();
    app.queue(Transition::Pop);
    app.drain(&host);
    assert_eq!(
        *trace.events.borrow(),
        ["exit", "destroy", "publish", "unlock", "deliver"]
    );
    let ReaderChildResult::Chapter(Some(chapter)) = trace.delivered.borrow_mut().take().unwrap()
    else {
        panic!("wrong result")
    };
    assert_eq!(chapter.spine, 1023);
    assert_eq!(chapter.anchor.bytes().as_ptr(), pointer);
    assert_eq!(chapter.anchor.bytes(), b"chapter-1023-anchor");
}
#[test]
fn cancellation_and_zero_percent_remain_distinct_owned_results() {
    for result in [
        ReaderChildResult::Chapter(None),
        ReaderChildResult::Percent(None),
        ReaderChildResult::Percent(Some(0)),
        ReaderChildResult::QrClosed,
    ] {
        let trace = Rc::new(Trace::default());
        let host = Host(trace.clone());
        let app = Application::new();
        let tag = core::mem::discriminant(&result);
        app.replace(host.owner(None), &host);
        app.queue(Transition::Push(host.owner(Some(result))));
        app.drain(&host);
        app.queue(Transition::Pop);
        app.drain(&host);
        let delivered = trace.delivered.borrow_mut().take().unwrap();
        assert_eq!(core::mem::discriminant(&delivered), tag);
        if let ReaderChildResult::Percent(Some(value)) = delivered {
            assert_eq!(value, 0);
        }
    }
}
