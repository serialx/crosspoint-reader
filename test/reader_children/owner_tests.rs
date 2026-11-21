use crate::{owned, resources::Text, test_resources};
use std::{
    alloc::{GlobalAlloc, Layout, System},
    cell::Cell,
    sync::OnceLock,
};

thread_local! {
    static FAIL_NEXT: Cell<bool> = const { Cell::new(false) };
    static COUNT_ALLOCATIONS: Cell<Option<usize>> = const { Cell::new(None) };
}
pub(crate) fn count_allocations<T>(call: impl FnOnce() -> T) -> (T, usize) {
    COUNT_ALLOCATIONS.with(|n| n.set(Some(0)));
    let value = call();
    (value, COUNT_ALLOCATIONS.with(|n| n.take().unwrap()))
}
pub(crate) fn fail_next_allocation() {
    FAIL_NEXT.with(|flag| flag.set(true));
}
struct Allocator;
unsafe impl GlobalAlloc for Allocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let _ = COUNT_ALLOCATIONS.try_with(|n| {
            if let Some(count) = n.get() {
                n.set(Some(count + 1));
            }
        });
        if FAIL_NEXT
            .try_with(|flag| flag.replace(false))
            .unwrap_or(false)
        {
            std::ptr::null_mut()
        } else {
            System.alloc(layout)
        }
    }
    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        System.dealloc(pointer, layout);
    }
}
#[global_allocator]
static ALLOCATOR: Allocator = Allocator;
unsafe extern "C" fn lock() {}
pub(crate) fn runtime() -> &'static crate::abi::Runtime {
    static RUNTIME: OnceLock<crate::abi::Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| crate::abi::Runtime {
        model_lock: lock,
        model_unlock: lock,
        ..test_resources::runtime()
    })
}
fn items() -> crate::menu::Items {
    crate::menu::Items::new(crate::menu::Flags {
        footnotes: true,
        bookmarks: true,
        clippings: true,
        frontlight: true,
        bluetooth: true,
        dictionary: true,
        sync: true,
    })
}

#[test]
fn empty_toc_fallback_needs_no_allocation_under_memory_pressure() {
    let runtime = runtime();
    let mut text = Text::default();
    FAIL_NEXT.with(|flag| flag.set(true));
    assert!(text.assign_indented(0, b"").is_ok());
    assert!(owned::Percent::try_new(runtime, 50).is_none());
    assert!(text.is_empty());
}
#[test]
fn each_child_has_a_separate_stable_box_and_qr_moves_its_text() {
    let menu = owned::Menu::try_new(
        runtime(),
        items(),
        Text::copy(b"Title").unwrap(),
        Text::copy(b"1/2").unwrap(),
        3,
        0,
    )
    .unwrap();
    let chapters = owned::Chapters::try_new(runtime(), 700).unwrap();
    let percent = owned::Percent::try_new(runtime(), 0).unwrap();
    let payload = Text::copy(b"owned page QR text").unwrap();
    let original = payload.bytes().as_ptr();
    let qr = owned::Qr::try_new(runtime(), payload).unwrap();
    assert_eq!(qr.payload.bytes().as_ptr(), original);
    assert_eq!(chapters.model.with(|m| m.state.list.selected), 700);
    assert_eq!(menu.model.with(|m| m.menu.orientation), 3);
    assert_eq!(percent.model.with(|m| m.state.value()), 0);
    let before = (&*percent as *const owned::Percent).addr();
    let owner = owned::ReaderChild::Percent(percent.into());
    let owned::ReaderChild::Percent(percent) = owner else {
        unreachable!()
    };
    assert_eq!((&*percent as *const owned::Percent).addr(), before);
    let frame = unsafe { &*percent.frame.get() };
    assert_eq!(frame.commands.len(), 18);
    assert!(frame
        .commands
        .iter()
        .all(|command| *command == Default::default()));
    assert_eq!(std::mem::size_of_val(&frame.commands), 576);
    assert!(std::mem::size_of::<owned::ReaderChild>() < std::mem::size_of::<owned::Percent>());
    let toolbar = owned::Toolbar::try_new(runtime()).unwrap();
    let frame = unsafe { &*toolbar.frame.get() };
    assert_eq!(frame.rows.len(), 16);
    assert!(frame
        .rows
        .iter()
        .all(|row| row.label.is_empty() && row.value.is_empty()));
}
#[test]
fn owner_allocation_failure_is_fallible_for_every_screen() {
    let runtime = runtime();
    let title = Text::copy(b"menu title").unwrap();
    let progress = Text::copy(b"1/2").unwrap();
    let items = items();
    FAIL_NEXT.with(|flag| flag.set(true));
    assert!(owned::Menu::try_new(runtime, items, title, progress, 0, 0).is_none());
    FAIL_NEXT.with(|flag| flag.set(true));
    assert!(owned::Chapters::try_new(runtime, 0).is_none());
    FAIL_NEXT.with(|flag| flag.set(true));
    assert!(owned::Percent::try_new(runtime, 50).is_none());
    let text = Text::copy(b"QR payload before allocation failure").unwrap();
    FAIL_NEXT.with(|flag| flag.set(true));
    assert!(owned::Qr::try_new(runtime, text).is_none());
    FAIL_NEXT.with(|flag| flag.set(true));
    assert!(owned::Toolbar::try_new(runtime).is_none());
}

