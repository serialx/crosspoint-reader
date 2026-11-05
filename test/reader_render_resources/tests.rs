use super::{abi, render_resources::*};
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
    collections::BTreeMap,
    pin::Pin,
};

#[cfg(feature = "reader-strips")]
#[path = "replay_tests.rs"]
mod replay_tests;

#[derive(Default)]
struct Trace {
    calls: usize,
    fail: usize,
    live: BTreeMap<usize, (usize, *mut u8)>,
    events: Vec<String>,
}
thread_local! { static TRACE: RefCell<Trace> = RefCell::default(); }
unsafe extern "C" fn allocate(domain: u8, len: usize) -> *mut u8 {
    assert_eq!(domain, abi::MEMORY_DEFAULT);
    TRACE.with(|trace| {
        let mut trace = trace.borrow_mut();
        trace.calls += 1;
        trace.events.push(format!("allocate:{len}"));
        if trace.calls == trace.fail {
            return std::ptr::null_mut();
        }
        let data = alloc(Layout::from_size_align(len, 8).unwrap());
        assert!(!data.is_null());
        data.write_bytes(0xa5, len);
        trace.live.insert(data.addr(), (len, data));
        data
    })
}
unsafe extern "C" fn release(domain: u8, data: *mut u8) {
    assert_eq!(domain, abi::MEMORY_DEFAULT);
    TRACE.with(|trace| {
        let mut trace = trace.borrow_mut();
        let (len, original) = trace.live.remove(&data.addr()).expect("live owner");
        assert_eq!(data, original);
        trace.events.push(format!("free:{len}"));
        dealloc(data, Layout::from_size_align(len, 8).unwrap());
    });
}
fn reset(fail: usize) {
    TRACE.with(|trace| {
        assert!(trace.borrow().live.is_empty());
        *trace.borrow_mut() = Trace {
            fail,
            ..Trace::default()
        };
    });
}
fn events() -> Vec<String> {
    TRACE.with(|trace| trace.borrow().events.clone())
}
fn clear_events() {
    TRACE.with(|trace| trace.borrow_mut().events.clear());
}
fn record(event: &str) {
    TRACE.with(|trace| trace.borrow_mut().events.push(event.into()));
}
fn pinned() -> Pin<Box<PageResources>> {
    let mut storage = Box::<PageResources>::new_uninit();
    unsafe {
        PageResources::initialize(storage.as_mut_ptr());
        Pin::new_unchecked(storage.assume_init())
    }
}
struct Physical {
    fail_begin: bool,
}
impl PhysicalResources for Physical {
    unsafe fn prewarm_begin(&self, storage: *mut PrewarmStorage) -> bool {
        record("prewarm:begin");
        if self.fail_begin {
            return false;
        }
        // A native-like token retains its construction address until destruction.
        (*storage).words[0] = storage.addr();
        (*storage).words[1] = 1;
        true
    }
    unsafe fn prewarm_finish(&self, storage: *mut PrewarmStorage) {
        assert_eq!((*storage).words[0], storage.addr());
        assert_eq!((*storage).words[1], 1);
        (*storage).words[1] = 0;
        record("prewarm:finish");
    }
    unsafe fn prewarm_drop(&self, storage: *mut PrewarmStorage) {
        assert_eq!((*storage).words[0], storage.addr());
        record("prewarm:drop");
    }
    fn detach_source(&self) {
        record("source:detach");
    }
    fn clear_plan(&self) {
        record("plan:clear");
    }
    fn clear_window(&self) {
        record("window:clear");
    }
    fn release_images(&self) {
        record("images:release");
    }
}

#[test]
fn frame_frees_owned_planes_before_scope_and_image_cleanup() {
    reset(0);
    let resources = pinned();
    let physical = Physical { fail_begin: false };
    let page = [1u8, 2, 3];
    let frame = resources.as_ref().begin_frame(&page, &physical);
    assert!(frame.allocate(&RESOURCES, PlaneBuffer::Lsb, 101));
    assert!(frame.allocate(&RESOURCES, PlaneBuffer::Msb, 102));
    assert!(frame.allocate(&RESOURCES, PlaneBuffer::Scratch, 103));
    assert!(frame.begin_prewarm());
    frame.finish_scan();
    clear_events();
    drop(frame);
    let mut expected = vec!["free:103", "free:102", "free:101"];
    if cfg!(feature = "reader-strips") {
        expected.extend(["plan:clear", "window:clear"]);
    }
    expected.extend(["prewarm:drop", "images:release"]);
    assert_eq!(events(), expected);
    drop(resources);
    reset(0);
}

