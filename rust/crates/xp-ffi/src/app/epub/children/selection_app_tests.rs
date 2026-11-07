//! Real App stack with physical native viewer/selection witnesses.
use super::super as children;
use super::*;
use crate::app::{self, abi as wire, stable_owner::StableOwner, sync::ModelCell};
use core::{
    cell::{RefCell, UnsafeCell},
    ffi::c_void,
    ptr::NonNull,
};
use xp_ui::app::{Application, Services, Transition};

thread_local! {
    static LOCKED: Cell<bool> = const { Cell::new(false) };
    static CLIP_LIVE: Cell<bool> = const { Cell::new(false) };
    static CANCELLED: Cell<u8> = const { Cell::new(0) };
}
unsafe extern "C" fn snapshot(poll: u8, full: u8, consume: u8, out: *mut wire::MappedSnapshot) {
    assert_eq!((poll, full, consume), (0, 0, 1));
    // Stop this sampling probe before loop side effects; neither lookup nor viewer
    // may consume the native definition viewer's full button frame.
    let mut frame: wire::MappedSnapshot = unsafe { core::mem::zeroed() };
    frame.suppressed_release = 1;
    unsafe { out.write(frame) };
}
unsafe extern "C" fn render_lock() -> *mut c_void {
    LOCKED.with(|v| assert!(!v.replace(true)));
    NonNull::<u8>::dangling().as_ptr().cast()
}
unsafe extern "C" fn render_unlock(_: *mut c_void) {
    LOCKED.with(|v| assert!(v.replace(false)));
    call("unlock");
}
unsafe extern "C" fn allowed(_: u8) -> u8 {
    1
}
unsafe extern "C" fn quiet(_: u8) -> u8 {
    0
}
unsafe extern "C" fn now() -> u32 {
    100
}
unsafe extern "C" fn diagnostic(kind: u8, a: u32, b: u32) {
    assert_eq!((kind, a, b), (2, 1, 0));
}
unsafe extern "C" fn empty() {}
unsafe extern "C" fn native_event(_: *mut c_void) {}
unsafe extern "C" fn no_create(_: *const wire::Launch) -> *mut c_void {
    panic!("create")
}
unsafe extern "C" fn no_render(_: *mut c_void, _: *mut wire::RenderToken) {
    panic!("render")
}
unsafe extern "C" fn no_facts(_: *const c_void, _: u8, _: u8) -> wire::LegacyFacts {
    panic!("facts")
}
unsafe extern "C" fn no_action(_: *mut c_void) -> u8 {
    panic!("action")
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    panic!("turn")
}
unsafe extern "C" fn no_screenshot(_: *const c_void, _: wire::ScreenshotSink, _: *mut c_void) {
    panic!("screenshot")
}
unsafe extern "C" fn result(_: *mut c_void, sink: wire::ResultSink, context: *mut c_void) {
    call("native-take");
    let original = 7_u8;
    let view = wire::XpAppResultView {
        original: (&original as *const u8).cast(),
        value: wire::XpAppResultPayload {
            clipping: wire::XpAppClipResult {
                text: wire::XpAppNativeText {
                    bytes: wire::Text::from_bytes(b"word"),
                    native_string: core::ptr::null(),
                },
                start_page_offset: 0,
                end_page_offset: 0,
                start_word: 0,
                end_word: 1,
                word_count: 1,
                start_offset: 0,
                end_offset: 0,
                action: 1,
            },
        },
        kind: wire::RESULT_CLIPPING,
        cancelled: CANCELLED.with(Cell::get),
    };
    unsafe { sink(context, &view) };
    call("native-result-drop");
}
unsafe extern "C" fn destroy(pointer: *mut c_void) {
    // Only the native ClipSelection witness is separately heap-owned.
    if CLIP_LIVE.with(|v| v.replace(false)) {
        LOCKED.with(|v| assert!(v.get()));
        let loan = unsafe { Box::from_raw(pointer.cast::<*mut c_void>()) };
        assert!(!loan.is_null());
        call("clip-drop");
    }
}
unsafe extern "C" fn delivered(_: *mut c_void, _: *const c_void) {
    panic!("Definition selection must use owned delivery")
}
unsafe extern "C" fn drop_definition(p: *mut children::definition_physical::PayloadStorage) {
    CLIP_LIVE.with(|v| assert!(!v.get()));
    unsafe { definition_drop(p) };
}
fn app_fixture() -> app::App {
    static RUNTIME: OnceLock<wire::Runtime> = OnceLock::new();
    static RESOURCES: OnceLock<wire::Resources> = OnceLock::new();
    const LEAVES: abi::XpEpubLeaves = abi::XpEpubLeaves {
        log_enabled: quiet,
        fonts_release_caches: empty,
        ..test_leaves::leaves()
    };
    static LEGACY: wire::Legacy = wire::Legacy {
        create: no_create,
        destroy,
        enter: native_event,
        r#loop: native_event,
        exit: native_event,
        render: no_render,
        facts: no_facts,
        with_result: result,
        deliver_result: delivered,
        prepare_sleep: native_event,
        home_gesture: no_action,
        forced_refresh: no_action,
        reader_turn: no_turn,
        screenshot_info: no_screenshot,
    };
    let runtime = RUNTIME.get_or_init(|| wire::Runtime {
        render_lock,
        render_unlock,
        input_mapped_snapshot: snapshot,
        model_lock: lock,
        model_unlock: lock,
        ble_before_screen_change: allowed,
        now_ms: now,
        diagnostic,
        header_back_clear: empty,
        ..test_resources::runtime()
    });
    let leaves = &LEAVES;
    app::App {
        clippings: app::clippings_facade::ClippingService::new(ModelCell::new(
            app::clippings_service::Phase::Idle,
            runtime,
        )),
        clipping_loaded_observer: Cell::new(None),
        clipping_unloaded_observer: Cell::new(None),
        navigation: Application::new(),
        input: RefCell::new(Default::default()),
        driver: Cell::new(Default::default()),
        signals: ModelCell::new(
            app::Signals {
                target: None,
                visit: 0,
                painted_visit: 0,
                requested: false,
            },
            runtime,
        ),
        widths: UnsafeCell::new(Default::default()),
        legacy: &LEGACY,
        epub: leaves,
        legacy_entry_fixture: Cell::new(None),
        child_drawing: Some(&leaves.child_drawing),
        resources: RESOURCES.get_or_init(test_resources::resources),
        runtime,
    }
}

