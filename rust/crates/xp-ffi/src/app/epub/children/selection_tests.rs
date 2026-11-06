use super::{
    selection_owned::Lookup,
    selection_physical::LookupStorage,
    toolbar_owner::tests::{fail_next_allocation, test_leaves},
};
use crate::app::{
    abi as app_abi,
    epub::{abi, page_owner::Page},
    test_resources,
};
use core::cell::Cell;
use std::{
    alloc::{alloc, dealloc, Layout},
    sync::OnceLock,
};
thread_local! {
    static PAGE_DROPS: Cell<usize> = const { Cell::new(0) };
    static NATIVE_DROPS: Cell<usize> = const { Cell::new(0) };
    static NATIVE_INITS: Cell<usize> = const { Cell::new(0) };
}
unsafe extern "C" fn allocate(_: u8, bytes: usize) -> *mut u8 {
    let raw = unsafe { alloc(Layout::from_size_align(bytes + 8, 8).unwrap()) };
    if raw.is_null() {
        return raw;
    }
    unsafe {
        raw.cast::<usize>().write(bytes);
        raw.add(8)
    }
}
unsafe extern "C" fn release(_: u8, pointer: *mut u8) {
    let raw = unsafe { pointer.sub(8) };
    let bytes = unsafe { raw.cast::<usize>().read() };
    unsafe { dealloc(raw, Layout::from_size_align(bytes + 8, 8).unwrap()) };
    PAGE_DROPS.with(|v| v.set(v.get() + 1));
}
unsafe extern "C" fn lock() {}
fn runtime() -> &'static app_abi::Runtime {
    static VALUE: OnceLock<app_abi::Runtime> = OnceLock::new();
    VALUE.get_or_init(|| app_abi::Runtime {
        model_lock: lock,
        model_unlock: lock,
        ..test_resources::runtime()
    })
}
fn page() -> Page {
    page_bytes(&[0; 6])
}
fn page_bytes(bytes: &[u8]) -> Page {
    static RESOURCES: OnceLock<app_abi::Resources> = OnceLock::new();
    const LEAVES: abi::XpEpubLeaves = test_leaves::leaves();
    let r = RESOURCES.get_or_init(|| app_abi::Resources {
        buffer_allocate: allocate,
        buffer_release: release,
        ..test_resources::resources()
    });
    Page::from_bytes(r, &LEAVES, bytes, 0).unwrap()
}

#[test]
fn footnote_page_loan_first_match_and_owned_href_survive_navigation() {
    use super::selection_owned::Footnote;
    let mut storage = vec![0; 20_000];
    let mut builder = xp_core::page::builder::Builder::new(&mut storage).unwrap();
    let mut href = [0; 256];
    href[..12].copy_from_slice(b"chapter#note");
    let mut number = [0; 32];
    number[0] = b'1';
    for _ in 0..16 {
        builder.footnote(&number, &href).unwrap();
    }
    for x in 0..32 {
        builder
            .link(xp_core::page::Link {
                href: &href,
                x,
                y: 0,
                width: 10,
                height: 10,
            })
            .unwrap();
    }
    let page = page_bytes(builder.finish().unwrap());
    let arena = page.arena().as_ptr_range();
    PAGE_DROPS.with(|v| v.set(0));
    let owner = Footnote::try_new(runtime(), page, [4, 8]).unwrap();
    assert_eq!(owner.count, 16);
    for (i, link) in owner.links.iter().enumerate() {
        assert_eq!(*link, [0, i as u16]);
    }
    assert!(arena.contains(&owner.href(15).unwrap().as_ptr()));
    assert!(owner.href(16).is_none());
    let result = owner.selection(15).unwrap();
    let mut parents = vec![owner];
    parents.reserve(16);
    drop(parents);
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    assert_eq!(result.bytes(), b"chapter#note");
}

#[test]
fn footnote_owner_oom_releases_its_page_once() {
    let runtime = runtime();
    let page = page();
    PAGE_DROPS.with(|v| v.set(0));
    fail_next_allocation();
    assert!(super::selection_owned::Footnote::try_new(runtime, page, [0; 2]).is_none());
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
}

thread_local! {
    static CALLS: core::cell::RefCell<Vec<&'static str>> = const { core::cell::RefCell::new(Vec::new()) };
}
fn call(name: &'static str) {
    CALLS.with(|v| v.borrow_mut().push(name));
}
unsafe extern "C" fn unavailable(_: *mut LookupStorage, needs: *mut u8) -> u8 {
    call("open");
    unsafe { *needs = 0 };
    0
}
unsafe extern "C" fn wait_paint() {
    call("paint");
}
unsafe extern "C" fn discard(_: *mut LookupStorage) {
    call("discard");
}