#[test]
fn failed_prewarm_is_not_destroyed_and_an_ended_scope_is_destroyed() {
    reset(0);
    let resources = pinned();
    let failed = Physical { fail_begin: true };
    let frame = resources.as_ref().begin_frame(&[], &failed);
    assert!(!frame.begin_prewarm());
    drop(frame);
    assert!(!events().iter().any(|e| e == "prewarm:drop"));
    let physical = Physical { fail_begin: false };
    let scope = resources.as_ref().begin_idle_prewarm(&physical).unwrap();
    scope.finish_scan();
    drop(scope);
    assert_eq!(
        &events()[events().len() - 3..],
        ["prewarm:begin", "prewarm:finish", "prewarm:drop"]
    );
    drop(resources);
    reset(0);
}

#[test]
fn each_plane_failure_keeps_cleanup_balanced() {
    for fail in 1..=3 {
        reset(fail);
        let resources = pinned();
        let physical = Physical { fail_begin: false };
        let frame = resources.as_ref().begin_frame(&[], &physical);
        for (index, plane) in [PlaneBuffer::Lsb, PlaneBuffer::Msb, PlaneBuffer::Scratch]
            .into_iter()
            .enumerate()
        {
            assert_eq!(
                frame.allocate(&RESOURCES, plane, 100 + index),
                index + 1 != fail
            );
        }
        drop(frame);
        drop(resources);
        reset(0);
    }
}

#[cfg(feature = "reader-strips")]
#[test]
fn strip_failures_release_partial_buffers_in_parent_order_and_retry() {
    for fail in 1..=4 {
        reset(fail);
        let resources = pinned();
        assert!(!resources.as_ref().prepare_strips(&RESOURCES, 99, true));
        TRACE.with(|trace| assert!(trace.borrow().live.is_empty()));
        let freed: Vec<_> = events()
            .into_iter()
            .filter(|e| e.starts_with("free:"))
            .collect();
        let expected: Vec<_> = [8000, 16384, 4096, 4096]
            .into_iter()
            .enumerate()
            .filter(|(i, _)| *i + 1 != fail)
            .map(|(_, n)| format!("free:{n}"))
            .collect();
        assert_eq!(freed, expected);
        assert!(resources.as_ref().prepare_strips(&RESOURCES, 100, true));
        clear_events();
        assert!(resources.as_ref().prepare_strips(&RESOURCES, 100, true));
        assert!(events().is_empty());
        assert!(resources.as_ref().release_idle_strips());
        drop(resources);
        reset(0);
    }
}