#[test]
fn percent_owner_publishes_only_complete_controls_and_keeps_input_during_paint() {
    use xp_ui::{
        model::{Input, TOUCH_RELEASED},
        scene::{HitSink, Interaction, Rect},
        utility::STEP,
    };
    let owner = owned::Percent::try_new(runtime(), 50).unwrap();
    let input = Input {
        touch_x: 15,
        touch_y: 15,
        touch_flags: TOUCH_RELEASED,
        ..Default::default()
    };
    let hit = Interaction {
        rect: Rect {
            x: 10,
            y: 10,
            w: 20,
            h: 20,
        },
        action: STEP,
        value: -1,
        inputs: 1,
        ..Default::default()
    };
    owner.input(input, true);
    assert_eq!(owner.model.with(|m| m.state.value()), 50);
    let frame = unsafe { &mut *owner.frame.get() };
    frame.controls.push(hit).unwrap();
    owner.publish(frame.controls);
    let rendered = owner.begin_render();
    frame.controls = Default::default();
    frame
        .controls
        .push(Interaction { value: 1, ..hit })
        .unwrap();
    owner.input(input, true);
    assert_eq!(rendered.value(), 50);
    assert_eq!(owner.model.with(|m| m.state.value()), 49);
    owner.publish(frame.controls);
    assert_eq!(owner.model.with(|m| m.state.value()), 49);
    owner.input(input, true);
    assert_eq!(owner.model.with(|m| m.state.value()), 50);
}

#[test]
fn chapter_owner_moves_result_once_before_destruction() {
    let owner = owned::Chapters::try_new(runtime(), 0).unwrap();
    let anchor = Text::copy(b"section-anchor").unwrap();
    let address = anchor.bytes().as_ptr();
    unsafe {
        *owner.result.get() = Some(crate::child_result::ReaderChildResult::Chapter(Some(
            crate::child_result::ChapterSelection { spine: 17, anchor },
        )));
    }
    let owner = owned::ReaderChild::Chapters(owner.into());
    let result = unsafe { owner.take_result() }.unwrap();
    assert!(unsafe { owner.take_result() }.is_none());
    drop(owner);
    let crate::child_result::ReaderChildResult::Chapter(Some(selected)) = result else {
        panic!("chapter result")
    };
    assert_eq!(selected.spine, 17);
    assert_eq!(selected.anchor.bytes().as_ptr(), address);
    assert_eq!(selected.anchor.bytes(), b"section-anchor");
}

#[test]
fn toolbar_publication_cannot_overwrite_navigation_during_render() {
    let owner = owned::Toolbar::try_new(runtime()).unwrap();
    let mut painted = owner.model.with(|m| m.navigation);
    painted.visible = 8;
    painted.drawn = 7;
    painted.drawn_count = 100;
    owner.publish(&painted, Default::default());
    assert_eq!(owner.visible_rows(), 7);
    assert!(owner.scroll(10, 100));
    owner.publish(&painted, Default::default());
    assert_eq!(owner.model.with(|m| m.navigation.top), 10);
    owner.reset_navigation(20, 16);
    owner.publish(&painted, Default::default());
    assert_eq!(owner.model.with(|m| m.navigation.top), 16);
    assert_eq!(owner.model.with(|m| m.navigation.selected), 20);
}

#[test]
fn stable_owner_transfer_allocates_nothing_and_drops_once() {
    struct Payload<'a>(&'a Cell<usize>);
    impl Drop for Payload<'_> {
        fn drop(&mut self) {
            self.0.set(self.0.get() + 1);
        }
    }
    let runtime = runtime();
    let drops = Cell::new(0);
    let boxed = Box::new(Payload(&drops));
    fail_next_allocation();
    let owner = crate::stable_owner::StableOwner::from(boxed);
    let target = owner.pointer();
    let moved = Some(owner);
    assert_eq!(unsafe { target.as_ref() }.0.get(), 0);
    drop(moved);
    assert_eq!(drops.get(), 1);
    // Transfer and destruction leave the injected failure for the next allocation.
    assert!(owned::Percent::try_new(runtime, 0).is_none());
}

#[test]
fn clipping_owner_is_fallible_and_starts_with_an_unpublished_empty_window() {
    let runtime = runtime();
    fail_next_allocation();
    assert!(owned::Clippings::try_new(runtime).is_none());
    let owner = owned::Clippings::try_new(runtime).unwrap();
    let frame = unsafe { &*owner.frame.get() };
    assert!(frame.initial);
    assert!(frame.window.row(0).is_none());
    owner
        .model
        .with(|m| assert!(!m.ready && m.state.list.selected == 0));
}

#[test]
fn clipping_child_target_and_result_survive_owner_transfer() {
    use crate::child_result::{ClippingPosition, ReaderChildResult};
    let owner = owned::Clippings::try_new(runtime()).unwrap();
    unsafe {
        *owner.result.get() = Some(ReaderChildResult::Clippings(Some(ClippingPosition {
            spine: 2,
            page: 9,
            offset: 4096,
        })));
    }
    let owner = owned::ReaderChild::Clippings(owner.into());
    let target = owner.target();
    let moved = Some(owner);
    let Some(ReaderChildResult::Clippings(Some(position))) = (unsafe { target.take_result() })
    else {
        panic!("clipping result")
    };
    assert!(unsafe { target.take_result() }.is_none());
    drop(moved);
    assert_eq!(
        (position.spine, position.page, position.offset),
        (2, 9, 4096)
    );
}
