use super::super::physical::Physical;
use super::*;
use crate::app::{abi as app_abi, stable_owner::StableOwner, test_resources};
use core::ffi::c_void;
use std::{
    alloc::{GlobalAlloc, Layout, System},
    cell::Cell,
    panic::{catch_unwind, AssertUnwindSafe},
    sync::OnceLock,
};
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::list,
    scene::Rect,
};
#[path = "test_leaves.rs"]
pub(in crate::app::epub::children) mod test_leaves;

#[derive(Clone, Copy, Default)]
struct Allocations {
    watching: bool,
    fail: bool,
    attempts: usize,
    created: usize,
    freed: usize,
}
thread_local! {
    static ALLOCATIONS: Cell<Allocations> = Cell::new(Allocations::default());
    static BUFFERS: Cell<usize> = const { Cell::new(0) };
    static PAINTS: Cell<usize> = const { Cell::new(0) };
    static INPUTS: Cell<usize> = const { Cell::new(0) };
}
thread_local! { static FAIL_ANY: Cell<bool> = const { Cell::new(false) }; }
pub(in crate::app::epub::children) fn fail_next_allocation() {
    FAIL_ANY.with(|v| v.set(true));
}
struct Allocator;
unsafe impl GlobalAlloc for Allocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if crate::app::home_test_allocator::denied(layout) {
            return core::ptr::null_mut();
        }
        if FAIL_ANY.try_with(|v| v.replace(false)).unwrap_or(false) {
            return core::ptr::null_mut();
        }
        let fail = if layout == Layout::new::<Toolbar>() {
            ALLOCATIONS
                .try_with(|trace| {
                    let mut a = trace.get();
                    let fail = a.watching && a.fail;
                    if a.watching {
                        a.attempts += 1;
                        a.fail = false;
                    }
                    trace.set(a);
                    fail
                })
                .unwrap_or(false)
        } else {
            false
        };
        if fail {
            return core::ptr::null_mut();
        }
        let pointer = unsafe { System.alloc(layout) };
        crate::app::home_test_allocator::allocated(pointer, layout);
        if !pointer.is_null() && layout == Layout::new::<Toolbar>() {
            let _ = ALLOCATIONS.try_with(|trace| {
                let mut a = trace.get();
                if a.watching {
                    a.created += 1;
                }
                trace.set(a);
            });
        }
        pointer
    }
    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        crate::app::home_test_allocator::deallocated(pointer);
        if layout == Layout::new::<Toolbar>() {
            let _ = ALLOCATIONS.try_with(|trace| {
                let mut a = trace.get();
                if a.watching {
                    a.freed += 1;
                }
                trace.set(a);
            });
        }
        unsafe { System.dealloc(pointer, layout) };
    }
}
#[global_allocator]
static ALLOCATOR: Allocator = Allocator;
unsafe extern "C" fn lock() {}
unsafe extern "C" fn width(text: app_abi::Text, _: i32, _: u8) -> i32 {
    text.len as i32 * 8
}
unsafe extern "C" fn line(_: i32) -> i32 {
    24
}
unsafe extern "C" fn paint(scene: *const SceneView, _: *const Paint) -> u8 {
    assert!(unsafe { (*scene).command_count } > 0);
    PAINTS.with(|count| count.set(count.get() + 1));
    1
}
unsafe extern "C" fn input(out: *mut Input) {
    INPUTS.with(|count| count.set(count.get() + 1));
    unsafe { out.write(Input::default()) };
}
unsafe extern "C" fn allocate(domain: u8, bytes: usize) -> *mut u8 {
    assert_eq!(domain, app_abi::MEMORY_DEFAULT);
    let layout = Layout::from_size_align(bytes + 8, 8).unwrap();
    let pointer = unsafe { std::alloc::alloc(layout) };
    if pointer.is_null() {
        return pointer;
    }
    unsafe { pointer.cast::<usize>().write(bytes) };
    BUFFERS.with(|count| count.set(count.get() + 1));
    unsafe { pointer.add(8) }
}
unsafe extern "C" fn release(domain: u8, pointer: *mut u8) {
    assert_eq!(domain, app_abi::MEMORY_DEFAULT);
    let pointer = unsafe { pointer.sub(8) };
    let bytes = unsafe { pointer.cast::<usize>().read() };
    unsafe { std::alloc::dealloc(pointer, Layout::from_size_align(bytes + 8, 8).unwrap()) };
    BUFFERS.with(|count| count.set(count.get() - 1));
}
fn parent() -> StableOwner<EpubSession> {
    static RUNTIME: OnceLock<app_abi::Runtime> = OnceLock::new();
    static RESOURCES: OnceLock<app_abi::Resources> = OnceLock::new();
    const LEAVES: abi::XpEpubLeaves = abi::XpEpubLeaves {
        input_snapshot: input,
        ..test_leaves::leaves()
    };
    let runtime = RUNTIME.get_or_init(|| app_abi::Runtime {
        model_lock: lock,
        model_unlock: lock,
        ..test_resources::runtime()
    });
    let leaves = &LEAVES;
    let resources = RESOURCES.get_or_init(|| app_abi::Resources {
        text_width: width,
        line_height: line,
        paint_scene: paint,
        buffer_allocate: allocate,
        buffer_release: release,
        ..test_resources::resources()
    });
    EpubSession::try_new(b"", false, 0, leaves, runtime, resources)
        .unwrap()
        .into()
}
unsafe extern "C" fn theme(
    out: *mut list::Theme,
    facts: *mut xp_ui::reader_chrome::runtime::ThemeFacts,
    toolbar: u8,
) {
    *facts = xp_ui::reader_chrome::runtime::ThemeFacts {
        body_line_height: 24,
        help_font: 0,
        bookmark_help_font: 0,
    };
    assert_eq!(toolbar, 1);
    let screen = Rect {
        w: 480,
        h: 800,
        ..Default::default()
    };
    unsafe {
        out.write(list::Theme {
            screen,
            safe: screen,
            row_min: 48,
            row_gap: 4,
            row_padding: 4,
            row_inset: 8,
            scroll_width: 3,
            space_sm: 4,
            space_md: 8,
            space_lg: 16,
            ..Default::default()
        })
    };
}
unsafe extern "C" fn layout(_: *mut xp_ui::utility::Layout) -> u8 {
    panic!("utility")
}
unsafe extern "C" fn header(_: Rect, _: app_abi::Text, _: u8) {
    panic!("header")
}
unsafe extern "C" fn footer(_: Rect, _: *const app_abi::Text) {
    panic!("footer")
}
unsafe extern "C" fn qr(_: Rect, _: app_abi::Text) {
    panic!("qr")
}
unsafe extern "C" fn icon(_: u32, _: Rect, _: Rect) {
    panic!("physical icon")
}
unsafe extern "C" fn prewarm(
    _: i32,
    _: *const c_void,
    _: unsafe extern "C" fn(*const c_void, u32) -> app_abi::Text,
    _: u32,
) {
    panic!("prewarm")
}
fn render(target: ToolbarTarget, parent: &EpubSession) {
    let pointer = match target {
        ToolbarTarget::Owned(pointer) => pointer,
        #[cfg(feature = "app-test")]
        ToolbarTarget::Legacy(_) => panic!("owned toolbar test"),
    };
    let physical = Physical {
        list_theme: theme,
        utility_layout: layout,
        header,
        utility_footer: footer,
        qr,
        toolbar_icon: icon,
        clear_fonts: lock,
        prewarm_rows: prewarm,
    };
    let content = Content {
        panel: true,
        dense: false,
        active_tool: 1,
        progress_permille: 500,
        chapter: Some(c"Chapter"),
        page_info: Some(c"1/2 50%"),
        panel_title: Some(c"Contents"),
        count: 100,
        selected: 7,
    };
    unsafe {
        pointer
            .as_ref()
            .render(parent.resources, &physical, &content, &mut |_, row| {
                row.label.assign(b"row").is_ok() && row.value.assign(b"value").is_ok()
            })
            .unwrap();
    }
}
fn slot(parent: &EpubSession) -> ToolbarSlot {
    ToolbarSlot::Owned(Toolbar::try_new(parent.runtime).unwrap().into())
}
#[test]
fn toolbar_install_target_survives_install_input_removal_and_reopen() {
    let parent = parent();
    ALLOCATIONS.with(|trace| {
        trace.set(Allocations {
            watching: true,
            ..Default::default()
        })
    });
    let access = unsafe { parent.borrowed_resources() };
    let target = parent.install_toolbar(&access, slot(&parent));
    target.begin(&parent);
    target.reset(&parent, 7, 3);
    assert_eq!(target.visible_rows(&parent), 1);
    assert_eq!(target.route(&parent).permille, -1); // Unpublished, no input sampled.
    INPUTS.with(|count| assert_eq!(count.get(), 0));
    render(target, &parent);
    assert!(target.visible_rows(&parent) > 1);
    assert_eq!(target.route(&parent).event, 0);
    INPUTS.with(|count| assert_eq!(count.get(), 1));
    assert!(target.scroll(&parent, 10, 100));
    render(target, &parent);
    assert!(catch_unwind(AssertUnwindSafe(
        || parent.install_toolbar(&access, slot(&parent))
    ))
    .is_err());
    ALLOCATIONS.with(|trace| assert_eq!(trace.get().freed, 1));
    assert!(target.visible_rows(&parent) > 1); // Failed install leaves current owner usable.
    parent.take_toolbar(&access).unwrap().close(&parent);
    assert!(parent.toolbar(&access).is_none());
    ALLOCATIONS.with(|trace| assert_eq!(trace.get().freed, 2));
    let reopened = parent.install_toolbar(&access, slot(&parent));
    reopened.begin(&parent);
    reopened.reset(&parent, 2, 0);
    render(reopened, &parent);
    reopened.route(&parent);
    parent.take_toolbar(&access).unwrap().close(&parent);
    // Rejected before install uses the same close path, without publishing a target.
    slot(&parent).close(&parent);
    ALLOCATIONS.with(|trace| {
        let mut a = trace.get();
        a.fail = true;
        trace.set(a);
    });
    assert!(Toolbar::try_new(parent.runtime).is_none());
    assert!(parent.toolbar(&access).is_none());
    BUFFERS.with(|count| assert_eq!(count.get(), 0));
    PAINTS.with(|count| assert_eq!(count.get(), 3));
    ALLOCATIONS.with(|trace| {
        let a = trace.replace(Allocations::default());
        assert_eq!((a.attempts, a.created, a.freed), (5, 4, 4));
    });
}

#[path = "poll_lite_tests.rs"]
mod poll_lite;
