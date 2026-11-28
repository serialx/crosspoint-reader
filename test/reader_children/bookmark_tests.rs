use crate::{
    bookmark_owned::Bookmarks,
    bookmark_window::{Toc, Window},
    bookmarks, owner_tests,
    resources::Text,
    stable_owner::StableOwner,
};

struct Titles {
    text: Vec<u8>,
    calls: usize,
    fail: bool,
}
impl Toc for Titles {
    fn subtitle(&mut self, _: u16, prefix: &[u8], out: &mut Text) -> bool {
        self.calls += 1;
        !self.fail && out.assign_parts(&[prefix, &self.text]).is_ok()
    }
}
fn rows(count: usize) -> Vec<bookmarks::Record<Text>> {
    (0..count)
        .map(|i| bookmarks::Record {
            xpath: Text::copy(b"/body/p[1]").unwrap(),
            summary: Text::copy(b"summary").unwrap(),
            name: Text::default(),
            percentage: 0.505,
            spine: i as u16,
            page_count: 10,
            page: 2,
            visible_offset: Some(0),
        })
        .collect()
}
#[test]
fn bookmark_owner_is_one_fallible_stable_allocation() {
    let runtime = owner_tests::runtime();
    owner_tests::fail_next_allocation();
    let (denied, attempts) = owner_tests::count_allocations(|| Bookmarks::try_new(runtime));
    assert!(denied.is_none());
    assert_eq!(attempts, 1);
    let (owner, allocations) = owner_tests::count_allocations(|| Bookmarks::try_new(runtime));
    assert_eq!(allocations, 1);
    let owner: StableOwner<Bookmarks> = owner.unwrap().into();
    let target = owner.pointer();
    let mut parents = vec![owner];
    parents.reserve(32);
    assert_eq!(parents[0].pointer(), target);
    assert_eq!(
        unsafe { target.as_ref() }
            .model
            .with(|m| m.state.list.selected),
        0
    );
    assert!(!unsafe { target.as_ref() }.model.with(|m| m.ready));
}
#[test]
fn subtitle_window_owns_only_visible_subtitles_and_invalidates_partial_fill() {
    let owner = Bookmarks::try_new(owner_tests::runtime()).unwrap();
    let window: &mut Window = unsafe { &mut (*owner.frame.get()).window };
    let records = rows(80);
    let mut toc = Titles {
        text: b"chapter".to_vec(),
        calls: 0,
        fail: false,
    };
    assert!(window.refresh(&records, 40, &mut toc));
    assert!(window.subtitle(39).is_none());
    assert_eq!(
        window.subtitle(40).unwrap().bytes(),
        b"51% - 3/10 - chapter"
    );
    assert_eq!(toc.calls, 24);
    toc.text.fill(b'x');
    assert!(window.refresh(&records, 40, &mut toc));
    assert_eq!(toc.calls, 24);
    assert_eq!(
        window.subtitle(40).unwrap().bytes(),
        b"51% - 3/10 - chapter"
    );
    toc.fail = true;
    assert!(!window.refresh(&records, 60, &mut toc));
    assert!(window.subtitle(40).is_none());
    assert!(window.subtitle(60).is_none());
    toc.fail = false;
    assert!(window.refresh(&records, 60, &mut toc));
    assert!(window.subtitle(55).is_none());
    assert!(window.subtitle(56).is_some());
    window.invalidate();
    assert!(window.subtitle(56).is_none());
}
#[test]
fn bookmark_result_owns_xpath_and_preserves_offset_with_invalid_page_hints() {
    let owner = Bookmarks::try_new(owner_tests::runtime()).unwrap();
    let mut records = rows(2);
    records[1].page = 10;
    owner.select(1, records.len());
    let result = owner.selection(&records, 2).unwrap();
    assert_eq!(result.spine, 1);
    assert_eq!(result.visible_offset, Some(0));
    assert_eq!((result.page, result.total_pages), (0, 0));
    drop(records);
    drop(owner);
    assert_eq!(result.xpath.bytes(), b"/body/p[1]");
}
#[test]
fn failed_result_allocation_and_deleted_selection_do_not_escape_old_rows() {
    let owner = Bookmarks::try_new(owner_tests::runtime()).unwrap();
    let mut records = rows(2);
    owner.select(1, records.len());
    owner_tests::fail_next_allocation();
    assert!(owner.selection(&records, 2).is_none());
    records.remove(1);
    unsafe { owner.after_delete(records.len()) };
    assert_eq!(owner.model.with(|m| m.state.list.selected), 0);
    assert!(unsafe { owner.take_result() }.is_none());
    records.clear();
    unsafe { owner.after_delete(0) };
    assert!(matches!(unsafe { owner.take_result() }, Some(None)));
    assert!(unsafe { owner.take_result() }.is_none());
    assert!(owner.selection(&records, 2).is_none());
}