#[test]
fn definition_owned_result_actual_app_destroys_and_publishes_before_transfer() {
    for cancelled in [0, 1, 2] {
        let app = app_fixture();
        let parent: StableOwner<app::epub::EpubSession> =
            app::epub::EpubSession::try_new(b"", false, 0, app.epub, app.runtime, app.resources)
                .unwrap()
                .into();
        let root = app::Owner::Legacy {
            object: NonNull::dangling(),
            services: app.legacy,
            reader: true,
        };
        app.navigation.replace(root, &app);
        let mut ops = test_leaves::selection();
        ops.lookup_init = native_init;
        ops.lookup_drop = native_drop;
        ops.lookup_open = opened;
        ops.lookup_index = indexed;
        ops.lookup_find = found;
        ops.wait_paint = wait_paint;
        ops.lookup_discard = discard;
        ops.definition.payload_lookup = definition_lookup;
        ops.definition.payload_drop = drop_definition;
        ops.definition.payload_prepare = prepared;
        ops.definition.layout = layout;
        ops.definition.selection_release = release_pages;
        ops.definition.heap = default_heap;
        let owner = Lookup::try_new(
            app.runtime,
            page(),
            [0; 2],
            1,
            &ops,
            wire::Text::from_bytes(b"word"),
        )
        .unwrap();
        PAGE_DROPS.with(|v| v.set(0));
        NATIVE_DROPS.with(|v| v.set(0));
        let lookup = children::ReaderChild::Lookup(owner);
        let target = lookup.target();
        app.navigation
            .queue(Transition::Push(app::Owner::ReaderChild {
                child: lookup,
                parent: parent.pointer(),
            }));
        app.navigation.drain(&app);
        let children::ChildTarget::Lookup(pointer) = target else {
            panic!()
        };
        app.activity_loop();
        let lookup = unsafe { pointer.as_ref() };
        let children::lookup::Attempt::Detail(viewer) = (unsafe {
            lookup.perform(
                {
                    static OPS: std::sync::OnceLock<
                        crate::app::epub::children::selection_physical::Physical,
                    > = std::sync::OnceLock::new();
                    OPS.get_or_init(|| ops)
                },
                app.runtime,
                || 0,
                || lookup.release_page(),
            )
        }) else {
            panic!()
        };
        app.navigation
            .queue(Transition::Push(app::Owner::Definition(viewer)));
        app.navigation.drain(&app);
        let app::Target::Definition(viewer) = app.navigation.current().unwrap() else {
            panic!()
        };
        app.activity_loop();
        let loan = Box::new(viewer.as_ptr());
        CLIP_LIVE.with(|v| v.set(true));
        app.navigation.queue(Transition::Push(app::Owner::Legacy {
            object: NonNull::new(Box::into_raw(loan).cast()).unwrap(),
            services: app.legacy,
            reader: false,
        }));
        app.navigation.drain(&app);
        assert_eq!(app.navigation.parent_count(), 3);
        CALLS.with(|v| v.borrow_mut().clear());
        if cancelled == 2 {
            // Home uses this same replacement teardown, deepest child first.
            app.navigation
                .queue(Transition::Replace(app::Owner::Legacy {
                    object: NonNull::dangling(),
                    services: app.legacy,
                    reader: false,
                }));
            app.navigation.drain(&app);
            assert_eq!(app.navigation.parent_count(), 0);
            assert_eq!(PAGE_DROPS.with(Cell::get), 1);
            assert_eq!(NATIVE_DROPS.with(Cell::get), 1);
            CALLS.with(|v| assert_eq!(&*v.borrow(), &["clip-drop", "definition-drop", "unlock"]));
            continue;
        }
        CANCELLED.with(|v| v.set(cancelled));
        app.navigation.queue(Transition::Pop);
        app.navigation.drain(&app);
        CALLS.with(|v| {
            let mut expected = vec![
                "native-take",
                "clip-drop",
                "unlock",
                "selection-pages-release",
            ];
            expected.push("native-result-drop");
            assert_eq!(*v.borrow(), expected);
        });
        assert_eq!(app.navigation.parent_count(), 2);
        CANCELLED.with(|v| v.set(cancelled));
        unsafe { *viewer.as_ref().result.get() = Some(cancelled != 0) };
        // The small result is copied before the continuation drops the Rust owner.
        app.with_result(app::Target::Definition(viewer),|result| {
            let app::ActivityResult::Child(slot) = result else { panic!("wrong result route") };
            assert!(matches!(unsafe { slot.take() }, Some(children::ReaderChildResult::DefinitionClosed{cancelled:value}) if value==(cancelled!=0)));
        });
        unsafe { *viewer.as_ref().result.get() = Some(cancelled != 0) };
        app.navigation.queue(Transition::Pop);
        app.navigation.drain(&app);
        assert_eq!(app.navigation.parent_count(), 0);
        assert!(matches!(
            app.navigation.current(),
            Some(app::Target::Legacy(_))
        ));
        assert_eq!(PAGE_DROPS.with(Cell::get), 1);
        assert_eq!(NATIVE_DROPS.with(Cell::get), 1);
        CALLS.with(|v| {
            let events = v.borrow();
            let drop = events.iter().position(|v| *v == "definition-drop").unwrap();
            assert_eq!(events[drop + 1], "unlock");
        });
    }
}

