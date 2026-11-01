use super::app_abi;
use core::ffi::c_void;

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
pub(super) mod read_folder;

#[derive(Clone, Copy)]
pub(super) struct EstimateObserver {
    pub context: *mut c_void,
    pub read: unsafe extern "C" fn(*mut c_void, u16),
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
    pub estimate_read: unsafe extern "C" fn(*mut c_void, u16),
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