#[test]
fn bookmark_swipe_and_hold_use_published_rows_and_stale_rows_are_ignored() {
    use xp_ui::{
        model::{Input, CONFIRM, SWIPE_LEFT, TOUCH_RELEASED},
        reader_chrome::{hits, ACTIONS, DELETE},
        scene::{HitSink, Interaction, Rect},
    };
    let owner = Bookmarks::try_new(owner_tests::runtime()).unwrap();
    owner.model.with(|model| {
        model.hits = hits::Layout::new(1, 1);
        model
            .hits
            .push(Interaction {
                rect: Rect {
                    x: 0,
                    y: 20,
                    w: 100,
                    h: 40,
                },
                action: 1,
                value: 1,
                generation: 1,
                inputs: 1,
                flags: 0,
            })
            .unwrap();
        let swipe = Input {
            swipe: SWIPE_LEFT,
            ..Default::default()
        };
        model.bookmark_input(swipe, 2, false, None, Some((10, 30)));
        assert_eq!(model.state.revealed, -1);
        model.ready = true;
        model.bookmark_input(swipe, 1, false, None, Some((10, 30)));
        assert_eq!(model.state.revealed, -1);
        model.bookmark_input(swipe, 2, false, None, Some((10, 30)));
        assert_eq!(model.state.revealed, 1);
        model
            .hits
            .push(Interaction {
                rect: Rect {
                    x: 70,
                    y: 20,
                    w: 30,
                    h: 40,
                },
                action: 2,
                value: 1,
                generation: 1,
                inputs: 1,
                flags: 0,
            })
            .unwrap();
        let tap = Input {
            touch_x: 80,
            touch_y: 30,
            touch_flags: TOUCH_RELEASED,
            ..Default::default()
        };
        assert_eq!(
            model.bookmark_input(tap, 2, false, None, None).effect.kind,
            DELETE
        );
        assert_eq!(model.state.revealed, -1);
        let held = model.bookmark_input(Input::default(), 2, false, Some((10, 30)), None);
        assert_eq!(held.effect.kind, ACTIONS);
        assert_eq!(held.effect.value, 1);
        assert!(held.long_haptic);
        let release = Input {
            released: CONFIRM,
            ..Default::default()
        };
        assert_eq!(
            model
                .bookmark_input(release, 2, false, None, None)
                .effect
                .kind,
            0
        );
        assert_eq!(
            model
                .bookmark_input(release, 2, false, None, None)
                .effect
                .value,
            1
        );
    });
}

