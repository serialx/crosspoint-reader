//! Borrowed native storage views; valid only within their original guard scopes.
use super::wifi_view;
use crate::{
    scene::{Command, Rect},
    settings::settings_ffi::Services,
};
use core::ffi::{c_char, c_void};
pub const ERROR_GENERAL: u32 = 0x200000;
pub const ERROR_MISSING: u32 = 0x200001;
pub const ERROR_TIMEOUT: u32 = 0x200002;
pub const HIDDEN_LABEL: u32 = 0x200003;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bytes {
    pub data: *const u8,
    pub len: usize,
}
impl Bytes {
    pub const EMPTY: Self = Self {
        data: core::ptr::null(),
        len: 0,
    };
    pub fn from_slice(s: &[u8]) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
    /// # Safety
    /// The native loan is live, unmodified and guarded for the returned borrow.
    pub unsafe fn as_slice(&self) -> &[u8] {
        if self.len == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(self.data, self.len) }
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text {
    pub string: *const c_void,
    pub bytes: Bytes,
}
impl Text {
    pub const EMPTY: Self = Self {
        string: core::ptr::null(),
        bytes: Bytes::EMPTY,
    };
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Row {
    pub ssid: Text,
    pub rssi: i32,
    pub encrypted: u8,
    pub saved: u8,
    pub hidden: u8,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CredentialView {
    pub ssid: Text,
    pub password: Text,
}
#[repr(C)]
pub struct LastStorage {
    pub words: [usize; 7],
}
#[repr(C)]
pub struct CredentialStorage {
    pub words: [usize; 13],
}
#[repr(C)]
pub struct KeyboardStorage {
    pub words: [usize; 6],
}
#[repr(C)]
pub struct Snapshot {
    pub strings: [Text; 5],
    pub row: Row,
    pub status: Bytes,
    pub row_count: u32,
    pub row_valid: u8,
    pub attempted: u8,
}
#[repr(C)]
pub struct Edits {
    pub attempt: *const Text,
    pub assign: [*const Text; 4],
}
impl Edits {
    pub fn empty() -> Self {
        Self {
            attempt: core::ptr::null(),
            assign: [core::ptr::null(); 4],
        }
    }
}
#[repr(C)]
pub struct PaintSnapshot {
    pub row_text: *const c_char,
    pub mac: *const c_char,
    pub real_count: u16,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Diagnostic {
    ExitStart,
    ScanDelete,
    ScanDeleted,
    ExitEnd,
    AutoAttempt,
    Saved,
    HiddenSaved,
    Connected,
    AutoFailure,
    Manual,
    SnapshotOom,
    SceneOom,
    InvalidScene,
}
#[repr(C)]
pub struct Physical {
    pub allow_auto: unsafe extern "C" fn(*mut c_void) -> u8,
    pub resource_lock: unsafe extern "C" fn(*mut c_void, u8),
    pub release_resources: unsafe extern "C" fn(*mut c_void),
    pub layout: unsafe extern "C" fn(*mut c_void, *mut wifi_view::Layout),
    pub metrics: unsafe extern "C" fn(*mut c_void) -> Services,
    pub view_text: unsafe extern "C" fn(*mut c_void, u32) -> *const c_char,
    pub paint: unsafe extern "C" fn(*mut c_void, *const Command, Rect, *const PaintSnapshot),
    pub paint_strings_clear: unsafe extern "C" fn(*mut c_void),
    pub prompt_label: unsafe extern "C" fn(*mut c_void),
    pub read_mac: unsafe extern "C" fn(*mut c_void),
    pub last_ssid_drop: unsafe extern "C" fn(*mut LastStorage),
    pub last_ssid: unsafe extern "C" fn(*mut LastStorage) -> Text,
    pub credential_lookup:
        unsafe extern "C" fn(*mut CredentialStorage, Text, *mut CredentialView) -> u8,
    pub credential_drop: unsafe extern "C" fn(*mut CredentialStorage),
    pub store_load: unsafe extern "C" fn(),
    pub store_save: unsafe extern "C" fn(Text, Text),
    pub store_remove: unsafe extern "C" fn(*mut c_void, Text),
    pub store_last: unsafe extern "C" fn(Text),
    pub snapshot: unsafe extern "C" fn(*mut c_void, u32, *const Text, *mut Snapshot),
    pub text_apply: unsafe extern "C" fn(*mut c_void, *const Edits),
    pub rows_reset: unsafe extern "C" fn(*mut c_void, u32, u8),
    pub row_write: unsafe extern "C" fn(*mut c_void, u32, *const Row, *const u32),
    pub rows_sort: unsafe extern "C" fn(*mut c_void, unsafe extern "C" fn(u8, i32, u8, i32) -> u8),
    pub statuses_rebuild: unsafe extern "C" fn(*mut c_void, unsafe extern "C" fn(i32) -> u8),
    pub attempts_reset: unsafe extern "C" fn(*mut c_void) -> u32,
    pub station_mode: unsafe extern "C" fn(),
    pub disconnect: unsafe extern "C" fn(),
    pub scan_begin: unsafe extern "C" fn(),
    pub scan_complete: unsafe extern "C" fn() -> i32,
    pub scan_ssid: unsafe extern "C" fn(u32, *mut c_char),
    pub scan_rssi: unsafe extern "C" fn(u32) -> i32,
    pub scan_delete: unsafe extern "C" fn(),
    pub connect: unsafe extern "C" fn(Text, Text, u8),
    pub connection_status: unsafe extern "C" fn() -> i32,
    pub connection_ip: unsafe extern "C" fn(*mut c_char),
    pub connected_diagnostic: unsafe extern "C" fn(),
    pub clock_available: unsafe extern "C" fn() -> u8,
    pub clock_synced: unsafe extern "C" fn() -> u8,
    pub clock_sync: unsafe extern "C" fn() -> u8,
    pub clock_mark_and_save: unsafe extern "C" fn(),
    pub trusted_time_start: unsafe extern "C" fn(),
    pub plugin_drain: unsafe extern "C" fn(*mut c_void),
    pub diagnostic: unsafe extern "C" fn(*mut c_void, Diagnostic, u32),
    pub prepare_result_retire_zc1: unsafe extern "C" fn(*mut c_void, u8),
    pub keyboard_text: unsafe extern "C" fn(*mut KeyboardStorage, Bytes) -> Text,
    pub keyboard_text_drop: unsafe extern "C" fn(*mut KeyboardStorage),
}
const _: () = {
    assert!(core::mem::size_of::<Physical>() == 46 * core::mem::size_of::<usize>());
    #[cfg(target_pointer_width = "32")]
    {
        assert!(core::mem::size_of::<Snapshot>() == 96);
        assert!(core::mem::size_of::<Row>() == 20);
        assert!(core::mem::size_of::<CredentialView>() == 24);
    }
};

#[cfg(test)]
mod tests;
