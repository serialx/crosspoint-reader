//! Failed Application parent reservation must drop an unentered keyboard once.
use std::{
    alloc::{GlobalAlloc, Layout, System},
    cell::Cell,
    ptr::NonNull,
    rc::Rc,
};
use xp_ui::{
    app::{Activity, Application, Services, Transition},
    keyboard::{
        owner::{storage::Memory, Host, Keyboard, Lock, Resources},
        Layouts,
    },
    keyboard_view::{Layout as ViewLayout, Metrics, PaintKey},
    scene::{Command, Rect, Scene},
};
#[path = "../../../../rust/crates/xp-ffi/src/app/stable_owner.rs"]
mod stable_owner;
use stable_owner::StableOwner;

thread_local! {
    static FAIL_NEXT: Cell<bool> = const { Cell::new(false) };
    static FAILED_BYTES: Cell<usize> = const { Cell::new(0) };
}
struct Allocator;
unsafe impl GlobalAlloc for Allocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if FAIL_NEXT.try_with(|f| f.replace(false)).unwrap_or(false) {
            FAILED_BYTES.with(|n| n.set(layout.size()));
            return std::ptr::null_mut();
        }
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, p: *mut u8, layout: Layout) {
        unsafe { System.dealloc(p, layout) };
    }
}
#[global_allocator]
static ALLOCATOR: Allocator = Allocator;
#[derive(Default)]
struct Facts {
    locked: Cell<bool>,
    entered: Cell<usize>,
    dropped: Cell<usize>,
    failed: Cell<usize>,
    published: Cell<usize>,
    live: Cell<usize>,
}
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(l: Layout) -> *mut u8 {
        unsafe { std::alloc::alloc_zeroed(l) }
    }
    unsafe fn release(p: *mut u8, l: Layout) {
        unsafe { std::alloc::dealloc(p, l) };
    }
}
struct Physical(Rc<Facts>);
impl Drop for Physical {
    fn drop(&mut self) {
        self.0.dropped.set(self.0.dropped.get() + 1);
    }
}
unsafe impl Host for Physical {
    type Memory = Heap;
    fn lock(&self, _: Lock) {}
    fn unlock(&self, _: Lock) {}
    fn settings(&self) -> (u16, u8) {
        (1, 0)
    }
    fn layout(&self) -> ViewLayout {
        unsafe { std::mem::zeroed() }
    }
    fn with_layouts<R>(&self, _: impl FnOnce(&Layouts) -> R) -> R {
        panic!("unpainted shell")
    }
    fn with_metrics<R>(&self, _: impl FnOnce(&Metrics) -> R) -> R {
        panic!("unpainted shell")
    }
    fn paint(&self, _: &Scene<'_>, _: &mut impl Resources) -> bool {
        panic!("unpainted shell")
    }
    fn chrome(&self, _: &Command, _: Rect, _: &[u8]) {
        panic!("unpainted shell")
    }
    fn key(&self, _: &Command, _: &PaintKey, _: u8, _: bool, _: bool) {
        panic!("unpainted shell")
    }
    fn text(&self, _: &Command, _: Rect, _: &[u8]) {
        panic!("unpainted shell")
    }
    fn refresh(&self) {
        panic!("unpainted shell")
    }
    fn request_update(&self) {}
    fn haptic(&self, _: bool) {
        panic!("unpainted shell")
    }
    fn error(&self, _: &'static str, _: u32) {
        panic!("unexpected owner allocation failure")
    }
}
#[derive(Clone, Copy)]
enum Target {
    Parent,
    Keyboard(NonNull<Keyboard<Physical>>),
}
enum Node {
    Parent,
    Keyboard(StableOwner<Keyboard<Physical>>),
}
impl Activity for Node {
    type Target = Target;
    fn target(&self) -> Target {
        match self {
            Self::Parent => Target::Parent,
            Self::Keyboard(k) => Target::Keyboard(k.pointer()),
        }
    }
    fn is_reader(&self) -> bool {
        false
    }
}
struct Guard(Rc<Facts>);
impl Drop for Guard {
    fn drop(&mut self) {
        assert!(self.0.locked.replace(false));
    }
}
impl Services<Node> for Rc<Facts> {
    type RenderGuard = Guard;
    type Result = ();
    fn render_lock(&self) -> Guard {
        assert!(!self.locked.replace(true));
        Guard(self.clone())
    }
    fn before_screen_change(&self, _: bool) -> bool {
        true
    }
    fn publish(&self, t: Option<Target>, _: u32) {
        assert!(self.locked.get());
        self.published.set(self.published.get() + 1);
        self.live.set(if matches!(t, Some(Target::Keyboard(_))) {
            1
        } else {
            0
        });
    }
    fn clear_header(&self) {}
    fn enter(&self, t: Target) {
        assert!(!self.locked.get());
        if let Target::Keyboard(k) = t {
            self.entered.set(self.entered.get() + 1);
            assert!(unsafe { k.as_ref().initialize(b"Title", b"edit", 4, 0) });
        }
    }
    fn exit(&self, t: Target) {
        assert!(self.locked.get());
        if let Target::Keyboard(k) = t {
            unsafe { k.as_ref() }.exit();
        }
    }
    fn with_result(&self, _: Target, consume: impl FnOnce(())) {
        assert!(self.locked.get());
        consume(());
    }
    fn deliver_result(&self, _: Target, _: ()) {
        panic!("replacement delivers no result")
    }
    fn home(&self) -> Option<Node> {
        None
    }
    fn request_update(&self) {}
    fn missing_pop(&self) {
        panic!("no pop")
    }
    fn stack_allocation_failed(&self) {
        self.failed.set(self.failed.get() + 1);
    }
    fn prepare_sleep(&self, _: Target) {}
}
fn child(f: &Rc<Facts>) -> Node {
    Node::Keyboard(StableOwner::from(
        Keyboard::try_shell(Physical(f.clone())).unwrap(),
    ))
}
#[test]
fn parent_stack_failure_keeps_parent_and_drops_keyboard_once_then_retry_enters() {
    let facts = Rc::new(Facts::default());
    let app = Application::new();
    app.replace(Node::Parent, &facts);
    app.queue(Transition::Push(child(&facts)));
    // Nothing before try_reserve allocates; the one failing allocation is Vec<Node>.
    FAIL_NEXT.with(|f| f.set(true));
    assert!(app.drain(&facts));
    assert!(!FAIL_NEXT.with(Cell::get));
    assert!(FAILED_BYTES.with(Cell::get) >= std::mem::size_of::<Node>());
    assert!(matches!(app.current(), Some(Target::Parent)));
    assert_eq!(app.parent_count(), 0);
    assert_eq!(
        (
            facts.failed.get(),
            facts.entered.get(),
            facts.dropped.get(),
            facts.published.get(),
            facts.live.get()
        ),
        (1, 0, 1, 1, 0)
    );
    app.queue(Transition::Push(child(&facts)));
    assert!(app.drain(&facts));
    assert!(matches!(app.current(), Some(Target::Keyboard(_))));
    assert_eq!(app.parent_count(), 1);
    assert_eq!(
        (facts.failed.get(), facts.entered.get(), facts.dropped.get()),
        (1, 1, 1)
    );
    app.queue(Transition::Replace(Node::Parent));
    assert!(app.drain(&facts));
    assert!(matches!(app.current(), Some(Target::Parent)));
    assert_eq!(app.parent_count(), 0);
    assert_eq!((facts.entered.get(), facts.dropped.get()), (1, 2));
    let _guard = facts.render_lock();
    drop(app);
}