mod lifecycle {
    use super::*;
    use crate::bookmark_owned::Position;
    use core::{
        cell::{Cell, RefCell},
        mem::ManuallyDrop,
        ptr::NonNull,
    };
    use std::rc::Rc;
    use xp_ui::app::{Activity, Application, Services, Transition};
    #[derive(Default)]
    struct Trace {
        locked: Cell<bool>,
        events: RefCell<Vec<&'static str>>,
        delivered: RefCell<Option<Option<Position>>>,
        drops: Cell<usize>,
        stack_failures: Cell<usize>,
    }
    struct Child {
        owner: ManuallyDrop<StableOwner<Bookmarks>>,
        trace: Rc<Trace>,
    }
    impl Drop for Child {
        fn drop(&mut self) {
            // Free the actual child before publication; no borrowed owner survives.
            unsafe { ManuallyDrop::drop(&mut self.owner) };
            self.trace.drops.set(self.trace.drops.get() + 1);
            self.trace.events.borrow_mut().push("destroy");
        }
    }
    enum Owner {
        Parent,
        Child(Child),
    }
    #[derive(Clone, Copy)]
    enum Target {
        Parent,
        Child(NonNull<Bookmarks>),
    }
    impl Activity for Owner {
        type Target = Target;
        fn target(&self) -> Target {
            match self {
                Self::Parent => Target::Parent,
                Self::Child(c) => Target::Child(c.owner.pointer()),
            }
        }
        fn is_reader(&self) -> bool {
            matches!(self, Self::Parent)
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
        fn child(&self) -> Owner {
            Owner::Child(Child {
                owner: ManuallyDrop::new(
                    Bookmarks::try_new(owner_tests::runtime()).unwrap().into(),
                ),
                trace: self.0.clone(),
            })
        }
    }
    impl Services<Owner> for Host {
        type RenderGuard = Guard;
        type Result = Option<Position>;
        fn render_lock(&self) -> Guard {
            assert!(!self.0.locked.replace(true));
            Guard(self.0.clone())
        }
        fn before_screen_change(&self, _: bool) -> bool {
            true
        }
        fn publish(&self, _: Option<Target>, _: u32) {
            assert!(self.0.locked.get());
            self.0.events.borrow_mut().push("publish");
        }
        fn clear_header(&self) {}
        fn enter(&self, _: Target) {
            assert!(!self.0.locked.get());
        }
        fn exit(&self, target: Target) {
            assert!(self.0.locked.get());
            if let Target::Child(owner) = target {
                unsafe { owner.as_ref().exit(|_| panic!("no popup")) };
            }
            self.0.events.borrow_mut().push("exit");
        }
        fn with_result(&self, target: Target, consume: impl FnOnce(Self::Result)) {
            assert!(self.0.locked.get());
            let Target::Child(owner) = target else {
                panic!("parent result")
            };
            let result = unsafe { owner.as_ref().take_result() }.unwrap();
            consume(result);
        }
        fn deliver_result(&self, parent: Target, result: Self::Result) {
            assert!(!self.0.locked.get());
            assert!(matches!(parent, Target::Parent));
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
            self.0.stack_failures.set(self.0.stack_failures.get() + 1);
        }
        fn prepare_sleep(&self, _: Target) {}
    }
    #[test]
    fn actual_bookmark_owner_result_moves_through_application_destruction() {
        let trace = Rc::new(Trace::default());
        let host = Host(trace.clone());
        let app = Application::new();
        app.replace(Owner::Parent, &host);
        let records = rows(1);
        app.queue(Transition::Push(host.child()));
        app.drain(&host);
        let Target::Child(target) = app.current().unwrap() else {
            panic!("child")
        };
        let pointer = {
            let owner = unsafe { target.as_ref() };
            let result = owner.selection(&records, 1).unwrap();
            let pointer = result.xpath.bytes().as_ptr();
            unsafe { owner.finish(Some(result)) };
            pointer
        };
        drop(records);
        trace.events.borrow_mut().clear();
        app.queue(Transition::Pop);
        app.drain(&host);
        assert_eq!(
            *trace.events.borrow(),
            ["exit", "destroy", "publish", "unlock", "deliver"]
        );
        assert_eq!(trace.drops.get(), 1);
        let delivered = trace.delivered.borrow_mut().take().unwrap().unwrap();
        assert_eq!(delivered.xpath.bytes().as_ptr(), pointer);
        assert_eq!(delivered.visible_offset, Some(0));
        assert_eq!(delivered.xpath.bytes(), b"/body/p[1]");
    }
    #[test]
    fn bookmark_parent_stack_failure_and_pending_replacement_release_owners_once() {
        let trace = Rc::new(Trace::default());
        let host = Host(trace.clone());
        let app = Application::new();
        app.replace(Owner::Parent, &host);
        // Reserve the diagnostic trace before targeting Application's parent reserve.
        trace.events.borrow_mut().reserve(32);
        app.queue(Transition::Push(host.child()));
        owner_tests::fail_next_allocation();
        app.drain(&host);
        assert!(matches!(app.current(), Some(Target::Parent)));
        assert_eq!(trace.stack_failures.get(), 1);
        assert_eq!(trace.drops.get(), 1);
        app.queue(Transition::Push(host.child()));
        app.queue(Transition::Push(host.child()));
        assert_eq!(trace.drops.get(), 2);
        app.drain(&host);
        let Target::Child(target) = app.current().unwrap() else {
            panic!("child")
        };
        unsafe { target.as_ref().finish(None) };
        app.queue(Transition::Pop);
        app.drain(&host);
        assert_eq!(trace.drops.get(), 3);
        assert!(matches!(*trace.delivered.borrow(), Some(None)));
    }
}