#[test]
fn lookup_unavailable_keeps_page_until_timed_error_exit() {
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    ops.lookup_open = unavailable;
    ops.wait_paint = wait_paint;
    ops.lookup_discard = discard;
    let owner = Lookup::try_new(
        runtime(),
        page(),
        [0; 2],
        1,
        &ops,
        app_abi::Text::from_bytes(b"word"),
    )
    .unwrap();
    CALLS.with(|v| v.borrow_mut().clear());
    PAGE_DROPS.with(|v| v.set(0));
    assert!(matches!(
        unsafe { owner.perform(&ops, || 100, || panic!("unavailable lookup drops no Page")) },
        super::lookup::Attempt::Popup {
            allocation_failed: false
        }
    ));
    CALLS.with(|v| assert_eq!(*v.borrow(), ["open", "paint", "discard"]));
    owner.model.with(|m| {
        assert_eq!(m.state.message, xp_ui::dictionary::popup::ERROR);
        assert!(!m.state.expired(1599));
        assert!(m.state.expired(1600));
    });
    assert_eq!(PAGE_DROPS.with(Cell::get), 0);
    drop(owner);
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
}

unsafe extern "C" fn opened(_: *mut LookupStorage, needs: *mut u8) -> u8 {
    unsafe { *needs = 1 };
    call("open");
    1
}
unsafe extern "C" fn indexed(_: *mut LookupStorage, result: *mut u8) -> u8 {
    unsafe { *result = 0 };
    call("index");
    1
}
unsafe extern "C" fn found(_: *mut LookupStorage, result: *mut u8) -> u8 {
    unsafe { *result = 0 };
    call("find");
    1
}
unsafe extern "C" fn found_oom(p: *mut LookupStorage, result: *mut u8) -> u8 {
    let result = unsafe { found(p, result) };
    fail_next_allocation();
    result
}
#[repr(C)]
struct DefinitionLoan {
    lookup: *mut LookupStorage,
}
unsafe extern "C" fn definition_lookup(
    p: *mut super::selection_physical::DefinitionStorage,
    source: *mut LookupStorage,
) -> *mut core::ffi::c_void {
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    assert_eq!(NATIVE_DROPS.with(Cell::get), 0);
    unsafe {
        p.cast::<DefinitionLoan>()
            .write(DefinitionLoan { lookup: source })
    };
    call("definition");
    p.cast()
}
unsafe extern "C" fn definition_drop(p: *mut super::selection_physical::DefinitionStorage) {
    let lookup = unsafe { (*p.cast::<DefinitionLoan>()).lookup };
    assert_eq!(unsafe { (*lookup.cast::<Native>()).pointer }, lookup);
    assert_eq!(NATIVE_DROPS.with(Cell::get), 0);
    call("definition-drop");
}
#[test]
fn lookup_definition_loan_survives_owner_moves_after_page_drop() {
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    ops.lookup_open = opened;
    ops.lookup_index = indexed;
    ops.lookup_find = found;
    ops.wait_paint = wait_paint;
    ops.lookup_discard = discard;
    ops.definition_lookup = definition_lookup;
    ops.definition_drop = definition_drop;
    let owner = Lookup::try_new(
        runtime(),
        page(),
        [0; 2],
        1,
        &ops,
        app_abi::Text::from_bytes(b"word"),
    )
    .unwrap();
    CALLS.with(|v| v.borrow_mut().clear());
    PAGE_DROPS.with(|v| v.set(0));
    NATIVE_DROPS.with(|v| v.set(0));
    let super::lookup::Attempt::Detail(viewer) =
        (unsafe { owner.perform(&ops, || 0, || owner.release_page()) })
    else {
        panic!("lookup must open a definition")
    };
    let mut parents = vec![owner];
    parents.reserve(16);
    let mut current = vec![viewer];
    current.reserve(16);
    drop(current);
    CALLS.with(|v| {
        assert_eq!(
            *v.borrow(),
            [
                "open",
                "paint",
                "index",
                "find",
                "definition",
                "discard",
                "definition-drop"
            ]
        )
    });
    assert_eq!(NATIVE_DROPS.with(Cell::get), 0);
    drop(parents);
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    assert_eq!(NATIVE_DROPS.with(Cell::get), 1);
}
#[test]
fn definition_oom_discards_attempt_output_after_page_drop() {
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    ops.lookup_open = opened;
    ops.lookup_index = indexed;
    ops.lookup_find = found_oom;
    ops.wait_paint = wait_paint;
    ops.lookup_discard = discard;
    let owner = Lookup::try_new(
        runtime(),
        page(),
        [0; 2],
        1,
        &ops,
        app_abi::Text::from_bytes(b"word"),
    )
    .unwrap();
    CALLS.with(|v| {
        let mut v = v.borrow_mut();
        v.clear();
        v.reserve(16);
    });
    PAGE_DROPS.with(|v| v.set(0));
    assert!(matches!(
        unsafe { owner.perform(&ops, || 200, || owner.release_page()) },
        super::lookup::Attempt::Popup {
            allocation_failed: true
        }
    ));
    CALLS.with(|v| assert_eq!(*v.borrow(), ["open", "paint", "index", "find", "discard"]));
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    owner.model.with(|m| {
        assert_eq!(m.state.message, xp_ui::dictionary::popup::LOW_MEMORY);
        assert!(m.state.expired(1700));
    });
    drop(owner);
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
}