#[test]
fn failed_definition_stack_growth_returns_to_timed_low_memory() {
    let app = app_fixture();
    let parent: StableOwner<app::epub::EpubSession> =
        app::epub::EpubSession::try_new(b"", false, 0, app.epub, app.runtime, app.resources)
            .unwrap()
            .into();
    let mut ops = test_leaves::selection();
    ops.lookup_init = native_init;
    ops.lookup_drop = native_drop;
    let owner = Lookup::try_new(
        app.runtime,
        page(),
        [0; 2],
        1,
        &ops,
        wire::Text::from_bytes(b"word"),
    )
    .unwrap();
    owner.model.with(|m| {
        m.state.pending = 0;
        m.awaiting_detail = true;
    });
    unsafe { owner.release_page() };
    let child = children::ReaderChild::Lookup(owner);
    let target = child.target();
    app.navigation.replace(
        app::Owner::ReaderChild {
            child,
            parent: parent.pointer(),
        },
        &app,
    );
    app.stack_allocation_failed();
    let children::ChildTarget::Lookup(pointer) = target else {
        panic!()
    };
    unsafe { pointer.as_ref() }.model.with(|m| {
        assert!(!m.awaiting_detail);
        assert_eq!(m.state.message, xp_ui::dictionary::popup::LOW_MEMORY);
        assert!(!m.state.expired(1599));
        assert!(m.state.expired(1600));
    });
}

