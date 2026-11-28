use core::ffi::CStr;
use std::{
    alloc::{alloc_zeroed, dealloc, Layout as Allocation},
    cell::{Cell, RefCell},
    rc::Rc,
};
use xp_ui::{
    model::{self, Input},
    scene::{Command, Rect, Refresh, Scene},
    settings::{root::*, view::Layout},
};

thread_local! {
    static FAIL: Cell<bool> = const { Cell::new(false) };
    static ALLOCATIONS: RefCell<Vec<(bool, usize)>> = const { RefCell::new(Vec::new()) };
}
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(l: Allocation) -> *mut u8 {
        ALLOCATIONS.with(|a| a.borrow_mut().push((true, l.size())));
        if FAIL.with(|f| f.replace(false)) {
            std::ptr::null_mut()
        } else {
            unsafe { alloc_zeroed(l) }
        }
    }
    unsafe fn release(p: *mut u8, l: Allocation) {
        ALLOCATIONS.with(|a| a.borrow_mut().push((false, l.size())));
        unsafe { dealloc(p, l) };
    }
}
#[derive(Default)]
struct Facts {
    locked: Cell<bool>,
    home: Cell<u32>,
    reload: Cell<u32>,
    updates: Cell<u32>,
    haptics: Cell<u32>,
    fail_paint: Cell<bool>,
    fail_child: Cell<bool>,
    language: Cell<bool>,
    touch: Cell<bool>,
    sections: RefCell<Vec<Section>>,
    logs: RefCell<Vec<Log>>,
    commands: RefCell<Vec<Command>>,
    labels: RefCell<Vec<Vec<u8>>>,
    during_paint: RefCell<Option<Box<dyn Fn()>>>,
}
struct Platform(Rc<Facts>);
unsafe impl Host for Platform {
    type Memory = Heap;
    fn lock(&self) {
        assert!(!self.0.locked.replace(true));
    }
    fn unlock(&self) {
        assert!(self.0.locked.replace(false));
    }
    fn translate(&self, _: u16) -> &'static CStr {
        if self.0.language.get() {
            c"Changed"
        } else {
            c"Setting"
        }
    }
    fn layout(&self, l: &mut Layout) {
        *l = Layout::default();
        l.list.screen = Rect {
            x: 0,
            y: 0,
            w: 528,
            h: 792,
        };
        l.list.area = l.list.screen;
        l.safe = l.list.screen;
        l.list.row_height = 48;
        l.header_height = 40;
        l.hints_height = 40;
        l.touch = self.0.touch.get().into();
    }
    fn width(&self, text: &CStr, _: i32, _: u8) -> i32 {
        text.to_bytes().len() as i32 * 8
    }
    fn height(&self, _: &CStr, _: i32, _: u8, _: u8, _: i16) -> i32 {
        20
    }
    fn line(&self, _: i32) -> i32 {
        20
    }
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool {
        assert!(!self.0.locked.get());
        if self.0.fail_paint.replace(false) {
            return false;
        }
        *self.0.commands.borrow_mut() = scene.commands.to_vec();
        self.0.labels.borrow_mut().clear();
        for c in scene.commands {
            if c.resource != 0 {
                assert!(resources.valid(c.resource, c.generation));
                resources.paint(c, c.rect);
            }
        }
        if let Some(f) = self.0.during_paint.borrow_mut().take() {
            f();
        }
        true
    }
    fn text(&self, _: &Command, _: Rect, text: &CStr) {
        self.0.labels.borrow_mut().push(text.to_bytes().to_vec());
    }
    fn chrome(&self, _: &Command, _: Rect, _: &CStr, _: u8) {}
    fn footer(&self) {}
    fn refresh(&self, _: Refresh) {}
    fn request_update(&self) {
        self.0.updates.set(self.0.updates.get() + 1);
    }
    fn haptic(&self) {
        self.0.haptics.set(self.0.haptics.get() + 1);
    }
    fn home(&self) {
        self.0.home.set(self.0.home.get() + 1);
    }
    fn open_section(&self, section: Section) {
        self.0.sections.borrow_mut().push(section);
        // The physical child factory owns its allocation-stage diagnostic.
    }
    fn theme_reload(&self) {
        self.0.reload.set(self.0.reload.get() + 1);
    }
    fn log(&self, log: Log) {
        self.0.logs.borrow_mut().push(log);
    }
}
fn setup(section: u8) -> (Box<Root<Platform>>, Rc<Facts>) {
    FAIL.with(|v| v.set(false));
    ALLOCATIONS.with(|v| v.borrow_mut().clear());
    let facts = Rc::new(Facts::default());
    let owner = Root::try_new(Platform(facts.clone()), section).unwrap();
    (owner, facts)
}
#[test]
fn pending_network_is_deferred_and_failed_child_keeps_selection() {
    let (root, f) = setup(5);
    f.fail_child.set(true);
    assert!(root.enter());
    assert!(f.sections.borrow().is_empty());
    root.input(Input::default());
    assert_eq!(&*f.sections.borrow(), &[Section::Network]);
    assert_eq!(root.selected(), 5);
    root.input(Input::default());
    assert_eq!(f.sections.borrow().len(), 1);
    root.exit();
}
#[test]
fn all_six_initial_sections_and_end_have_native_meaning() {
    for section in 0..=6 {
        let (root, f) = setup(section);
        root.enter();
        root.input(Input::default());
        assert_eq!(f.sections.borrow().len(), usize::from(section < 6));
        root.exit();
    }
    assert!(Root::try_new(Platform(Rc::new(Facts::default())), 7).is_none());
}
#[test]
fn entry_allocation_failure_goes_home_then_exit_reloads_once() {
    let (root, f) = setup(6);
    FAIL.with(|v| v.set(true));
    assert!(!root.enter());
    assert_eq!(f.home.get(), 1);
    assert_eq!(&*f.logs.borrow(), &[Log::Enter, Log::Arena(1024)]);
    root.exit();
    root.exit();
    assert_eq!(f.reload.get(), 1);
    assert_eq!(f.logs.borrow().last(), Some(&Log::Exit));
}
#[test]
fn return_rebuilds_translation_and_invalidates_old_hits() {
    let (root, f) = setup(6);
    root.enter();
    assert!(root.render());
    assert!(root.ready());
    assert!(f.labels.borrow().iter().any(|v| v == b"Setting"));
    f.language.set(true);
    root.returned();
    assert!(!root.ready());
    assert!(root.render());
    assert!(f.labels.borrow().iter().any(|v| v == b"Changed"));
    assert!(!f.labels.borrow().iter().any(|v| v == b"Setting"));
    root.exit();
}
#[test]
fn failed_paint_suppresses_held_release_until_new_gesture() {
    let (root, f) = setup(6);
    root.enter();
    f.fail_paint.set(true);
    assert!(!root.render());
    root.input(Input {
        held: model::CONFIRM,
        ..Input::default()
    });
    assert!(root.render());
    root.input(Input {
        released: model::CONFIRM,
        ..Input::default()
    });
    assert!(f.sections.borrow().is_empty());
    root.input(Input {
        pressed: model::CONFIRM,
        held: model::CONFIRM,
        ..Input::default()
    });
    root.input(Input {
        released: model::CONFIRM,
        ..Input::default()
    });
    assert_eq!(&*f.sections.borrow(), &[Section::General]);
    root.exit();
}
#[test]
fn stale_paint_does_not_publish_and_touch_has_one_haptic() {
    let (root, f) = setup(6);
    root.enter();
    f.touch.set(true);
    // Box stays in place for this synchronous callback; App move coverage uses StableOwner.
    let pointer = &*root as *const Root<Platform>;
    *f.during_paint.borrow_mut() = Some(Box::new(move || unsafe { (*pointer).returned() }));
    assert!(root.render());
    assert!(!root.ready());
    assert!(root.render());
    assert!(root.ready());
    root.input(Input {
        touch_x: 40,
        touch_y: 50,
        touch_flags: model::TOUCH_PRESSED,
        ..Input::default()
    });
    root.input(Input {
        touch_x: 40,
        touch_y: 50,
        touch_flags: model::TOUCH_RELEASED,
        ..Input::default()
    });
    assert_eq!(f.haptics.get(), 1);
    assert_eq!(&*f.sections.borrow(), &[Section::General]);
    root.exit();
}
#[test]
fn repeated_visits_release_commands_and_reload_per_exit() {
    let (root, f) = setup(6);
    for _ in 0..3 {
        assert!(root.enter());
        assert!(root.render());
        root.input(Input {
            released: model::BACK,
            ..Input::default()
        });
        root.exit();
    }
    assert_eq!(f.home.get(), 3);
    assert_eq!(f.reload.get(), 3);
    let balance = ALLOCATIONS.with(|a| {
        a.borrow().iter().fold(0isize, |n, (new, b)| {
            n + if *new { *b as isize } else { -(*b as isize) }
        })
    });
    assert_eq!(balance, 0);
}