unsafe extern "C" fn paint_scene(
    scene: *const xp_ui::ffi::SceneView,
    paint: *const xp_ui::ffi::Paint,
) -> u8 {
    use xp_ui::scene::Kind;
    let scene = unsafe { &*scene };
    let paint = unsafe { &*paint };
    for command in
        unsafe { core::slice::from_raw_parts(scene.commands, usize::from(scene.command_count)) }
    {
        if command.kind == Kind::Clear as u8 {
            call("clear");
        }
        if matches!(command.kind, k if k==Kind::Page as u8 || k==Kind::TextHandle as u8) {
            assert_eq!(
                unsafe {
                    paint.resource_valid.unwrap()(
                        paint.context,
                        command.resource,
                        command.generation,
                    )
                },
                1
            );
            unsafe {
                paint.resource.unwrap()(paint.context, core::ptr::null(), command, command.rect)
            };
        }
    }
    1
}
unsafe extern "C" fn paint_page(view: *const abi::XpEpubPageView, _: i32, _: i32, _: i32, _: u8) {
    assert!(xp_core::page::Page::open(unsafe {
        core::slice::from_raw_parts((*view).arena, (*view).size)
    })
    .is_ok());
    call("page");
}
unsafe extern "C" fn translate(_: u16) -> app_abi::Text {
    app_abi::Text::from_terminated(b"label\0")
}
unsafe extern "C" fn text_draw(
    _: *const xp_ui::scene::Command,
    _: xp_ui::scene::Rect,
    text: app_abi::Text,
) {
    assert!(!unsafe { text.bytes() }.unwrap().is_empty());
    call("text");
}
unsafe extern "C" fn width(_: app_abi::Text, _: i32, _: u8) -> i32 {
    40
}
unsafe extern "C" fn height(_: i32) -> i32 {
    10
}
unsafe extern "C" fn refresh(_: u8) {
    call("refresh");
}
unsafe extern "C" fn prewarm_begin(
    p: *mut crate::app::epub::render_resources::PrewarmStorage,
) -> u8 {
    unsafe {
        p.cast::<*mut crate::app::epub::render_resources::PrewarmStorage>()
            .write(p)
    };
    call("prewarm");
    1
}
unsafe extern "C" fn prewarm_finish(p: *mut crate::app::epub::render_resources::PrewarmStorage) {
    let pointer = unsafe { *p.cast::<*mut crate::app::epub::render_resources::PrewarmStorage>() };
    assert_eq!(pointer, p);
    call("scan-end");
}
unsafe extern "C" fn prewarm_drop(p: *mut crate::app::epub::render_resources::PrewarmStorage) {
    let pointer = unsafe { *p.cast::<*mut crate::app::epub::render_resources::PrewarmStorage>() };
    assert_eq!(
        unsafe { *pointer.cast::<*mut crate::app::epub::render_resources::PrewarmStorage>() },
        p
    );
    call("prewarm-drop");
}
unsafe extern "C" fn popup_layout(p: *mut xp_ui::dictionary::popup::Layout) {
    unsafe {
        *p = xp_ui::dictionary::popup::Layout {
            screen: screen(),
            font: 1,
            margin_x: 4,
            margin_y: 4,
            top_ratio: 0.4,
            ..Default::default()
        }
    };
}
fn screen() -> xp_ui::scene::Rect {
    xp_ui::scene::Rect {
        x: 0,
        y: 0,
        w: 480,
        h: 800,
    }
}
fn drawing_resources() -> app_abi::Resources {
    app_abi::Resources {
        paint_scene,
        translate,
        text_draw,
        text_width: width,
        line_height: height,
        refresh,
        ..test_resources::resources()
    }
}
#[test]
fn lookup_render_page_loan_and_prewarm_end_after_popup_refresh() {
    let mut leaves = test_leaves::leaves();
    leaves.paint_arena = paint_page;
    leaves.fonts_prewarm_begin = prewarm_begin;
    leaves.fonts_prewarm_finish_scan = prewarm_finish;
    leaves.fonts_prewarm_drop = prewarm_drop;
    leaves.child_selection_physical.dictionary_popup_layout = popup_layout;
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    let owner = Lookup::try_new(
        runtime(),
        page(),
        [4, 8],
        1,
        &ops,
        app_abi::Text::from_bytes(b"word"),
    )
    .unwrap();
    CALLS.with(|v| v.borrow_mut().clear());
    unsafe { owner.render(&drawing_resources(), &leaves, screen()) }.unwrap();
    CALLS.with(|v| {
        assert_eq!(
            *v.borrow(),
            [
                "clear",
                "prewarm",
                "page",
                "scan-end",
                "page",
                "text",
                "refresh",
                "prewarm-drop"
            ]
        )
    });
}
#[repr(C)]
struct Native {
    pointer: *mut LookupStorage,
}
unsafe extern "C" fn native_init(p: *mut LookupStorage, query: app_abi::Text) {
    assert_eq!(unsafe { query.bytes() }.unwrap(), b"word");
    unsafe { p.cast::<Native>().write(Native { pointer: p }) };
    NATIVE_INITS.with(|v| v.set(v.get() + 1));
}
unsafe extern "C" fn native_drop(p: *mut LookupStorage) {
    let original = unsafe { (*p.cast::<Native>()).pointer };
    assert_eq!(unsafe { (*original.cast::<Native>()).pointer }, p);
    NATIVE_DROPS.with(|v| v.set(v.get() + 1));
}
#[test]
fn lookup_releases_background_before_definition_and_native_resource_after_owner_moves() {
    PAGE_DROPS.with(|v| v.set(0));
    NATIVE_DROPS.with(|v| v.set(0));
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    let owner = Lookup::try_new(
        runtime(),
        page(),
        [0; 2],
        1,
        &ops,
        app_abi::Text::from_bytes(b"word"),
    )
    .unwrap();
    let mut parents = vec![owner];
    parents.reserve(16);
    let owner = parents.pop().unwrap();
    assert_eq!(PAGE_DROPS.with(Cell::get), 0);
    unsafe { owner.release_page() };
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    assert_eq!(NATIVE_DROPS.with(Cell::get), 0);
    drop(owner);
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    assert_eq!(NATIVE_DROPS.with(Cell::get), 1);
}
#[test]
fn lookup_owner_oom_drops_unpublished_page_without_native_init() {
    PAGE_DROPS.with(|v| v.set(0));
    NATIVE_INITS.with(|v| v.set(0));
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    let runtime = runtime();
    let page = page();
    fail_next_allocation();
    assert!(Lookup::try_new(
        runtime,
        page,
        [0; 2],
        1,
        &ops,
        app_abi::Text::from_bytes(b"word")
    )
    .is_none());
    assert_eq!(PAGE_DROPS.with(Cell::get), 1);
    assert_eq!(NATIVE_INITS.with(Cell::get), 0);
}

