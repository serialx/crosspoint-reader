extern crate alloc;
use super::{
    super::{
        abi, app_abi,
        state::{Notice, Overlay, Position, TimedNotice},
        EpubSession,
    },
    BaseHooks, ChildHooks, LifetimeOverrides, MenuHooks, NavigationHooks, OverlayInputHooks,
    ProjectionHooks,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app::epub) struct LoopHooks {
    pub context: *mut c_void,
    pub idle: unsafe extern "C" fn(*mut c_void) -> u8,
    pub remember: unsafe extern "C" fn(*mut c_void),
    pub clear_end: unsafe extern "C" fn(*mut c_void),
    pub touch_menu: unsafe extern "C" fn(*mut c_void) -> u8,
    pub overlay_input: unsafe extern "C" fn(*mut c_void),
    pub restore: unsafe extern "C" fn(*mut c_void),
    pub background: unsafe extern "C" fn(*mut c_void),
    pub end_menu: unsafe extern "C" fn(*mut c_void) -> u8,
    pub end_active: unsafe extern "C" fn(*mut c_void) -> u8,
    pub threshold: unsafe extern "C" fn(*mut c_void) -> u32,
    pub end_turn: unsafe extern "C" fn(*mut c_void, u8, u8) -> u8,
    pub sync: unsafe extern "C" fn(*mut c_void) -> u8,
    pub link_repaint: unsafe extern "C" fn(*mut c_void, u8) -> u8,
    pub link_hit: unsafe extern "C" fn(*mut c_void, i32, i32, i32, i32) -> i32,
    pub framebuffer: unsafe extern "C" fn(*mut c_void, u8),
    pub font_cache: unsafe extern "C" fn(*mut c_void, u8),
    pub read_folder: unsafe extern "C" fn(*mut c_void, u8),
    pub orientation: unsafe extern "C" fn(*mut c_void, u8),
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app::epub) struct SeedState {
    pub spine: i32,
    pub page: i32,
    pub idle_spine: i32,
    pub idle_page: i32,
    pub last_render: u32,
    pub last_turn: u32,
    pub duration: u32,
    pub bookmark_time: u32,
    pub dictionary_time: u32,
    pub font_time: u32,
    pub clipping_time: u32,
    pub link_left: i32,
    pub link_top: i32,
    pub width: u16,
    pub height: u16,
    pub orientation: u8,
    pub radio_released: u8,
    pub partial_failed: u8,
    pub recents_removed: u8,
    pub read_move: u8,
    pub overlay: u8,
    pub automatic: u8,
    pub manual_turn: i8,
    pub manual_touch: u8,
    pub bookmark: u8,
    pub dictionary: u8,
    pub font: u8,
    pub clipping: u8,
    pub history_depth: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app::epub) struct Seed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub popup: *mut c_void,
    pub state: SeedState,
    pub hooks: LoopHooks,
    pub finish: unsafe extern "C" fn(*mut c_void),
    pub update: unsafe extern "C" fn(*mut c_void),
    pub buttons: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub touch: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub back: unsafe extern "C" fn(*mut c_void) -> u8,
    pub turn: unsafe extern "C" fn(*mut c_void, u8) -> u8,
    pub skip: unsafe extern "C" fn(*mut c_void, i32) -> u8,
    pub note: unsafe extern "C" fn(*mut c_void, u8, u8),
    pub toolbar: unsafe extern "C" fn(*mut c_void) -> u8,
    pub bookmark: unsafe extern "C" fn(*mut c_void),
    pub dictionary: unsafe extern "C" fn(*mut c_void),
    pub footnotes: unsafe extern "C" fn(*mut c_void, u8),
    pub selection: unsafe extern "C" fn(*mut c_void, i32, i32, u8),
    pub menu: unsafe extern "C" fn(*mut c_void),
    pub navigate: unsafe extern "C" fn(*mut c_void, app_abi::Text, u8),
    pub overlay: unsafe extern "C" fn(*mut c_void, u8),
    pub discard: unsafe extern "C" fn(*mut c_void),
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app::epub) struct Observation {
    pub radio_released: i32,
    pub idle_spine: i32,
    pub idle_page: i32,
    pub partial_failed: i32,
    pub recents_removed: i32,
    pub read_move: i32,
    pub overlay: i32,
    pub last_turn: u32,
    pub automatic: i32,
    pub manual_turn: i32,
    pub manual_touch: i32,
    pub bookmark: i32,
    pub dictionary: i32,
    pub font: i32,
    pub clipping: i32,
    pub orientation: i32,
    pub spine: i32,
}
unsafe extern "C" fn unexpected0<R>(_: *mut c_void) -> R {
    panic!("unexpected loop composition")
}
unsafe extern "C" fn unexpected1<A, R>(_: *mut c_void, _: A) -> R {
    panic!("unexpected loop composition")
}
unsafe extern "C" fn unexpected2<A, B, R>(_: *mut c_void, _: A, _: B) -> R {
    panic!("unexpected loop composition")
}
unsafe extern "C" fn unexpected3<A, B, C, R>(_: *mut c_void, _: A, _: B, _: C) -> R {
    panic!("unexpected loop composition")
}
unsafe extern "C" fn unexpected4<A, B, C, D, R>(_: *mut c_void, _: A, _: B, _: C, _: D) -> R {
    panic!("unexpected loop composition")
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_loop_create(
    seed: *const Seed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    let s = &seed.state;
    let state = owner.state.get_mut();
    state.ready = true;
    state.current = Position {
        spine: s.spine,
        page: s.page,
    };
    state.idle_prewarm = Position {
        spine: s.idle_spine,
        page: s.idle_page,
    };
    state.last_render_ms = s.last_render;
    state.last_turn_ms = s.last_turn;
    state.turn_duration_ms = s.duration;
    state.viewport = (s.width, s.height);
    state.orientation = s.orientation;
    state.facts.radio_released = s.radio_released != 0;
    state.partial_start_failed = s.partial_failed != 0;
    state.recents_removed = s.recents_removed != 0;
    state.move_to_read = s.read_move != 0;
    state.overlay = match s.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("fixture overlay"),
    };
    state.auto_turn = s.automatic != 0;
    state.manual_turn = s.manual_turn;
    state.manual_touch = s.manual_touch != 0;
    let notice = |shown, since, message| (shown != 0).then_some(TimedNotice { message, since });
    state.bookmark_notice = notice(s.bookmark, s.bookmark_time, Notice::BookmarkAdded);
    state.dictionary_notice = notice(s.dictionary, s.dictionary_time, Notice::Dictionary);
    state.font_notice = notice(s.font, s.font_time, Notice::FontDamaged);
    state.clipping_notice = notice(s.clipping, s.clipping_time, Notice::ClipSaved);
    state.history.depth = s.history_depth;
    state.link_margins = (s.link_left, s.link_top);
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.book = NonNull::new(seed.book);
    guarded.section = NonNull::new(seed.section);
    guarded.failure_popup = NonNull::new(seed.popup);
    owner.loop_hooks = Some(seed.hooks);
    let context = seed.hooks.context;
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
        back: seed.back,
        format: unexpected0,
        touch: seed.touch,
        buttons: seed.buttons,
        turn: seed.turn,
        skip: seed.skip,
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
    owner.navigation_hooks = Some(NavigationHooks {
        context,
        finish: seed.finish,
        push: unexpected1,
        reopen: unexpected1,
        update: seed.update,
    });
    owner.projection_hooks = Some(ProjectionHooks {
        context,
        toolbar_menu: seed.toolbar,
        base_input: unexpected0,
        acknowledge: unexpected0,
        clear: unexpected0,
        fallback: unexpected0,
    });
    owner.menu_hooks = Some(MenuHooks {
        context,
        chapter: unexpected1,
        percent: unexpected2,
        orientation: unexpected1,
        automatic: unexpected1,
        progress: unexpected1,
        clear: unexpected0,
        footnotes: seed.footnotes,
        jump: unexpected1,
        dictionary: seed.dictionary,
        home: unexpected0,
        save: unexpected3,
        sync: unexpected0,
        selection: seed.selection,
        clippings: unexpected0,
        bookmark: seed.bookmark,
        bluetooth: unexpected0,
        build_items: unexpected4,
        remember: unexpected0,
        popup: unexpected0,
    });
    owner.child_hooks = Some(ChildHooks {
        context,
        bookmark: unexpected0,
        menu: seed.menu,
        progress: unexpected1,
        flush: unexpected0,
        save_progress: unexpected3,
        navigate: seed.navigate,
        hit: unexpected3,
        replace: unexpected1,
    });
    owner.overlay_input_hooks = Some(OverlayInputHooks {
        context,
        open: seed.overlay,
        close: unexpected0,
        clear: unexpected0,
        text: unexpected0,
        live: unexpected0,
        text_popup: unexpected1,
        more: unexpected1,
        popup: unexpected0,
        width: unexpected1,
        height: unexpected1,
    });
    owner.lifetime_overrides = Some(LifetimeOverrides {
        context,
        settle_overlay: unexpected0,
        discard_overlay: seed.discard,
        save_links: unexpected0,
        load_links: unexpected0,
        load_bookmarks: unexpected0,
        disable_fast: unexpected0,
        destination: unexpected3,
        move_book: unexpected3,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    Box::into_raw(owner).cast()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_loop(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).loop_reader();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_loop_observe(raw: *mut c_void, out: *mut Observation) {
    *out = (&*raw.cast::<EpubSession>()).model(|s| Observation {
        radio_released: i32::from(s.facts.radio_released),
        idle_spine: s.idle_prewarm.spine,
        idle_page: s.idle_prewarm.page,
        partial_failed: i32::from(s.partial_start_failed),
        recents_removed: i32::from(s.recents_removed),
        read_move: i32::from(s.move_to_read),
        overlay: s.overlay as i32,
        last_turn: s.last_turn_ms,
        automatic: i32::from(s.auto_turn),
        manual_turn: i32::from(s.manual_turn),
        manual_touch: i32::from(s.manual_touch),
        bookmark: i32::from(s.bookmark_notice.is_some()),
        dictionary: i32::from(s.dictionary_notice.is_some()),
        font: i32::from(s.font_notice.is_some()),
        clipping: i32::from(s.clipping_notice.is_some()),
        orientation: i32::from(s.orientation),
        spine: s.current.spine,
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_loop_destroy(raw: *mut c_void) {
    let mut owner = Box::from_raw(raw.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    let guarded = owner.guarded.get_mut();
    guarded.native = None;
    guarded.book = None;
    guarded.failure_popup = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_loop_remote(raw: *mut c_void, reader_menu: u8) -> u8 {
    u8::from((&*raw.cast::<EpubSession>()).dispatch_remote_shortcut(reader_menu != 0))
}