#[cfg(feature = "reader-strips")]
#[test]
fn source_loan_keeps_borrowed_scratch_until_detach() {
    reset(0);
    let resources = pinned();
    assert!(!resources.as_ref().prepare_strips(&RESOURCES, 98, false));
    assert!(events().is_empty());
    assert!(resources.as_ref().prepare_strips(&RESOURCES, 99, true));
    let physical = Physical { fail_begin: false };
    let frame = resources.as_ref().begin_frame(&[], &physical);
    assert!(frame.borrow_scratch());
    frame.source_installed(true);
    clear_events();
    assert!(!resources.as_ref().release_idle_strips());
    assert!(!resources.as_ref().prepare_strips(&RESOURCES, 100, false));
    assert!(events().is_empty());
    drop(frame);
    assert_eq!(
        events(),
        [
            "source:detach",
            "plan:clear",
            "window:clear",
            "images:release"
        ]
    );
    assert!(resources.as_ref().release_idle_strips());
    assert_eq!(
        &events()[4..],
        ["free:8000", "free:16384", "free:4096", "free:4096"]
    );
    drop(resources);
    reset(0);
}
unsafe extern "C" fn unexpected0<R>() -> R {
    panic!("unexpected physical leaf")
}
unsafe extern "C" fn unexpected1<A0, R>(_: A0) -> R {
    panic!("unexpected physical leaf")
}
unsafe extern "C" fn unexpected2<A0, A1, R>(_: A0, _: A1) -> R {
    panic!("unexpected physical leaf")
}
unsafe extern "C" fn unexpected3<A0, A1, A2, R>(_: A0, _: A1, _: A2) -> R {
    panic!("unexpected physical leaf")
}
unsafe extern "C" fn unexpected4<A0, A1, A2, A3, R>(_: A0, _: A1, _: A2, _: A3) -> R {
    panic!("unexpected physical leaf")
}
unsafe extern "C" fn unexpected5<A0, A1, A2, A3, A4, R>(_: A0, _: A1, _: A2, _: A3, _: A4) -> R {
    panic!("unexpected physical leaf")
}
static RESOURCES: abi::Resources = abi::Resources {
    settings_snapshot: unexpected0,
    recents_visit: unexpected2,
    recent_missing: unexpected1,
    recent_update: unexpected1,
    opds_count: unexpected0,
    book_progress: unexpected1,
    library_open: unexpected0,
    library_count: unexpected1,
    library_row: unexpected5,
    library_close: unexpected1,
    library_rebuild: unexpected1,
    book_open: unexpected2,
    book_thumb_path: unexpected4,
    book_load: unexpected3,
    book_generate_thumb: unexpected2,
    book_generate_thumb_from_source: unexpected2,
    book_close: unexpected1,
    path_exists: unexpected1,
    thumbnail_path: unexpected4,
    bitmap_open: unexpected1,
    bitmap_info: unexpected1,
    bitmap_draw: unexpected3,
    bitmap_close: unexpected1,
    theme_snapshot: unexpected1,
    translate: unexpected1,
    header_draw: unexpected2,
    footer_draw: unexpected1,
    placeholder_draw: unexpected1,
    popup_draw: unexpected1,
    popup_progress: unexpected2,
    icon_draw: unexpected2,
    text_draw: unexpected3,
    text_width: unexpected3,
    line_height: unexpected1,
    text_wrap: unexpected4,
    text_truncate: unexpected4,
    text_box_height: unexpected2,
    region_size: unexpected1,
    region_read: unexpected3,
    region_write: unexpected3,
    rounded_mask: unexpected3,
    polygon_draw: unexpected3,
    paint_scene: unexpected2,
    refresh: unexpected1,
    buffer_allocate: allocate,
    buffer_release: release,
};

#[test]
fn new_buffers_preserve_native_value_initialization() {
    reset(0);
    let resources = pinned();
    let physical = Physical { fail_begin: false };
    let frame = resources.as_ref().begin_frame(&[], &physical);
    assert!(frame.allocate(&RESOURCES, PlaneBuffer::Lsb, 101));
    let bytes = unsafe { std::slice::from_raw_parts(frame.buffer(PlaneBuffer::Lsb), 101) };
    assert!(bytes.iter().all(|byte| *byte == 0));
    drop(frame);
    drop(resources);
    reset(0);
}

#[cfg(feature = "reader-strips")]
#[test]
fn image_window_failure_releases_previous_text_reservations() {
    reset(0);
    let resources = pinned();
    assert!(resources.as_ref().prepare_strips(&RESOURCES, 100, false));
    TRACE.with(|trace| trace.borrow_mut().fail = 4);
    clear_events();
    assert!(!resources.as_ref().prepare_strips(&RESOURCES, 100, true));
    assert_eq!(
        events(),
        ["allocate:4096", "free:8000", "free:16384", "free:4096"]
    );
    drop(resources);
    reset(0);
}

#[cfg(feature = "reader-strips")]
#[test]
fn rejected_source_retains_plan_and_window_storage_until_frame_cleanup() {
    reset(0);
    let resources = pinned();
    assert!(resources.as_ref().prepare_strips(&RESOURCES, 99, true));
    let physical = Physical { fail_begin: false };
    let frame = resources.as_ref().begin_frame(&[], &physical);
    frame.plan_installed();
    frame.window_installed();
    frame.source_installed(false);
    clear_events();
    assert!(!resources.as_ref().release_idle_strips());
    drop(frame);
    assert_eq!(events(), ["plan:clear", "window:clear", "images:release"]);
    drop(resources);
    assert_eq!(
        &events()[3..],
        ["free:8000", "free:16384", "free:4096", "free:4096"]
    );
    reset(0);
}

