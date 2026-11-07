use super::{
    definition_owned::{DefinitionOwner, PayloadSlot},
    definition_physical::*,
    selection_physical::{DefinitionFill, LookupStorage},
    toolbar_owner::tests::test_leaves,
};
use crate::app::{
    abi,
    epub::abi::{XpEpubHeap, XpEpubLeaves},
    test_resources,
};
use core::{
    cell::{Cell, RefCell},
    ffi::c_void,
    ptr,
};
use std::{
    alloc::{alloc, dealloc, Layout},
    sync::OnceLock,
};
thread_local! {
    static EVENTS: RefCell<Vec<&'static str>> = const { RefCell::new(Vec::new()) };
    static DEFAULT_HEAP: Cell<(u32,u32)> = const { Cell::new((100_000, 50_000)) };
    static PREPARE_OK: Cell<bool> = const { Cell::new(true) };
    static MALLOC_FAIL: Cell<bool> = const { Cell::new(false) };
    static BLOCKS: RefCell<Vec<(*mut u8,Layout)>> = const { RefCell::new(Vec::new()) };
    static FIND_CODES: RefCell<std::collections::VecDeque<u8>> = const { RefCell::new(std::collections::VecDeque::new()) };
    static PREPARE_VIEWS: RefCell<std::collections::VecDeque<Option<PayloadView>>> = const { RefCell::new(std::collections::VecDeque::new()) };
    static LOGS: RefCell<Vec<(u8,Vec<u8>,Vec<u8>)>> = const { RefCell::new(Vec::new()) };
    static LOCKED: Cell<bool> = const { Cell::new(false) };
}
fn event(s: &'static str) {
    EVENTS.with(|v| v.borrow_mut().push(s));
}
#[no_mangle]
unsafe extern "C" fn xp_test_definition_malloc(size: usize) -> *mut c_void {
    event("malloc");
    if MALLOC_FAIL.with(Cell::get) {
        return ptr::null_mut();
    }
    let layout = Layout::from_size_align(size, 1).unwrap();
    let block = unsafe { alloc(layout) };
    BLOCKS.with(|v| v.borrow_mut().push((block, layout)));
    block.cast()
}
#[no_mangle]
unsafe extern "C" fn xp_test_definition_free(block: *mut c_void) {
    event("free");
    BLOCKS.with(|v| {
        let mut v = v.borrow_mut();
        let i = v.iter().position(|(p, _)| *p == block.cast()).unwrap();
        let (p, l) = v.remove(i);
        unsafe { dealloc(p, l) };
    });
}
#[no_mangle]
unsafe extern "C" fn xp_test_definition_copied(_: *const u8, _: *mut u8, _: usize) {
    assert!(!LOCKED.with(Cell::get));
    event("copy");
}
unsafe extern "C" fn empty() {}
unsafe extern "C" fn now() -> u32 {
    10
}
unsafe extern "C" fn notify() {
    event("notify");
}
unsafe extern "C" fn lock() -> *mut c_void {
    assert!(!LOCKED.with(|v| v.replace(true)));
    ptr::NonNull::<u8>::dangling().as_ptr().cast()
}
unsafe extern "C" fn unlock(_: *mut c_void) {
    assert!(LOCKED.with(|v| v.replace(false)));
}
fn runtime() -> &'static abi::Runtime {
    static VALUE: OnceLock<abi::Runtime> = OnceLock::new();
    VALUE.get_or_init(|| abi::Runtime {
        model_lock: empty,
        model_unlock: empty,
        now_ms: now,
        notify_render: notify,
        render_lock: lock,
        render_unlock: unlock,
        ..test_resources::runtime()
    })
}
unsafe extern "C" fn plain(
    p: *mut PayloadStorage,
    _: abi::Text,
    _: u32,
    _: DefinitionFill,
    _: *mut c_void,
) -> u8 {
    unsafe { p.cast::<*mut PayloadStorage>().write(p) };
    event("construct");
    1
}
unsafe extern "C" fn lookup(
    p: *mut PayloadStorage,
    _: *mut LookupStorage,
    sink: abi::TextSink,
    context: *mut c_void,
    html: *mut u8,
) -> u8 {
    if unsafe { sink(context, abi::Text::from_bytes(b"initial")) } == 0 {
        return 0;
    }
    unsafe {
        html.write(0);
        p.cast::<*mut PayloadStorage>().write(p);
    }
    event("construct");
    event("source-swap");
    1
}
unsafe extern "C" fn destroy(p: *mut PayloadStorage) {
    let saved = unsafe { p.cast::<*mut PayloadStorage>().read() };
    assert_eq!(saved, p);
    assert_eq!(unsafe { saved.cast::<*mut PayloadStorage>().read() }, p);
    event("destroy");
}
unsafe extern "C" fn prepare(
    _: *mut PayloadStorage,
    _: u8,
    _: u8,
    _: u8,
    _: xp_ui::scene::Rect,
    out: *mut PayloadView,
) -> u8 {
    // Poison output even on failure; the caller must not publish these facts.
    unsafe {
        out.write(PayloadView {
            pages: -31337,
            styled: 255,
            reserved: [255; 3],
        });
    }
    if let Some(view) = PREPARE_VIEWS.with(|v| v.borrow_mut().pop_front()) {
        if let Some(view) = view {
            unsafe { out.write(view) };
            event("prepare");
            return 1;
        }
        event("prepare-failed");
        return 0;
    }
    if !PREPARE_OK.with(Cell::get) {
        event("prepare-failed");
        return 0;
    }
    unsafe {
        out.write(PayloadView {
            pages: 7,
            styled: 0,
            reserved: [0; 3],
        });
    }
    event("prepare");
    1
}
unsafe extern "C" fn heap(out: *mut XpEpubHeap) {
    event("default-heap");
    let (free_bytes, largest_block) = DEFAULT_HEAP.with(Cell::get);
    unsafe {
        out.write(XpEpubHeap {
            free_bytes,
            largest_block,
        })
    };
}
unsafe extern "C" fn internal(out: *mut XpEpubHeap) {
    event("internal-heap");
    unsafe {
        out.write(XpEpubHeap {
            free_bytes: 0,
            largest_block: 0,
        })
    };
}
unsafe extern "C" fn selection_release(_: *mut PayloadStorage) {
    event("selection-release");
}
unsafe extern "C" fn release(_: *mut PayloadStorage, _: u8, _: u8) {
    event("payload-release");
}
unsafe extern "C" fn commit(_: *mut PayloadStorage) {
    event("commit");
}
unsafe extern "C" fn find(
    _: *mut PayloadStorage,
    _: u8,
    _: *mut LookupStorage,
    _: abi::Text,
    out: *mut u8,
) -> u8 {
    event("find");
    let code = FIND_CODES.with(|v| v.borrow_mut().pop_front()).unwrap_or(0);
    unsafe { out.write(code) };
    u8::from(code == 0)
}
unsafe extern "C" fn fonts() {
    event("fonts-release");
}
unsafe extern "C" fn wait() {
    event("wait");
}
unsafe extern "C" fn enabled(_: u8) -> u8 {
    1
}
unsafe extern "C" fn log(level: u8, tag: abi::Text, message: abi::Text) {
    event("log");
    LOGS.with(|v| {
        v.borrow_mut().push((
            level,
            unsafe { tag.bytes().unwrap() }.to_vec(),
            unsafe { message.bytes().unwrap() }.to_vec(),
        ))
    });
}
unsafe extern "C" fn fill(_: *mut c_void, _: *mut u8, _: u32) -> u8 {
    1
}
const fn physical() -> DefinitionPhysical {
    let mut p = DefinitionPhysical::unused();
    p.payload_plain = plain;
    p.payload_lookup = lookup;
    p.payload_drop = destroy;
    p.payload_prepare = prepare;
    p.heap = heap;
    p.selection_release = selection_release;
    p.payload_release = release;
    p.payload_commit = commit;
    p.payload_find = find;
    p.layout = layout;
    p
}
static PHYSICAL: DefinitionPhysical = physical();
fn leaves() -> XpEpubLeaves {
    let mut l = test_leaves::leaves();
    l.heap_snapshot = internal;
    l.log = log;
    l.log_enabled = enabled;
    l.fonts_release_caches = fonts;
    l.child_selection_physical.wait_paint = wait;
    l
}
fn owner() -> DefinitionOwner {
    unsafe {
        DefinitionOwner::try_plain(
            runtime(),
            &PHYSICAL,
            abi::Text::from_bytes(b"title"),
            0,
            fill,
            ptr::null_mut(),
        )
    }
    .unwrap()
}
fn selected(text: &[u8]) -> abi::XpAppResultView {
    abi::XpAppResultView {
        original: ptr::null(),
        kind: abi::RESULT_CLIPPING,
        cancelled: 0,
        value: abi::XpAppResultPayload {
            clipping: abi::XpAppClipResult {
                text: abi::XpAppNativeText {
                    bytes: abi::Text::from_bytes(text),
                    native_string: ptr::null(),
                },
                start_page_offset: 0,
                end_page_offset: 1,
                start_word: 0,
                end_word: 1,
                word_count: 2,
                start_offset: 0,
                end_offset: 0,
                action: 1,
            },
        },
    }
}
fn events() -> Vec<&'static str> {
    EVENTS.with(|v| mem_take(&mut v.borrow_mut()))
}
fn mem_take(v: &mut Vec<&'static str>) -> Vec<&'static str> {
    core::mem::take(v)
}
#[test]
fn failed_prepare_sentinel_cannot_publish_and_candidate_commits_with_facts() {
    let owner = owner();
    owner.model.with(|m| {
        m.state.page = 2;
        m.state.pages = 11;
        m.styled = true;
    });
    PREPARE_OK.with(|v| v.set(false));
    assert!(unsafe { owner.prepare(PayloadSlot::Candidate, false) }.is_none());
    owner.model.with(|m| {
        assert_eq!(m.state.pages, 11);
        assert_eq!(m.state.page, 2);
        assert!(m.styled);
    });
    PREPARE_OK.with(|v| v.set(true));
    unsafe {
        (*owner.navigation.get()).pending.assign(b"next");
    }
    events();
    unsafe { owner.navigate(false, runtime(), &leaves()) };
    let events = events();
    assert!(
        events.iter().position(|e| *e == "prepare").unwrap()
            < events.iter().position(|e| *e == "commit").unwrap()
    );
    owner.model.with(|m| {
        assert_eq!(m.state.pages, 7);
        assert_eq!(m.state.page, 0);
        assert_eq!(m.state.history_depth, 1);
        assert!(!m.styled);
    });
}
#[test]
fn default_heap_not_internal_controls_selection_admission_after_page_release() {
    let owner = owner();
    let leaves = leaves();
    events();
    DEFAULT_HEAP.with(|v| v.set((8195, 4)));
    unsafe { owner.selection_result(runtime(), &leaves, &selected(b"abc")) };
    let seen = events();
    assert_eq!(&seen[..3], &["selection-release", "default-heap", "malloc"]);
    assert!(!seen.contains(&"internal-heap"));
    assert_eq!(unsafe { (*owner.navigation.get()).pending.view().len }, 3);
    for (free, largest) in [(8194, 4), (8195, 3)] {
        DEFAULT_HEAP.with(|v| v.set((free, largest)));
        events();
        unsafe { owner.selection_result(runtime(), &leaves, &selected(b"abc")) };
        let seen = events();
        assert_eq!(&seen[..2], &["selection-release", "default-heap"]);
        assert!(!seen.contains(&"malloc"));
        assert!(seen.contains(&"log"));
    }
}
#[test]
fn initial_query_malloc_failure_never_constructs_or_releases_source() {
    MALLOC_FAIL.with(|v| v.set(true));
    events();
    assert!(unsafe {
        DefinitionOwner::try_lookup(
            runtime(),
            &PHYSICAL,
            ptr::NonNull::<LookupStorage>::dangling().as_ptr(),
        )
    }
    .is_none());
    MALLOC_FAIL.with(|v| v.set(false));
    assert_eq!(events(), vec!["malloc"]);
    assert!(BLOCKS.with(|v| v.borrow().is_empty()));
}
#[test]
fn stable_owner_moves_keep_native_interior_pointer_until_drop() {
    let owner = owner();
    let address = owner.pointer();
    let mut parents = Vec::new();
    parents.push(owner);
    parents.reserve(32);
    let owner = parents.pop().unwrap();
    assert_eq!(owner.pointer(), address);
    events();
    drop(owner);
    assert_eq!(events(), vec!["destroy"]);
}
thread_local! {
    static HEADER_RECT: Cell<Option<(i32,i32,i32,i32)>> = const { Cell::new(None) };
    static LONG_POINT: Cell<Option<(i32,i32)>> = const { Cell::new(None) };
}
unsafe extern "C" fn long_press(kind: u8, x: *mut i32, y: *mut i32) -> u8 {
    assert_eq!(kind, 0);
    assert!(!LOCKED.with(Cell::get));
    event("long");
    if let Some((px, py)) = LONG_POINT.with(Cell::get) {
        unsafe {
            x.write(px);
            y.write(py)
        };
        1
    } else {
        0
    }
}
unsafe extern "C" fn input_layout(
    out: *mut xp_ui::dictionary::definition::Layout,
    x: i32,
    y: i32,
) -> u8 {
    if (x, y) == (-1, -1) {
        event("body-layout");
    } else {
        assert!(!LOCKED.with(Cell::get));
        event("header-hit");
    }
    unsafe {
        out.write(xp_ui::dictionary::definition::Layout {
            screen: xp_ui::scene::Rect {
                x: 0,
                y: 0,
                w: 480,
                h: 800,
            },
            ..Default::default()
        })
    };
    u8::from(
        HEADER_RECT
            .with(Cell::get)
            .is_some_and(|(rx, ry, w, h)| w > 0 && x >= rx && x < rx + w && y >= ry && y < ry + h),
    )
}
unsafe extern "C" fn snapshot(out: *mut xp_ui::model::Input) {
    event("snapshot");
    unsafe {
        out.write(xp_ui::model::Input {
            now_ms: 10,
            ..Default::default()
        })
    };
}
unsafe extern "C" fn tap(_: *mut i32, _: *mut i32) -> u8 {
    event("tap");
    0
}
unsafe extern "C" fn select_child(
    _: *mut PayloadStorage,
    _: *const SelectionRequest,
    _: xp_ui::scene::Rect,
) -> *mut c_void {
    event("selection");
    ptr::NonNull::<u8>::dangling().as_ptr().cast()
}
#[test]
fn long_press_uses_recorded_header_hit_after_sampling_and_clear_removes_cancel() {
    static INPUT_PHYSICAL: DefinitionPhysical = {
        let mut p = physical();
        p.layout = input_layout;
        p.selection_child = select_child;
        p
    };
    let mut leaves = leaves();
    leaves.input_snapshot = snapshot;
    leaves.input_screen_point = long_press;
    leaves.input_screen_tap = tap;
    for (point, target, cancel) in [
        ((15, 25), Some((10, 20, 40, 40)), true),
        ((70, 25), Some((10, 20, 40, 40)), false),
        ((15, 25), None, false),
    ] {
        let owner = unsafe {
            DefinitionOwner::try_lookup(
                runtime(),
                &INPUT_PHYSICAL,
                ptr::NonNull::<LookupStorage>::dangling().as_ptr(),
            )
        }
        .unwrap();
        owner.model.with(|m| m.styled = true);
        HEADER_RECT.with(|v| v.set(target));
        LONG_POINT.with(|v| v.set(Some(point)));
        events();
        let child = unsafe { owner.input(runtime(), &leaves, true) };
        let seen = events();
        assert_eq!(&seen[..4], &["long", "header-hit", "snapshot", "tap"]);
        assert_eq!(child.is_null(), cancel);
        assert_eq!(
            unsafe { *owner.result.get() },
            if cancel { Some(true) } else { None }
        );
    }
}

unsafe extern "C" fn layout(out: *mut xp_ui::dictionary::definition::Layout, x: i32, y: i32) -> u8 {
    assert_eq!((x, y), (-1, -1));
    unsafe { out.write(xp_ui::dictionary::definition::Layout::default()) };
    0
}

#[test]
fn history_depth_recovery_and_log_order_preserve_native_contract() {
    use xp_core::i18n::ids::*;
    let leaves = leaves();
    let owner = owner();
    events();
    LOGS.with(|v| v.borrow_mut().clear());
    unsafe { owner.enter(runtime(), &leaves) };
    assert_eq!(&events()[..2], &["log", "prepare"]);
    assert_eq!(
        LOGS.with(|v| v.borrow()[0].clone()),
        (
            0,
            b"ACT".to_vec(),
            b"Entering activity: DictionaryDefinition".to_vec()
        )
    );
    DEFAULT_HEAP.with(|v| v.set((100000, 50000)));
    for depth in 0..16 {
        owner.model.with(|m| m.state.page = depth % 7);
        unsafe {
            owner.selection_result(runtime(), &leaves, &selected(b"next"));
            owner.navigate(false, runtime(), &leaves)
        };
        owner
            .model
            .with(|m| assert_eq!(m.state.history_depth, depth as u8 + 1));
    }
    events();
    unsafe { owner.selection_result(runtime(), &leaves, &selected(b"overflow")) };
    assert_eq!(events(), vec!["selection-release"]);
    owner
        .model
        .with(|m| assert_eq!(m.state.message, STR_DICT_HISTORY_FULL));
    for depth in (0..16).rev() {
        unsafe { owner.navigate(true, runtime(), &leaves) };
        owner.model.with(|m| {
            assert_eq!(m.state.history_depth, depth);
            assert_eq!(m.state.page, 1);
        });
    }
    // A failed forward lookup restores the current entry without changing history.
    for (code, message) in [
        (1, STR_DICT_NOT_FOUND),
        (2, STR_DICT_LOW_MEMORY),
        (3, STR_DICT_DECOMPRESS_ERROR),
        (4, STR_DICT_READ_FAILED),
    ] {
        FIND_CODES.with(|v| v.borrow_mut().extend([code, 0]));
        unsafe {
            (*owner.navigation.get()).pending.assign(b"missing");
            owner.navigate(false, runtime(), &leaves)
        };
        owner.model.with(|m| {
            assert_eq!(m.state.message, message);
            assert_eq!(m.state.history_depth, 0);
            assert_eq!(m.state.page, 1);
        });
        assert_eq!(unsafe { *owner.result.get() }, None);
    }
    // Poisoned failed candidate output cannot replace the recovered payload facts.
    PREPARE_VIEWS.with(|v| {
        v.borrow_mut().extend([
            None,
            Some(PayloadView {
                pages: 7,
                ..Default::default()
            }),
        ])
    });
    unsafe {
        (*owner.navigation.get()).pending.assign(b"candidate");
        owner.navigate(false, runtime(), &leaves)
    };
    owner
        .model
        .with(|m| assert_eq!((m.state.pages, m.state.message), (7, STR_DICT_LOW_MEMORY)));
    FIND_CODES.with(|v| v.borrow_mut().extend([1, 4]));
    unsafe {
        (*owner.navigation.get()).pending.assign(b"lost");
        owner.navigate(false, runtime(), &leaves)
    };
    assert_eq!(unsafe { *owner.result.get() }, Some(false));
    assert!(
        LOGS.with(|v| v.borrow().iter().any(|(level, tag, msg)| *level == 0
            && tag == b"DICT"
            && msg == b"Navigation after release: free=0 largest=0"))
    );
    events();
    unsafe { owner.exit(&leaves) };
    assert_eq!(events(), vec!["log", "fonts-release"]);
    assert_eq!(
        LOGS.with(|v| v.borrow().last().unwrap().clone()),
        (
            0,
            b"ACT".to_vec(),
            b"Exiting activity: DictionaryDefinition".to_vec()
        )
    );
}
#[test]
fn failed_back_layout_finishes_without_publishing_candidate_or_consuming_history() {
    let owner = owner();
    let leaves = leaves();
    unsafe {
        (*owner.navigation.get()).pending.assign(b"next");
        owner.navigate(false, runtime(), &leaves)
    };
    PREPARE_VIEWS.with(|v| {
        v.borrow_mut().push_back(Some(PayloadView {
            pages: 7,
            styled: 1,
            reserved: [0; 3],
        }))
    });
    events();
    unsafe { owner.navigate(true, runtime(), &leaves) };
    assert!(!events().contains(&"commit"));
    owner.model.with(|m| {
        assert_eq!(m.state.history_depth, 1);
        assert!(!m.styled);
    });
    assert_eq!(unsafe { *owner.result.get() }, Some(false));
}

#[path = "definition_render_tests.rs"]
mod render_tests;
