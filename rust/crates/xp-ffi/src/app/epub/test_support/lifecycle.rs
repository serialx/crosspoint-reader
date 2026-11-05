extern crate alloc;
use super::{
    super::{
        abi, app_abi,
        state::{Overlay, Position},
        EpubSession,
    },
    BaseHooks, BuildingObserver, DeferredOverride, EstimateObserver, LoopHooks, NavigationHooks,
    ProjectionHooks,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Snapshot {
    pub spine: i32,
    pub page: i32,
    pub next_page: i32,
    pub cached_spine: i32,
    pub cached_count: i32,
    pub cached_offset: u32,
    pub last_turn: u32,
    pub failed_spine: i32,
    pub history: [i32; 6],
    pub pending_page: u16,
    pub has_pending_page: u8,
    pub has_cached_offset: u8,
    pub history_depth: u8,
    pub heap_paused: u8,
    pub overlay: u8,
    pub stale: u8,
    pub manual_turn: i8,
    pub manual_touch: u8,
    pub orientation: u8,
    pub has_section: u8,
    pub radio_released: u8,
}

#[repr(C)]
pub struct Seed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub state: Snapshot,
    pub context: *mut c_void,
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub acknowledge: unsafe extern "C" fn(*mut c_void),
    pub fallback: unsafe extern "C" fn(*mut c_void),
    pub estimate: unsafe extern "C" fn(*mut c_void, i32),
    pub building: unsafe extern "C" fn(*mut c_void, u8),
    pub reposition: unsafe extern "C" fn(*mut c_void) -> u8,
    pub update: unsafe extern "C" fn(*mut c_void),
    pub buttons: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub note: unsafe extern "C" fn(*mut c_void, u8, u8),
    pub end_turn: unsafe extern "C" fn(*mut c_void, u8, u8) -> u8,
    pub orientation: unsafe extern "C" fn(*mut c_void, u8),
    pub input_composition: u8,
}
unsafe extern "C" fn unexpected0<R>(_: *mut c_void) -> R {
    panic!("unexpected lifecycle composition")
}
unsafe extern "C" fn unexpected1<A, R>(_: *mut c_void, _: A) -> R {
    panic!("unexpected lifecycle composition")
}
unsafe extern "C" fn unexpected4<A, B, C, D, R>(_: *mut c_void, _: A, _: B, _: C, _: D) -> R {
    panic!("unexpected lifecycle composition")
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_create_legacy_book(
    seed: *const Seed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    let value = seed.state;
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    owner.legacy_pages.set(Some(&legacy_book.section.page));
    let state = owner.state.get_mut();
    state.ready = true;
    state.facts.radio_released = value.radio_released != 0;
    state.facts.book = !seed.book.is_null();
    state.current = Position {
        spine: value.spine,
        page: value.page,
    };
    state.next_page = value.next_page;
    state.cached.spine = value.cached_spine;
    state.cached.pages = value.cached_count;
    state.cached.offset = (value.has_cached_offset != 0).then_some(value.cached_offset);
    state.deferred.page = (value.has_pending_page != 0).then_some(value.pending_page);
    state.last_turn_ms = value.last_turn;
    state.failed_build_spine = value.failed_spine;
    state.heap_paused = value.heap_paused != 0;
    state.overlay = match value.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        _ => panic!("fixture overlay"),
    };
    state.facts.frame_stale = value.stale != 0;
    state.manual_turn = value.manual_turn;
    state.manual_touch = value.manual_touch != 0;
    state.orientation = value.orientation;
    assert!(value.history_depth <= 3);
    state.history.depth = value.history_depth;
    for (slot, pair) in state
        .history
        .positions
        .iter_mut()
        .zip(value.history.as_chunks::<2>().0)
    {
        *slot = Position {
            spine: pair[0],
            page: pair[1],
        };
    }
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture root"));
    guarded.section =
        super::legacy_section::SectionSlot::legacy(seed.section, &legacy_book.section);
    let context = seed.context;
    owner.projection_hooks = Some(ProjectionHooks {
        context,
        toolbar_menu: unexpected0,
        base_input: unexpected0,
        acknowledge: seed.acknowledge,
        clear: seed.clear,
        fallback: seed.fallback,
    });
    owner.estimate_observer = Some(EstimateObserver {
        context,
        read: seed.estimate,
    });
    owner.building_observer = Some(BuildingObserver {
        context,
        read: seed.building,
    });
    owner.deferred_override = Some(DeferredOverride {
        context,
        apply: seed.reposition,
    });
    owner.navigation_hooks = Some(NavigationHooks {
        context,
        finish: unexpected0,
        push: unexpected1,
        reopen: unexpected1,
        update: seed.update,
    });
    if seed.input_composition != 0 {
        owner.base_hooks = Some(BaseHooks {
            context,
            enter: unexpected0,
            exit: unexpected0,
            orientation: unexpected0,
            load: unexpected0,
            failure: unexpected0,
            at_end: unexpected0,
            return_end: unexpected0,
            end_rendered: unexpected0,
            back: unexpected0,
            format: unexpected0,
            touch: unexpected0,
            buttons: seed.buttons,
            turn: None,
            skip: None,
            snapshot: unexpected0,
            worthy: unexpected0,
            reset: unexpected0,
            complete: unexpected0,
            mark: unexpected0,
            note: seed.note,
            force: unexpected0,
            percent: unexpected0,
            home: unexpected0,
            navigate: unexpected1,
        });
        owner.loop_hooks = Some(LoopHooks {
            context,
            idle: unexpected0,
            remember: unexpected0,
            clear_end: unexpected0,
            touch_menu: unexpected0,
            overlay_input: unexpected0,
            restore: unexpected0,
            background: None,
            end_menu: unexpected0,
            end_active: unexpected0,
            threshold: unexpected0,
            end_turn: seed.end_turn,
            sync: unexpected0,
            link_repaint: unexpected1,
            link_hit: unexpected4,
            framebuffer: unexpected1,
            font_cache: unexpected1,
            read_folder: unexpected1,
            orientation: seed.orientation,
        });
    }
    let access = owner.borrowed_resources();
    owner.publish_section(&access);
    drop(access);
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    let access = owner.borrowed_resources();
    owner.drop_section(&access);
    drop(access);
    owner.discard_legacy_fixture();
    owner.guarded.get_mut().native = None;
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_observe(raw: *mut c_void, out: *mut Snapshot) {
    let owner = &*raw.cast::<EpubSession>();
    *out = owner.model(|state| {
        let mut history = [0; 6];
        for (pair, position) in history
            .as_chunks_mut::<2>()
            .0
            .iter_mut()
            .zip(state.history.positions)
        {
            pair[0] = position.spine;
            pair[1] = position.page;
        }
        Snapshot {
            spine: state.current.spine,
            page: state.current.page,
            next_page: state.next_page,
            cached_spine: state.cached.spine,
            cached_count: state.cached.pages,
            cached_offset: state.cached.offset.unwrap_or(0),
            last_turn: state.last_turn_ms,
            failed_spine: state.failed_build_spine,
            history,
            pending_page: state.deferred.page.unwrap_or(0),
            has_pending_page: u8::from(state.deferred.page.is_some()),
            has_cached_offset: u8::from(state.cached.offset.is_some()),
            history_depth: state.history.depth,
            heap_paused: u8::from(state.heap_paused),
            overlay: state.overlay as u8,
            stale: u8::from(state.facts.frame_stale),
            manual_turn: state.manual_turn,
            manual_touch: u8::from(state.manual_touch),
            orientation: state.orientation,
            has_section: u8::from(state.facts.section.is_some()),
            radio_released: u8::from(state.facts.radio_released),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_clear_pending_page(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).model(|state| state.deferred.page = None);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_progress(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    drop(owner.load_progress(&access));
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_save_links(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.save_links(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_load_links(raw: *mut c_void) {
    let owner = &*raw.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.load_links(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_lifecycle_input(
    raw: *mut c_void,
    touch: xp_ui::reader::input::Turn,
) {
    (&*raw.cast::<EpubSession>()).manual_turn_input(touch);
}