#[cfg(feature = "reader-strips")]
struct Detach<'a> {
    check: &'a dyn Fn(),
    base: Physical,
}
#[cfg(feature = "reader-strips")]
impl PhysicalResources for Detach<'_> {
    unsafe fn prewarm_begin(&self, p: *mut PrewarmStorage) -> bool {
        self.base.prewarm_begin(p)
    }
    unsafe fn prewarm_finish(&self, p: *mut PrewarmStorage) {
        self.base.prewarm_finish(p)
    }
    unsafe fn prewarm_drop(&self, p: *mut PrewarmStorage) {
        self.base.prewarm_drop(p)
    }
    fn detach_source(&self) {
        (self.check)();
        self.base.detach_source();
    }
    fn clear_plan(&self) {
        self.base.clear_plan();
    }
    fn clear_window(&self) {
        self.base.clear_window();
    }
    fn release_images(&self) {
        self.base.release_images();
    }
}

#[cfg(feature = "reader-strips")]
#[test]
fn detach_callback_reborrows_live_storage_after_owned_planes_are_released() {
    reset(0);
    let resources = pinned();
    assert!(resources.as_ref().prepare_strips(&RESOURCES, 99, true));
    let page = vec![42; 31];
    let check = || {
        assert_eq!(page[0], 42);
        assert!(!resources.as_ref().release_idle_strips());
        assert!(!resources.as_ref().prepare_strips(&RESOURCES, 99, true));
        TRACE.with(|trace| {
            let trace = trace.borrow();
            let mut sizes: Vec<_> = trace.live.values().map(|(size, _)| *size).collect();
            sizes.sort();
            assert_eq!(sizes, [4096, 4096, 8000, 16384]);
        });
    };
    let physical = Detach {
        check: &check,
        base: Physical { fail_begin: false },
    };
    let frame = resources.as_ref().begin_frame(&page, &physical);
    assert!(frame.allocate(&RESOURCES, PlaneBuffer::Lsb, 33));
    assert!(frame.allocate(&RESOURCES, PlaneBuffer::Msb, 34));
    assert!(frame.borrow_scratch());
    let scratch = frame.buffer(PlaneBuffer::Scratch);
    assert!(!scratch.is_null());
    unsafe { scratch.write(0x5a) };
    frame.source_installed(true);
    assert!(frame.begin_prewarm());
    clear_events();
    drop(frame);
    assert_eq!(
        events(),
        [
            "free:34",
            "free:33",
            "source:detach",
            "plan:clear",
            "window:clear",
            "prewarm:drop",
            "images:release"
        ]
    );
    assert_eq!(unsafe { scratch.read() }, 0x5a);
    drop(resources);
    reset(0);
}

#[test]
fn repeated_prewarm_and_frames_leave_no_reservations() {
    reset(0);
    let resources = pinned();
    let physical = Physical { fail_begin: false };
    for _ in 0..128 {
        let scope = resources.as_ref().begin_idle_prewarm(&physical).unwrap();
        drop(scope); // Scanning scopes also require destruction.
        let frame = resources.as_ref().begin_frame(&[], &physical);
        assert!(frame.begin_prewarm());
        frame.finish_scan();
        drop(frame);
    }
    TRACE.with(|trace| assert_eq!(trace.borrow().calls, 0));
    drop(resources);
    reset(0);
}

#[cfg(feature = "reader-strips")]
#[test]
fn borrowed_scratch_blocks_idle_release_even_without_a_source() {
    reset(0);
    let resources = pinned();
    assert!(resources.as_ref().prepare_strips(&RESOURCES, 99, false));
    let physical = Physical { fail_begin: false };
    let frame = resources.as_ref().begin_frame(&[], &physical);
    assert!(frame.borrow_scratch());
    assert!(!resources.as_ref().release_idle_strips());
    assert!(!resources.as_ref().prepare_strips(&RESOURCES, 99, false));
    drop(frame);
    assert!(resources.as_ref().release_idle_strips());
    drop(resources);
    reset(0);
}
