//! Borrowed application boundary; ownership lives in xp-ui.

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text {
    pub data: *const u8,
    pub len: usize,
}

pub type TextSink = unsafe extern "C" fn(*mut core::ffi::c_void, Text) -> u8;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Book {
    pub path: Text,
    pub title: Text,
    pub author: Text,
    pub cover: Text,
}

pub type BookSink = unsafe extern "C" fn(*mut core::ffi::c_void, *const Book) -> u8;
pub type Status = u8;
pub type EventResult = u8;
pub type LaunchKind = u8;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ReaderLaunch {
    pub path: Text,
    pub fast_initial: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MessageLaunch {
    pub text: Text,
    pub style: u8,
}

#[repr(C)]
pub struct KeyboardLaunch {
    pub title: Text,
    pub initial: Text,
    pub maximum: usize,
    pub input_type: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DialogInterval {
    pub title: u16,
    pub format: u16,
    pub boundary: u16,
    pub initial: i32,
    pub minimum: i32,
    pub maximum: i32,
    pub small_step: i32,
    pub large_step: i32,
    pub edge_buttons: u8,
    pub reader: u8,
}
#[repr(C)]
pub struct DialogLaunch {
    pub kind: u8,
    pub style: u8,
    pub refresh: u8,
    pub message_font: i32,
    pub name: Text,
    pub heading: Text,
    pub body: Text,
    pub cancel: Text,
    pub confirm: Text,
    pub interval: DialogInterval,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub union LaunchValue {
    pub file_path: Text,
    pub bitmap_path: Text,
    pub reader: ReaderLaunch,
    pub message: MessageLaunch,
    pub dialog: *const DialogLaunch,
    pub keyboard: *const KeyboardLaunch,
    pub opds_server: u32,
    pub settings_section: u8,
    pub plugins_show_opds: u8,
    pub transfer_join_network: u8,
    pub sleep_from_timeout: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Launch {
    pub kind: LaunchKind,
    pub value: LaunchValue,
}

const _: () = {
    assert!(core::mem::size_of::<LaunchValue>() == 3 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<Launch>() == 4 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<KeyboardLaunch>() == 6 * core::mem::size_of::<usize>());
};

// Rust callers cannot pair a launch kind with a partially initialized union member.
pub enum LaunchPayload {
    None,
    Files(Text),
    Bitmap(Text),
    Reader(ReaderLaunch),
    Opds(u32),
    Settings(u8),
    Plugins(u8),
    Transfer(u8),
    Sleep(u8),
    // First production consumer: P17-L1 or S3.
    #[allow(dead_code)]
    Dialog(*const DialogLaunch),
    Keyboard(*const KeyboardLaunch),
    SettingsSection(u8),
}

impl Launch {
    pub fn new(kind: LaunchKind, payload: LaunchPayload) -> Self {
        use LaunchPayload::*;
        assert!(
            matches!(
                (kind, &payload),
                (FILES, Files(_))
                    | (BITMAP, Bitmap(_))
                    | (READER, Reader(_))
                    | (OPDS, Opds(_))
                    | (SETTINGS, Settings(_))
                    | (PLUGINS, Plugins(_))
                    | (FILE_TRANSFER, Transfer(_))
                    | (SLEEP, Sleep(_))
                    | (DIALOG, Dialog(_))
                    | (KEYBOARD, Keyboard(_))
                    | (SETTINGS_SECTION, SettingsSection(0..=5))
                    | (
                        LIBRARY | OPDS_PICKER | USB_DRIVE | BOOT | CRASH | FRONTLIGHT_PANEL,
                        None
                    )
            ),
            "launch payload does not initialize its kind"
        );
        let value = match payload {
            None => LaunchValue { opds_server: 0 },
            Files(file_path) => LaunchValue { file_path },
            Bitmap(bitmap_path) => LaunchValue { bitmap_path },
            Reader(reader) => LaunchValue { reader },
            Opds(opds_server) => LaunchValue { opds_server },
            Settings(settings_section) => LaunchValue { settings_section },
            Plugins(plugins_show_opds) => LaunchValue { plugins_show_opds },
            Transfer(transfer_join_network) => LaunchValue {
                transfer_join_network,
            },
            Sleep(sleep_from_timeout) => LaunchValue { sleep_from_timeout },
            Dialog(dialog) => LaunchValue { dialog },
            Keyboard(keyboard) => LaunchValue { keyboard },
            SettingsSection(settings_section) => LaunchValue { settings_section },
        };
        Self { kind, value }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HomeRequest {
    pub menu: u8,
    pub clean_initial: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RemoteTurn {
    pub forward: u8,
    pub chapter: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BootInput {
    pub allow_sleep_at: u32,
    pub wake_release_pending: u8,
}

pub type EventKind = u8;
#[repr(C)]
#[derive(Clone, Copy)]
pub union EventValue {
    pub adopt_replace: *mut core::ffi::c_void,
    pub adopt_push: *mut core::ffi::c_void,
    pub home: HomeRequest,
    pub navigate: Launch,
    pub render_immediate: u8,
    pub sleep_from_timeout: u8,
    pub remote_button: u8,
    pub remote_turn: RemoteTurn,
    pub reader_menu: u8,
    pub defer_home: u8,
    pub boot_input: BootInput,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Event {
    pub kind: EventKind,
    pub value: EventValue,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppNativeText {
    pub bytes: Text,
    pub native_string: *const core::ffi::c_void,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppMenuResult {
    pub action: i32,
    pub orientation: u8,
    pub page_turn_option: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppChapterResult {
    pub spine: i32,
    pub anchor: Text,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppProgressResult {
    pub spine: i32,
    pub page: i32,
    pub pages: i32,
    pub xpath: Text,
    pub percentage: f32,
    pub offset: u32,
    pub has_saved_progress: u8,
    pub has_offset: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppClipResult {
    pub text: XpAppNativeText,
    pub start_page_offset: u16,
    pub end_page_offset: u16,
    pub start_word: u16,
    pub end_word: u16,
    pub word_count: u16,
    pub start_offset: u32,
    pub end_offset: u32,
    pub action: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppWifiResult {
    pub ssid: Text,
    pub ip: Text,
    pub connected: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union XpAppResultPayload {
    pub menu: XpAppMenuResult,
    pub chapter: XpAppChapterResult,
    pub progress: XpAppProgressResult,
    pub clipping: XpAppClipResult,
    pub wifi: XpAppWifiResult,
    pub text: Text,
    pub signed_value: i32,
    pub unsigned_value: u32,
}

// Tags consumed by Rust; native-only variants remain opaque to this owner.
pub const RESULT_MENU: u8 = 3;
pub const RESULT_CHAPTER: u8 = 4;
pub const RESULT_PERCENT: u8 = 5;
pub const RESULT_CLIPPING: u8 = 8;
pub const RESULT_PROGRESS: u8 = 9;
pub const RESULT_FOOTNOTE: u8 = 11;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpAppResultView {
    pub original: *const core::ffi::c_void,
    pub value: XpAppResultPayload,
    pub kind: u8,
    pub cancelled: u8,
}

pub type ResultSink = unsafe extern "C" fn(*mut core::ffi::c_void, *const XpAppResultView);
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Screenshot {
    pub reader_type: u8,
    pub title: [core::ffi::c_char; 64],
    pub spine: i32,
    pub current_page: i32,
    pub total_pages: i32,
    pub progress_percent: i32,
}

pub type ScreenshotSink = unsafe extern "C" fn(*mut core::ffi::c_void, *const Screenshot);
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Facts {
    pub visit: u32,
    pub painted_visit: u32,
    pub current_kind: u8,
    pub pending: u8,
    pub reader_in_stack: u8,
    pub foreground_reader: u8,
    pub remote_context: u8,
    pub foreground_home_shown: u8,
    pub prevent_sleep: u8,
    pub exclusive_storage: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LegacyFacts {
    pub kind: u8,
    pub reader: u8,
    pub remote_input: u8,
    pub prevent_sleep: u8,
    pub exclusive_storage: u8,
    pub skip_delay: u8,
    pub input_over_page: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RenderToken {
    pub lock: *mut core::ffi::c_void,
}

pub type DefinitionTextRelease = unsafe extern "C" fn(*mut u8, u32);
#[repr(C)]
pub struct DefinitionOwnedText {
    pub data: *mut u8,
    pub len: u32,
    pub capacity: u32,
    pub release: Option<DefinitionTextRelease>,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Legacy {
    pub create: unsafe extern "C" fn(*const Launch) -> *mut core::ffi::c_void,
    pub destroy: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub enter: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub r#loop: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub exit: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub render: unsafe extern "C" fn(*mut core::ffi::c_void, *mut RenderToken),
    pub facts: unsafe extern "C" fn(*const core::ffi::c_void, u8, u8) -> LegacyFacts,
    pub with_result:
        unsafe extern "C" fn(*mut core::ffi::c_void, ResultSink, *mut core::ffi::c_void),
    pub deliver_result: unsafe extern "C" fn(*mut core::ffi::c_void, *const core::ffi::c_void),
    pub prepare_sleep: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub home_gesture: unsafe extern "C" fn(*mut core::ffi::c_void) -> u8,
    pub forced_refresh: unsafe extern "C" fn(*mut core::ffi::c_void) -> u8,
    pub reader_turn: unsafe extern "C" fn(*mut core::ffi::c_void, u8, u8) -> u8,
    pub screenshot_info:
        unsafe extern "C" fn(*const core::ffi::c_void, ScreenshotSink, *mut core::ffi::c_void),
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Compatibility {
    pub activity_facts: unsafe extern "C" fn() -> Facts,
    pub home_action: unsafe extern "C" fn() -> u8,
    pub screenshot_info: unsafe extern "C" fn(ScreenshotSink, *mut core::ffi::c_void),
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MappedSnapshot {
    pub input: xp_ui::model::Input,
    pub power_held_ms: u32,
    pub power_pressed: u8,
    pub power_released: u8,
    pub power_held: u8,
    pub physical_down: u8,
    pub any_pressed: u8,
    pub any_released: u8,
    pub touch_activity: u8,
    pub home_key_pressed: u8,
    pub home_key_tapped: u8,
    pub home_key_long_pressed: u8,
    pub has_home_key: u8,
    pub has_switch: u8,
    pub switch_on: u8,
    pub usb_changed: u8,
    pub home_gesture: u8,
    pub light_panel_gesture: u8,
    pub status_bar_tap: u8,
    pub suppressed_release: u8,
    pub cpu_hold: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Settings {
    pub power_duration: u32,
    pub sleep_minutes: u32,
    pub idle_power_saving: u32,
    pub home_tap: u8,
    pub home_double: u8,
    pub home_long: u8,
    pub short_power: u8,
    pub double_power_light: u8,
    pub slide_action: u8,
    pub tilt: u8,
    pub reader_orientation: u8,
    pub fading_fix: u8,
    pub screen_inverted: u8,
    pub touch_reader: u8,
    pub frontlight_on: u8,
    pub is_x4pro: u8,
    pub is_papermono: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DisplayGeometry {
    pub width: i32,
    pub height: i32,
    pub orientation: i32,
    pub frame_available: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BleScene {
    pub visit: u32,
    pub r#where: u8,
    pub page_shown: u8,
    pub storage_busy: u8,
    pub wifi_on: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HomeSettings {
    pub plugins: u8,
    pub opds: u8,
    pub library_metadata: u8,
    pub grid: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Theme {
    pub screen: xp_ui::scene::Rect,
    pub content: xp_ui::scene::Rect,
    pub safe: xp_ui::scene::Rect,
    pub title_font: i32,
    pub small_font: i32,
    pub tiny_font: i32,
    pub body_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub home_top: i16,
    pub tile_height: i16,
    pub cover_height: i16,
    pub side_padding: i16,
    pub spacing: i16,
    pub menu_top: i16,
    pub menu_height: i16,
    pub menu_gap: i16,
    pub hints_height: i16,
    pub scroll_width: i16,
    pub scroll_offset: i16,
    pub header_side: i16,
    pub battery_height: i16,
    pub corner: i16,
    pub tab_height: i16,
    pub tab_height_pct: i16,
    pub tab_width_pct: i16,
    pub tab_offset: i16,
    pub theme: u8,
    pub continue_in_menu: u8,
    pub columns: u8,
    pub touch: u8,
    pub body_bold: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BitmapInfo {
    pub width: i32,
    pub height: i32,
    pub valid: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TextLayout {
    pub font: i32,
    pub width: i32,
    pub lines: u8,
    pub style: u8,
}

pub type MemoryDomain = u8;
pub type BookKind = u8;
/// Address of an immutable, process-lifetime registry pointer in the native resource context.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct FontOwner(*const *const core::ffi::c_void);
// Sharing this opaque slot grants no access to the serialized registry.
unsafe impl Send for FontOwner {}
unsafe impl Sync for FontOwner {}
impl FontOwner {
    #[allow(dead_code)] // ABI fixtures have no native registry.
    pub const NONE: Self = Self(core::ptr::null());
    #[allow(dead_code)] // Shared ABI fixtures do not use the font owner.
    pub(crate) unsafe fn get(self) -> *const core::ffi::c_void {
        if self.0.is_null() {
            core::ptr::null()
        } else {
            self.0.read()
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Resources {
    pub settings_snapshot: unsafe extern "C" fn() -> HomeSettings,
    pub recents_visit: unsafe extern "C" fn(BookSink, *mut core::ffi::c_void) -> u8,
    pub recent_missing: unsafe extern "C" fn(Text) -> u8,
    pub recent_update: unsafe extern "C" fn(*const Book),
    pub opds_count: unsafe extern "C" fn() -> u32,
    pub book_progress: unsafe extern "C" fn(Text) -> i32,
    pub library_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub library_count: unsafe extern "C" fn(*mut core::ffi::c_void) -> u32,
    pub library_row: unsafe extern "C" fn(
        *mut core::ffi::c_void,
        u32,
        u8,
        BookSink,
        *mut core::ffi::c_void,
    ) -> u8,
    pub library_close: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub library_rebuild: unsafe extern "C" fn(u8) -> u8,
    pub book_open: unsafe extern "C" fn(BookKind, Text) -> *mut core::ffi::c_void,
    pub book_thumb_path:
        unsafe extern "C" fn(*mut core::ffi::c_void, i32, TextSink, *mut core::ffi::c_void) -> u8,
    pub book_load: unsafe extern "C" fn(*mut core::ffi::c_void, u8, u8) -> u8,
    pub book_generate_thumb: unsafe extern "C" fn(*mut core::ffi::c_void, i32) -> u8,
    pub book_generate_thumb_from_source: unsafe extern "C" fn(*mut core::ffi::c_void, i32) -> u8,
    pub book_close: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub path_exists: unsafe extern "C" fn(Text) -> u8,
    pub thumbnail_path: unsafe extern "C" fn(Text, i32, TextSink, *mut core::ffi::c_void) -> u8,
    pub bitmap_open: unsafe extern "C" fn(Text) -> *mut core::ffi::c_void,
    pub bitmap_info: unsafe extern "C" fn(*mut core::ffi::c_void) -> BitmapInfo,
    pub bitmap_draw: unsafe extern "C" fn(*mut core::ffi::c_void, xp_ui::scene::Rect, i32) -> u8,
    pub bitmap_close: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub theme_snapshot: unsafe extern "C" fn(*mut Theme),
    #[cfg(any(test, feature = "app-test"))]
    pub translate: unsafe extern "C" fn(u16) -> Text,
    #[cfg(not(any(test, feature = "app-test")))]
    pub language: unsafe extern "C" fn() -> u8,
    pub header_draw: unsafe extern "C" fn(xp_ui::scene::Rect, Text),
    pub footer_draw: unsafe extern "C" fn(*const Text),
    pub placeholder_draw: unsafe extern "C" fn(xp_ui::scene::Rect),
    pub popup_draw: unsafe extern "C" fn(Text) -> xp_ui::scene::Rect,
    pub popup_progress: unsafe extern "C" fn(xp_ui::scene::Rect, i32),
    pub icon_draw: unsafe extern "C" fn(u8, xp_ui::scene::Rect),
    pub text_draw: unsafe extern "C" fn(*const xp_ui::scene::Command, xp_ui::scene::Rect, Text),
    #[cfg(any(test, feature = "app-test"))]
    pub text_width: unsafe extern "C" fn(Text, i32, u8) -> i32,
    #[cfg(any(test, feature = "app-test"))]
    pub line_height: unsafe extern "C" fn(i32) -> i32,
    pub text_wrap: unsafe extern "C" fn(Text, TextLayout, TextSink, *mut core::ffi::c_void) -> u8,
    pub text_truncate:
        unsafe extern "C" fn(Text, TextLayout, TextSink, *mut core::ffi::c_void) -> u8,
    pub text_box_height: unsafe extern "C" fn(Text, TextLayout) -> i32,
    pub region_size: unsafe extern "C" fn(xp_ui::scene::Rect) -> usize,
    pub region_read: unsafe extern "C" fn(xp_ui::scene::Rect, *mut u8, usize) -> u8,
    pub region_write: unsafe extern "C" fn(xp_ui::scene::Rect, *const u8, usize) -> u8,
    pub rounded_mask: unsafe extern "C" fn(xp_ui::scene::Rect, u8, u8),
    pub polygon_draw: unsafe extern "C" fn(*const Point, u8, u8),
    #[cfg(any(test, feature = "app-test"))]
    pub paint_scene:
        unsafe extern "C" fn(*const xp_ui::ffi::SceneView, *const xp_ui::ffi::Paint) -> u8,
    pub refresh: unsafe extern "C" fn(u8),
    pub buffer_allocate: unsafe extern "C" fn(MemoryDomain, usize) -> *mut u8,
    pub buffer_release: unsafe extern "C" fn(MemoryDomain, *mut u8),
    pub font_owner: FontOwner,
}

const _: () = assert!(
    core::mem::size_of::<Resources>()
        == (44 + 3 * cfg!(any(test, feature = "app-test")) as usize)
            * core::mem::size_of::<usize>()
);

#[cfg(not(feature = "app-test"))]
#[repr(C)]
pub struct RawInput {
    pub power: u8,
    pub first: i32,
    pub second: i32,
}
#[cfg(not(feature = "app-test"))]
const _: () = {
    assert!(core::mem::size_of::<RawInput>() == 12);
    assert!(core::mem::offset_of!(RawInput, first) == 4);
    assert!(core::mem::offset_of!(RawInput, second) == 8);
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Runtime {
    #[cfg(feature = "app-test")]
    pub input_mapped_snapshot: unsafe extern "C" fn(u8, u8, u8, *mut MappedSnapshot),
    #[cfg(not(feature = "app-test"))]
    pub input_mapped_query: unsafe extern "C" fn(u8, u8, u8, u8, *mut MappedSnapshot),
    #[cfg(feature = "app-test")]
    pub input_shared_power_mode: unsafe extern "C" fn(u8),
    #[cfg(not(feature = "app-test"))]
    pub input_poll: unsafe extern "C" fn(u8, u8) -> crate::hal::gpio::Poll,
    pub input_power_confirm: unsafe extern "C" fn(u8),
    pub input_remote_click: unsafe extern "C" fn(u8) -> u8,
    pub loop_settings: unsafe extern "C" fn(*mut Settings),
    pub display_geometry: unsafe extern "C" fn() -> DisplayGeometry,
    pub header_back_clear: unsafe extern "C" fn(),
    pub now_ms: unsafe extern "C" fn() -> u32,
    pub delay_ms: unsafe extern "C" fn(u32),
    #[cfg(feature = "app-test")]
    pub idle_wait: unsafe extern "C" fn(u32, u32),
    #[cfg(not(feature = "app-test"))]
    pub raw_input_sample: unsafe extern "C" fn(*mut RawInput),
    pub yield_task: unsafe extern "C" fn(),
    #[cfg(feature = "app-test")]
    pub power_saving: unsafe extern "C" fn(u8),
    #[cfg(not(feature = "app-test"))]
    pub power_context: unsafe extern "C" fn(*mut crate::hal::power::Context),
    #[cfg(feature = "app-test")]
    pub tilt_update: unsafe extern "C" fn(u8, u8, u8) -> u8,
    #[cfg(not(feature = "app-test"))]
    pub tilt_context: unsafe extern "C" fn(*mut crate::hal::tilt::Context),
    pub battery_capacity_step: unsafe extern "C" fn() -> u8,
    pub fading_fix: unsafe extern "C" fn(u8),
    pub memory_diagnostic: unsafe extern "C" fn(),
    pub console_service: unsafe extern "C" fn(),
    pub screenshot: unsafe extern "C" fn(),
    pub touch_feedback: unsafe extern "C" fn(u8),
    pub frontlight_present: unsafe extern "C" fn() -> u8,
    #[cfg(feature = "app-test")]
    pub frontlight_state: unsafe extern "C" fn() -> u8,
    #[cfg(not(feature = "app-test"))]
    pub light_state: unsafe extern "C" fn() -> *mut crate::hal::Light,
    #[cfg(feature = "app-test")]
    pub frontlight_set: unsafe extern "C" fn(u8),
    #[cfg(not(feature = "app-test"))]
    pub light_output: unsafe extern "C" fn(u8),
    pub settings_save: unsafe extern "C" fn(*const Settings),
    pub ble_tick: unsafe extern "C" fn(*const BleScene) -> u8,
    pub ble_before_screen_change: unsafe extern "C" fn(u8) -> u8,
    pub sleep_enter: unsafe extern "C" fn(u8),
    pub diagnostic: unsafe extern "C" fn(u8, u32, u32),
    pub render_lock: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub render_try_lock: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub render_unlock: unsafe extern "C" fn(*mut core::ffi::c_void),
    pub model_lock: unsafe extern "C" fn(),
    pub model_unlock: unsafe extern "C" fn(),
    pub notify_render: unsafe extern "C" fn(),
    pub render_owned_by_current_task_retire_p18: unsafe extern "C" fn() -> u8,
}

const _: () = {
    assert!(core::mem::size_of::<Runtime>() == 34 * core::mem::size_of::<usize>());
    assert!(
        core::mem::offset_of!(Runtime, render_owned_by_current_task_retire_p18)
            == 33 * core::mem::size_of::<usize>()
    );
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Platform {
    pub legacy: *const Legacy,
    pub resources: *const Resources,
    pub runtime: *const Runtime,
    pub epub: *const super::epub::abi::XpEpubLeaves,
}

pub const OK: u8 = 0;
pub const INVALID: u8 = 1;
pub const WRONG_CONTEXT: u8 = 3;
pub const ACCEPTED: u8 = 0;
pub const REJECTED: u8 = 1;
pub const EVENT_OUT_OF_MEMORY: u8 = 2;
pub const EVENT_INVALID: u8 = 3;
pub const FILES: u8 = 0;
pub const LIBRARY: u8 = 1;
pub const OPDS: u8 = 2;
pub const OPDS_PICKER: u8 = 3;
pub const PLUGINS: u8 = 4;
pub const FILE_TRANSFER: u8 = 5;
pub const USB_DRIVE: u8 = 6;
pub const SETTINGS: u8 = 7;
pub const READER: u8 = 8;
pub const BITMAP: u8 = 9;
pub const BOOT: u8 = 10;
pub const SLEEP: u8 = 11;
pub const MESSAGE: u8 = 12;
pub const CRASH: u8 = 13;
pub const FRONTLIGHT_PANEL: u8 = 14;
pub const NETWORK_MODE: u8 = 15;
pub const DIALOG: u8 = 16;
pub const KEYBOARD: u8 = 17;
pub const WIFI: u8 = 18;
pub const SETTINGS_SECTION: u8 = 19;
pub const ADOPT_REPLACE: u8 = 0;
pub const ADOPT_PUSH: u8 = 1;
pub const POP: u8 = 2;
pub const HOME_REQUESTED: u8 = 3;
pub const NAVIGATE: u8 = 4;
pub const RENDER_REQUESTED: u8 = 5;
pub const SLEEP_REQUESTED: u8 = 6;
pub const PREPARE_SLEEP: u8 = 7;
pub const REMOTE_CLICK: u8 = 8;
pub const REMOTE_TURN: u8 = 9;
pub const REMOTE_SHORTCUT: u8 = 10;
pub const FORCED_REFRESH: u8 = 11;
pub const INPUT_POLLED: u8 = 12;
pub const RESET_HOME_INPUT: u8 = 13;
pub const BOOT_INPUT_STATE: u8 = 14;
pub const MEMORY_DEFAULT: u8 = 0;
pub const MEMORY_PSRAM_ONLY: u8 = 1;
pub const BOOK_REFLOWABLE: u8 = 0;
pub const BOOK_XTC: u8 = 1;

#[cfg(test)]
mod launch_tests {
    use super::*;

    #[test]
    #[should_panic(expected = "launch payload does not initialize its kind")]
    fn files_rejects_byte_payload() {
        Launch::new(FILES, LaunchPayload::Settings(0));
    }

    #[test]
    fn font_owner_slots_keep_independent_pointer_provenance() {
        let first = 11_u8;
        let second = 22_u8;
        let first_slot = core::ptr::addr_of!(first).cast();
        let second_slot = core::ptr::addr_of!(second).cast();
        let a = FontOwner(&first_slot);
        let b = FontOwner(&second_slot);
        unsafe {
            let captured = a.get();
            assert_eq!(b.get().cast::<u8>().read(), 22);
            assert_eq!(captured.cast::<u8>().read(), 11);
            assert_eq!(a.get(), captured);
            assert!(FontOwner::NONE.get().is_null());
        }
    }
}