unsafe extern "C" fn region_read(_: xp_ui::scene::Rect, data: *mut u8, capacity: usize) -> u8 {
    assert_eq!(capacity, 4096);
    unsafe { data.write_bytes(0, capacity) };
    call("snapshot-read");
    1
}
unsafe extern "C" fn region_write(_: xp_ui::scene::Rect, data: *const u8, _: usize) -> u8 {
    assert_eq!(unsafe { *data }, 0);
    call("snapshot-write");
    1
}
unsafe extern "C" fn hints(labels: *const app_abi::Text) {
    for text in unsafe { core::slice::from_raw_parts(labels, 6) } {
        assert_eq!(unsafe { text.bytes() }.unwrap(), b"label");
    }
    call("hints");
}
fn footnote_page() -> Page {
    let mut bytes = vec![0; 2000];
    let mut builder = xp_core::page::builder::Builder::new(&mut bytes).unwrap();
    let mut a = [0; 256];
    a[..2].copy_from_slice(b"#a");
    let mut b = [0; 256];
    b[..2].copy_from_slice(b"#b");
    let mut number = [0; 32];
    number[0] = b'1';
    builder.footnote(&number, &a).unwrap();
    builder.footnote(&number, &b).unwrap();
    for (href, x) in [(&a, 10), (&b, 50)] {
        builder
            .link(xp_core::page::Link {
                href,
                x,
                y: 20,
                width: 10,
                height: 12,
            })
            .unwrap();
    }
    page_bytes(builder.finish().unwrap())
}

