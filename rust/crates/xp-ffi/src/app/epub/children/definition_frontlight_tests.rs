use super::*;
use crate::app::epub::{
    children::{lookup::Attempt, selection_owned::Lookup, ReaderChild},
    page_owner::Page,
};
use crate::app::{self, stable_owner::StableOwner, sync::ModelCell};
use core::{cell::UnsafeCell, ptr::NonNull};
use xp_ui::app::{Application, Transition};

const RESULT_NONE: u8 = 0;
const RESULT_INTERVAL: u8 = 6;

#[derive(Clone, Copy)]
enum Entry {
    Details,
    PluginCatalog,
    EpubLookup,
    PlainClipping,
}
thread_local! {
    static LIGHT_GESTURE: Cell<bool> = const { Cell::new(false) };
    static RETURN_KIND: Cell<u8> = const { Cell::new(RESULT_NONE) };
    static RETURN_CANCELLED: Cell<u8> = const { Cell::new(0) };
}
struct Native {
    panel: bool,
}
unsafe extern "C" fn create(launch: *const abi::Launch) -> *mut c_void {
    assert_eq!(unsafe { (*launch).kind }, abi::FRONTLIGHT_PANEL);
    event("panel-create");
    Box::into_raw(Box::new(Native { panel: true })).cast()
}
unsafe extern "C" fn drop_native(p: *mut c_void) {
    let native = unsafe { Box::from_raw(p.cast::<Native>()) };
    if native.panel {
        assert!(LOCKED.with(Cell::get));
        event("panel-drop");
    }
}
unsafe extern "C" fn native_event(_: *mut c_void) {}
unsafe extern "C" fn facts(p: *const c_void, _: u8, _: u8) -> abi::LegacyFacts {
    abi::LegacyFacts {
        kind: if unsafe { (*p.cast::<Native>()).panel } {
            abi::FRONTLIGHT_PANEL
        } else {
            u8::MAX
        },
        ..unsafe { core::mem::zeroed() }
    }
}
unsafe extern "C" fn result(_: *mut c_void, sink: abi::ResultSink, context: *mut c_void) {
    let view = abi::XpAppResultView {
        original: ptr::null(),
        kind: RETURN_KIND.with(Cell::get),
        cancelled: RETURN_CANCELLED.with(Cell::get),
        value: abi::XpAppResultPayload { unsigned_value: 0 },
    };
    unsafe { sink(context, &view) };
    event("panel-result-return");
}
unsafe extern "C" fn unused_render(_: *mut c_void, _: *mut abi::RenderToken) {
    panic!("native render");
}
unsafe extern "C" fn unused_delivery(_: *mut c_void, _: *const c_void) {
    panic!("native delivery");
}
unsafe extern "C" fn no_action(_: *mut c_void) -> u8 {
    0
}
unsafe extern "C" fn no_turn(_: *mut c_void, _: u8, _: u8) -> u8 {
    0
}
unsafe extern "C" fn no_shot(_: *const c_void, _: abi::ScreenshotSink, _: *mut c_void) {
    panic!("screenshot");
}
unsafe extern "C" fn present() -> u8 {
    1
}
unsafe extern "C" fn allowed(_: u8) -> u8 {
    1
}
unsafe extern "C" fn feedback(_: u8) {
    event("feedback");
}
unsafe extern "C" fn mapped(_: u8, _: u8, _: u8, out: *mut abi::MappedSnapshot) {
    unsafe {
        out.write(core::mem::zeroed());
        (*out).light_panel_gesture = u8::from(LIGHT_GESTURE.with(|v| v.replace(false)));
    }
}
unsafe extern "C" fn test_settings(resolve: u8, out: *mut crate::app::epub::abi::XpEpubSettings) {
    if resolve != 0 {
        unsafe { settings(resolve, out) };
    } else {
        unsafe {
            out.write(core::mem::zeroed());
            (*out).touch_controls = 1;
        }
    }
}
unsafe extern "C" fn no_long(kind: u8, _: *mut i32, _: *mut i32) -> u8 {
    assert_eq!(kind, 0);
    0
}
unsafe extern "C" fn no_tap(_: *mut i32, _: *mut i32) -> u8 {
    0
}
unsafe extern "C" fn native_payload(
    p: *mut PayloadStorage,
    transfer: *mut NativeTransfer,
    html: *mut u8,
) -> u8 {
    match unsafe { *transfer.cast::<Entry>() } {
        Entry::Details => event("details-copy"),
        Entry::PluginCatalog => event("plugin-move"),
        _ => panic!("native entry"),
    }
    unsafe {
        html.write(0);
        plain(p, abi::Text::from_bytes(b"title"), 0, fill, ptr::null_mut())
    }
}
unsafe extern "C" fn lookup_init(_: *mut LookupStorage, _: abi::Text) {}
unsafe extern "C" fn lookup_drop(_: *mut LookupStorage) {}
unsafe extern "C" fn lookup_ok(_: *mut LookupStorage, out: *mut u8) -> u8 {
    unsafe { out.write(0) };
    1
}
unsafe extern "C" fn page_allocate(_: u8, size: usize) -> *mut u8 {
    let p = unsafe { alloc(Layout::from_size_align(size + 8, 8).unwrap()) };
    assert!(!p.is_null());
    unsafe {
        p.cast::<usize>().write(size);
        p.add(8)
    }
}
unsafe extern "C" fn page_release(_: u8, p: *mut u8) {
    let p = unsafe { p.sub(8) };
    let size = unsafe { p.cast::<usize>().read() };
    unsafe { dealloc(p, Layout::from_size_align(size + 8, 8).unwrap()) };
}
fn fixture() -> app::App {
    static RUNTIME: OnceLock<abi::Runtime> = OnceLock::new();
    static RESOURCES: OnceLock<abi::Resources> = OnceLock::new();
    static LEGACY: abi::Legacy = abi::Legacy {
        create,
        destroy: drop_native,
        enter: native_event,
        r#loop: native_event,
        exit: native_event,
        render: unused_render,
        facts,
        with_result: result,
        deliver_result: unused_delivery,
        prepare_sleep: native_event,
        home_gesture: no_action,
        forced_refresh: no_action,
        reader_turn: no_turn,
        screenshot_info: no_shot,
    };
    let runtime = RUNTIME.get_or_init(|| abi::Runtime {
        frontlight_present: present,
        touch_feedback: feedback,
        input_mapped_snapshot: mapped,
        ble_before_screen_change: allowed,
        header_back_clear: empty,
        ..*runtime()
    });
    const LEAVES: XpEpubLeaves = {
        let mut l = test_leaves::leaves();
        l.heap_snapshot = internal;
        l.log = log;
        l.log_enabled = enabled;
        l.fonts_release_caches = fonts;
        l.input_snapshot = snapshot;
        l.input_screen_point = no_long;
        l.input_screen_tap = no_tap;
        l.settings_snapshot = test_settings;
        l.fonts_prewarm_begin = begin;
        l.fonts_prewarm_finish_scan = finish;
        l.fonts_prewarm_drop = drop_scan;
        l.child_selection_physical.dictionary_popup_layout = popup;
        let ops = &mut l.child_selection_physical;
        ops.lookup_init = lookup_init;
        ops.lookup_drop = lookup_drop;
        ops.lookup_open = lookup_ok;
        ops.lookup_find = lookup_ok;
        ops.lookup_discard = lookup_drop;
        ops.wait_paint = empty;
        let p = &mut ops.definition;
        *p = physical();
        p.payload_native = native_payload;
        p.draw_header = header;
        p.draw_body = body;
        l
    };
    let leaves = &LEAVES;
    let resources = RESOURCES.get_or_init(|| abi::Resources {
        paint_scene: paint,
        refresh,
        translate,
        footer_draw: footer,
        text_width: width,
        line_height: height,
        text_draw: text,
        buffer_allocate: page_allocate,
        buffer_release: page_release,
        ..test_resources::resources()
    });
    app::App {
        clippings: app::clippings_facade::ClippingService::new(ModelCell::new(
            app::clippings_service::Phase::Idle,
            runtime,
        )),
        clipping_loaded_observer: Cell::new(None),
        clipping_unloaded_observer: Cell::new(None),
        legacy_entry_fixture: Cell::new(None),
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
        child_drawing: Some(&leaves.child_drawing),
        resources,
        runtime,
    }
}
fn exercise(entry: Entry) {
    let app = fixture();
    let epub: StableOwner<app::epub::EpubSession> =
        app::epub::EpubSession::try_new(b"", false, 0, app.epub, app.runtime, app.resources)
            .unwrap()
            .into();
    app.navigation.replace(
        app::Owner::Legacy {
            object: NonNull::new(Box::into_raw(Box::new(Native { panel: false })).cast()).unwrap(),
            services: app.legacy,
            reader: matches!(entry, Entry::EpubLookup | Entry::PlainClipping),
            section: false,
        },
        &app,
    );
    let ops = &app.epub.child_selection_physical;
    let viewer = match entry {
        Entry::EpubLookup => {
            let page = Page::from_bytes(app.resources, app.epub, &[0; 6], 0).unwrap();
            let lookup = Lookup::try_new(
                app.runtime,
                page,
                [0; 2],
                1,
                ops,
                abi::Text::from_bytes(b"word"),
            )
            .unwrap();
            let target = lookup.pointer();
            app.navigation
                .queue(Transition::Push(app::Owner::ReaderChild {
                    child: ReaderChild::Lookup(lookup),
                    parent: epub.pointer(),
                }));
            app.navigation.drain(&app);
            let lookup = unsafe { target.as_ref() };
            let Attempt::Detail(viewer) =
                (unsafe { lookup.perform(ops, app.runtime, || 10, || lookup.release_page()) })
            else {
                panic!("lookup failed")
            };
            viewer
        }
        Entry::PlainClipping => unsafe {
            DefinitionOwner::try_plain(
                app.runtime,
                &ops.definition,
                abi::Text::from_bytes(b"clip"),
                0,
                fill,
                ptr::null_mut(),
            )
        }
        .unwrap(),
        Entry::Details | Entry::PluginCatalog => {
            let mut transfer = entry;
            unsafe {
                DefinitionOwner::try_native(
                    app.runtime,
                    &ops.definition,
                    (&mut transfer as *mut Entry).cast(),
                )
            }
            .unwrap()
        }
    };
    let target = viewer.pointer();
    unsafe {
        (*viewer.navigation.get()).selection = SelectionPosition {
            start_word: 2,
            end_word: 7,
            valid: 1,
            ..Default::default()
        };
    }
    app.navigation
        .queue(Transition::Push(app::Owner::Definition(viewer)));
    app.navigation.drain(&app);
    RENDER_OWNER.with(|v| v.set(target.as_ptr()));
    let depth = app.navigation.parent_count();
    for (kind, cancelled) in [(RESULT_NONE, 0), (RESULT_NONE, 1), (RESULT_INTERVAL, 0)] {
        RETURN_KIND.with(|v| v.set(kind));
        RETURN_CANCELLED.with(|v| v.set(cancelled));
        LIGHT_GESTURE.with(|v| v.set(true));
        app.activity_loop();
        app.navigation.drain(&app);
        assert!(matches!(
            app.navigation.current(),
            Some(app::Target::Legacy(_))
        ));
        assert_eq!(app.navigation.parent_count(), depth + 1);
        events();
        app.navigation.queue(Transition::Pop);
        app.navigation.drain(&app);
        assert_eq!(events(), vec!["panel-drop", "panel-result-return"]);
        assert!(matches!(app.navigation.current(), Some(app::Target::Definition(p)) if p==target));
        assert!(app
            .signals
            .with(|s| matches!(s.target, Some(app::Target::Definition(p)) if p==target)));
        assert_eq!(app.navigation.parent_count(), depth);
        let nav = unsafe { &*target.as_ref().navigation.get() };
        assert_eq!(
            (
                nav.selection.start_word,
                nav.selection.end_word,
                nav.selection.valid
            ),
            (2, 7, 1)
        );
        assert!(nav.pending.is_empty());
        let guard = DefinitionSession::lock(app.runtime);
        assert!(app.definition_render(target));
        drop(guard);
        assert!(events().contains(&"body"));
        app.activity_loop();
        assert!(!events()
            .iter()
            .any(|s| ["copy", "selection-release", "selection", "find"].contains(s)));
    }
    RENDER_OWNER.with(|v| v.set(ptr::null()));
    drop(app);
    drop(epub);
}
#[test]
fn frontlight_return_from_details_keeps_definition() {
    exercise(Entry::Details);
}
#[test]
fn frontlight_return_from_plugin_catalog_keeps_definition() {
    exercise(Entry::PluginCatalog);
}
#[test]
fn frontlight_return_from_epub_lookup_keeps_definition() {
    exercise(Entry::EpubLookup);
}
#[test]
fn frontlight_return_from_plain_clipping_keeps_definition() {
    exercise(Entry::PlainClipping);
}