unsafe extern "C" fn prepared(
    _: *mut children::definition_physical::PayloadStorage,
    _: u8,
    _: u8,
    _: u8,
    _: xp_ui::scene::Rect,
    out: *mut children::definition_physical::PayloadView,
) -> u8 {
    unsafe {
        out.write(children::definition_physical::PayloadView {
            pages: 1,
            ..Default::default()
        })
    };
    1
}

#[test]
fn bookmark_result_slot_moves_once_after_producer_drop() {
    use crate::app::resources::Text;
    use children::bookmark_owned::{Bookmarks, Position};
    for cancelled in [false, true] {
        for consume_result in [false, true] {
            let app = app_fixture();
            let owner = StableOwner::from(Bookmarks::try_new(app.runtime).unwrap());
            let xpath = Text::copy(b"/body/section/paragraph").unwrap();
            let original = xpath.bytes().as_ptr();
            let result = (!cancelled).then_some(Position {
                xpath,
                percentage: 0.375,
                spine: 4,
                page: 3,
                total_pages: 10,
                visible_offset: Some(321),
            });
            unsafe { owner.finish(result) };
            let target = app::Target::ReaderChild {
                child: children::ChildTarget::Bookmarks(owner.pointer()),
                parent: NonNull::dangling(),
            };
            let mut moved = None;
            app.with_result(target, |result| {
                drop(owner);
                let app::ActivityResult::Child(slot) = result else {
                    panic!("wrong result route")
                };
                if consume_result {
                    let Some(children::ReaderChildResult::Bookmarks(position)) =
                        (unsafe { slot.take() })
                    else {
                        panic!("wrong child result")
                    };
                    moved = position;
                }
            });
            if let Some(position) = moved {
                assert_eq!(position.xpath.bytes().as_ptr(), original);
                assert_eq!(position.xpath.bytes(), b"/body/section/paragraph");
                assert_eq!(
                    (position.spine, position.page, position.total_pages),
                    (4, 3, 10)
                );
                assert_eq!(position.percentage, 0.375);
                assert_eq!(position.visible_offset, Some(321));
            } else {
                assert!(cancelled || !consume_result);
            }
        }
    }
}

#[test]
fn reader_result_slot_moves_once_after_producer_drop() {
    use children::selection_owned::Footnote;
    for cancelled in [false, true] {
        for consume_result in [false, true] {
            let app = app_fixture();
            let owner = Footnote::try_new(app.runtime, footnote_page(), [0; 2]).unwrap();
            let href = owner.selection(0).unwrap();
            let original = href.bytes().as_ptr();
            unsafe {
                *owner.result.get() = Some(children::ReaderChildResult::Footnote(
                    (!cancelled).then_some(href),
                ))
            };
            let target = app::Target::ReaderChild {
                child: children::ChildTarget::Footnote(owner.pointer()),
                parent: NonNull::dangling(),
            };
            let mut moved = None;
            app.with_result(target, |result| {
                drop(owner);
                let app::ActivityResult::Child(slot) = result else {
                    panic!("wrong result route")
                };
                if consume_result {
                    let Some(children::ReaderChildResult::Footnote(href)) =
                        (unsafe { slot.take() })
                    else {
                        panic!("wrong child result")
                    };
                    moved = href;
                }
            });
            if let Some(href) = moved {
                assert_eq!(href.bytes().as_ptr(), original);
                assert_eq!(href.bytes(), b"#a");
            } else {
                assert!(cancelled || !consume_result);
            }
        }
    }
}

unsafe extern "C" fn layout(out: *mut xp_ui::dictionary::definition::Layout, x: i32, y: i32) -> u8 {
    assert_eq!((x, y), (-1, -1));
    unsafe { out.write(Default::default()) };
    0
}
unsafe extern "C" fn release_pages(_: *mut children::definition_physical::PayloadStorage) {
    assert!(!LOCKED.with(Cell::get) && !CLIP_LIVE.with(Cell::get));
    call("selection-pages-release");
}
unsafe extern "C" fn default_heap(out: *mut abi::XpEpubHeap) {
    unsafe {
        out.write(abi::XpEpubHeap {
            free_bytes: 100000,
            largest_block: 50000,
        })
    };
}