#[test]
fn footnote_render_borrows_number_and_uses_differential_snapshot() {
    use super::selection_owned::Footnote;
    let owner = Footnote::try_new(runtime(), footnote_page(), [4, 8]).unwrap();
    static RESOURCES: OnceLock<app_abi::Resources> = OnceLock::new();
    let resources = RESOURCES.get_or_init(|| app_abi::Resources {
        region_read,
        region_write,
        buffer_allocate: allocate,
        buffer_release: release,
        ..drawing_resources()
    });
    let mut leaves = test_leaves::leaves();
    leaves.paint_arena = paint_page;
    leaves.child_selection_physical.directional_hints = hints;
    unsafe { owner.enter(resources) };
    CALLS.with(|v| v.borrow_mut().clear());
    unsafe { owner.render(resources, &leaves, screen(), 1) }.unwrap();
    CALLS.with(|v| {
        assert_eq!(
            *v.borrow(),
            ["clear", "page", "snapshot-read", "text", "hints", "refresh"]
        )
    });
    let update = owner.input(xp_ui::model::Input {
        pressed: xp_ui::model::SCREEN_RIGHT,
        ..Default::default()
    });
    assert_ne!(update.effect.invalidate, 0);
    CALLS.with(|v| v.borrow_mut().clear());
    unsafe { owner.render(resources, &leaves, screen(), 1) }.unwrap();
    CALLS.with(|v| {
        assert_eq!(
            *v.borrow(),
            [
                "snapshot-write",
                "snapshot-read",
                "text",
                "hints",
                "refresh"
            ]
        )
    });
    let confirm = owner.input(xp_ui::model::Input {
        released: xp_ui::model::CONFIRM,
        ..Default::default()
    });
    assert_eq!(
        confirm.effect.kind,
        xp_ui::model::EffectKind::Activate as u8
    );
    assert_eq!(
        owner
            .selection(confirm.effect.value as usize)
            .unwrap()
            .bytes(),
        b"#b"
    );
    let cancel = owner.input(xp_ui::model::Input {
        released: xp_ui::model::BACK,
        ..Default::default()
    });
    assert_eq!(cancel.effect.kind, xp_ui::model::EffectKind::Cancel as u8);
}

unsafe extern "C" fn no_buffer(_: u8, _: usize) -> *mut u8 {
    core::ptr::null_mut()
}
#[test]
fn footnote_snapshot_oom_keeps_full_page_fallback() {
    let owner =
        super::selection_owned::Footnote::try_new(runtime(), footnote_page(), [4, 8]).unwrap();
    static RESOURCES: OnceLock<app_abi::Resources> = OnceLock::new();
    let resources = RESOURCES.get_or_init(|| app_abi::Resources {
        buffer_allocate: no_buffer,
        ..drawing_resources()
    });
    let mut leaves = test_leaves::leaves();
    leaves.paint_arena = paint_page;
    leaves.child_selection_physical.directional_hints = hints;
    unsafe { owner.enter(resources) };
    assert!(unsafe { (*owner.frame.get()).snapshot.is_none() });
    for _ in 0..2 {
        CALLS.with(|v| v.borrow_mut().clear());
        unsafe { owner.render(resources, &leaves, screen(), 1) }.unwrap();
        CALLS.with(|v| assert_eq!(*v.borrow(), ["clear", "page", "text", "hints", "refresh"]));
        owner.input(xp_ui::model::Input {
            pressed: xp_ui::model::SCREEN_RIGHT,
            ..Default::default()
        });
    }
}

#[path = "selection_app_tests.rs"]
mod app_stack;
