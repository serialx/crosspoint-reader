mod base;
mod book_owner;
mod book_render;
pub(super) use book_render::BookRenderHooks;
mod child;
mod clipping;
pub(super) use child::ChildHooks;
mod menu;
pub(super) use menu::MenuHooks;
mod entry_gate;
mod idle;
mod lifecycle;
mod loop_owner;
pub(super) use loop_owner::LoopHooks;
mod input;
mod overlay_actions;
mod overlay_input;
pub(super) use overlay_input::OverlayInputHooks;
pub(super) mod overlay_view;
mod popup_concurrency;
mod render;
mod scene;
mod view_values;

use super::app_abi;
use core::ffi::c_void;

#[derive(Clone, Copy)]
pub(super) struct OverlayHooks {
    pub context: *mut c_void,
    pub home_reset: unsafe extern "C" fn(*mut c_void),
    pub current_toc: unsafe extern "C" fn(*mut c_void) -> i32,
    pub build_more: Option<unsafe extern "C" fn(*mut c_void)>,
    pub paint: unsafe extern "C" fn(*mut c_void),
    pub push: unsafe extern "C" fn(*mut c_void),
    pub a4: unsafe extern "C" fn(*mut c_void, u8),
    pub xteink: unsafe extern "C" fn(*mut c_void, u8),
}

#[derive(Clone, Copy)]
pub(super) struct OverlayViewHooks {
    pub context: *mut c_void,
    pub chapter: unsafe extern "C" fn(*mut c_void, app_abi::TextSink, *mut c_void) -> u8,
    pub text_name: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
    pub text_value: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
    pub more_name: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
    pub more_value: unsafe extern "C" fn(*mut c_void, i32, app_abi::TextSink, *mut c_void) -> u8,
}

#[derive(Clone, Copy)]
pub(super) struct RenderHooks {
    pub context: *mut c_void,
    pub status: unsafe extern "C" fn(*mut c_void),
    pub highlights: unsafe extern "C" fn(*mut c_void, *mut c_void, i32, i32, i32),
}

#[derive(Clone, Copy)]
pub(super) struct ViewValueHooks {
    pub context: *mut c_void,
    pub more_label: unsafe extern "C" fn(*mut c_void, i32) -> u16,
    pub link_note_drawn: unsafe extern "C" fn(*mut c_void, u8, u8),
}

#[derive(Clone, Copy)]
pub(super) struct ClippingHooks {
    pub context: *mut c_void,
    pub signature: unsafe extern "C" fn(*mut c_void, u16, u16) -> u32,
    pub nonempty: unsafe extern "C" fn(*mut c_void, u8),
}

#[derive(Clone, Copy)]
pub(super) struct NavigationHooks {
    pub context: *mut c_void,
    pub finish: unsafe extern "C" fn(*mut c_void),
    pub push: unsafe extern "C" fn(*mut c_void, *mut c_void),
    pub reopen: unsafe extern "C" fn(*mut c_void, app_abi::Text),
    pub update: unsafe extern "C" fn(*mut c_void),
}

#[derive(Clone, Copy)]
pub(super) struct BaseHooks {
    pub context: *mut c_void,
    pub enter: unsafe extern "C" fn(*mut c_void),
    pub exit: unsafe extern "C" fn(*mut c_void),
    pub orientation: unsafe extern "C" fn(*mut c_void),
    pub load: unsafe extern "C" fn(*mut c_void) -> u8,
    pub failure: unsafe extern "C" fn(*mut c_void) -> u8,
    pub at_end: unsafe extern "C" fn(*mut c_void) -> u8,
    pub return_end: unsafe extern "C" fn(*mut c_void),
    pub end_rendered: unsafe extern "C" fn(*mut c_void),
    pub back: unsafe extern "C" fn(*mut c_void) -> u8,
    pub format: unsafe extern "C" fn(*mut c_void) -> u8,
    pub touch: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub buttons: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub turn: Option<unsafe extern "C" fn(*mut c_void, u8) -> u8>,
    pub skip: Option<unsafe extern "C" fn(*mut c_void, i32) -> u8>,
    pub snapshot: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::Session,
    pub worthy: unsafe extern "C" fn(*mut c_void) -> u8,
    pub reset: unsafe extern "C" fn(*mut c_void),
    pub complete: unsafe extern "C" fn(*mut c_void),
    pub mark: unsafe extern "C" fn(*mut c_void),
    pub note: unsafe extern "C" fn(*mut c_void, u8, u8),
    pub force: unsafe extern "C" fn(*mut c_void),
    pub percent: unsafe extern "C" fn(*mut c_void) -> i32,
    pub home: unsafe extern "C" fn(*mut c_void),
    pub navigate: unsafe extern "C" fn(*mut c_void, app_abi::Text),
}

#[derive(Clone, Copy)]
pub(super) struct ProjectionHooks {
    pub context: *mut c_void,
    pub toolbar_menu: unsafe extern "C" fn(*mut c_void) -> u8,
    pub base_input: unsafe extern "C" fn(*mut c_void) -> u8,
    pub acknowledge: unsafe extern "C" fn(*mut c_void),
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub fallback: unsafe extern "C" fn(*mut c_void),
}

#[derive(Clone, Copy)]
pub(super) struct BookmarkOverrides {
    pub context: *mut c_void,
    pub position: unsafe extern "C" fn(*mut c_void, *mut super::abi::XpEpubPosition),
    pub sanitize: unsafe extern "C" fn(*mut c_void, app_abi::Text),
}

#[derive(Clone, Copy)]
pub(super) struct LifetimeOverrides {
    pub context: *mut c_void,
    pub settle_overlay: unsafe extern "C" fn(*mut c_void),
    pub discard_overlay: unsafe extern "C" fn(*mut c_void),
    pub save_links: unsafe extern "C" fn(*mut c_void),
    pub load_links: unsafe extern "C" fn(*mut c_void),
    pub load_bookmarks: unsafe extern "C" fn(*mut c_void),
    pub disable_fast: unsafe extern "C" fn(*mut c_void),
    pub destination:
        unsafe extern "C" fn(*mut c_void, app_abi::Text, app_abi::TextSink, *mut c_void) -> u8,
    pub move_book: unsafe extern "C" fn(*mut c_void, app_abi::Text, app_abi::Text, app_abi::Text),
}

pub(super) mod bookmarks;
pub(super) mod build;
pub(super) mod helpers;
pub(super) mod lifetime;
pub(super) mod link_text;
pub(super) mod navigation;
pub(super) mod position;
pub(super) mod projection;
pub(super) mod publication;
pub(super) mod read_folder;
pub(super) mod recovery;
pub(super) mod settings;

#[derive(Clone, Copy)]
pub(super) struct EstimateObserver {
    pub context: *mut c_void,
    pub read: unsafe extern "C" fn(*mut c_void, i32),
}

#[derive(Clone, Copy)]
pub(super) struct BuildingObserver {
    pub context: *mut c_void,
    pub read: unsafe extern "C" fn(*mut c_void, u8),
}

#[derive(Clone, Copy)]
pub(super) struct DeferredOverride {
    pub context: *mut c_void,
    pub apply: unsafe extern "C" fn(*mut c_void) -> u8,
}

#[repr(C)]
pub struct BuildSeed {
    pub native: *mut c_void,
    pub section: *mut c_void,
    pub spine: i32,
    pub active_page: i32,
    pub next_page: i32,
    pub failed_spine: i32,
    pub refresh: i32,
    pub radio_released: u8,
    pub heap_paused: u8,
    pub popup: u8,
    pub overlay: u8,
    pub pending_turn: i8,
    pub stale: u8,
    pub context: *mut c_void,
    pub building_read: unsafe extern "C" fn(*mut c_void, u8),
    pub reposition: unsafe extern "C" fn(*mut c_void) -> u8,
}

#[repr(C)]
pub struct BuildObservation {
    pub next_page: i32,
    pub failed_spine: i32,
    pub refresh: i32,
    pub radio_released: u8,
    pub heap_paused: u8,
    pub popup: u8,
    pub stale: u8,
}

// Host fixtures cross this boundary once, before invoking production methods.
#[repr(C)]
pub struct NavigationSeed {
    pub book: *mut c_void,
    pub section: *mut c_void,
    pub spine: i32,
    pub active_page: i32,
    pub next_page: i32,
    pub cached_spine: i32,
    pub cached_count: i32,
    pub cached_offset: u32,
    pub visible_offset: u32,
    pub history: [i32; 6],
    pub history_depth: u8,
    pub has_cached_offset: u8,
    pub has_visible_offset: u8,
    pub anchor: app_abi::Text,
    pub estimate_context: *mut c_void,
    pub estimate_read: unsafe extern "C" fn(*mut c_void, i32),
}

#[repr(C)]
pub struct NavigationObservation {
    pub spine: i32,
    pub next_page: i32,
    pub active_page: i32,
    pub cached_count: i32,
    pub cached_offset: u32,
    pub pending_offset: u32,
    pub spine_progress: f32,
    pub anchor: app_abi::Text,
    pub history: [i32; 6],
    pub history_depth: u8,
    pub has_section: u8,
    pub has_cached_offset: u8,
    pub has_pending_offset: u8,
    pub pending_percent: u8,
}
