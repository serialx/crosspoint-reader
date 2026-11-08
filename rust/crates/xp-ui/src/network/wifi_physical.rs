//! Physical Wi-Fi leaves and short native-store loans; Rust owns visit resources.
use super::wifi_view;
use crate::scene::{Command, Rect};
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
pub struct CredentialView {
    pub ssid: Bytes,
    pub password: Bytes,
}
#[repr(C)]
pub struct LastStorage {
    pub words: [usize; 7],
}
#[repr(C)]
pub struct CredentialStorage {
    pub words: [usize; 13],
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum DrawKind {
    Text,
    Centered,
    FlowText,
    Wrapped,
    WrappedBottom,
    Header,
    Subheader,
    Footer,
    Help,
}
#[repr(C)]
pub struct PaintData {
    pub labels: [*const c_char; 4],
    pub kind: DrawKind,
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
    pub width: unsafe extern "C" fn(*mut c_void, *const c_char, i32, u8) -> i32,
    pub height: unsafe extern "C" fn(*mut c_void, *const c_char, i32, u8, u8, i16) -> i32,
    pub line: unsafe extern "C" fn(*mut c_void, i32) -> i32,
    pub paint: unsafe extern "C" fn(*mut c_void, *const Command, Rect, *const PaintData),
    pub read_mac: unsafe extern "C" fn(*mut u8) -> i32,
    pub last_ssid_drop: unsafe extern "C" fn(*mut LastStorage),
    pub last_ssid: unsafe extern "C" fn(*mut LastStorage) -> Bytes,
    pub credential_lookup:
        unsafe extern "C" fn(*mut CredentialStorage, Bytes, *mut CredentialView) -> u8,
    pub credential_drop: unsafe extern "C" fn(*mut CredentialStorage),
    pub store_load: unsafe extern "C" fn(),
    pub store_save: unsafe extern "C" fn(Bytes, Bytes),
    pub store_remove: unsafe extern "C" fn(Bytes),
    pub store_last: unsafe extern "C" fn(Bytes),
    pub store_count: unsafe extern "C" fn() -> u32,
    pub store_has: unsafe extern "C" fn(Bytes) -> u8,
    pub station_mode: unsafe extern "C" fn(),
    pub disconnect: unsafe extern "C" fn(),
    pub scan_begin: unsafe extern "C" fn(),
    pub scan_complete: unsafe extern "C" fn() -> i32,
    pub scan_ssid: unsafe extern "C" fn(u32, *mut c_char),
    pub scan_rssi: unsafe extern "C" fn(u32) -> i32,
    pub scan_encrypted: unsafe extern "C" fn(u32) -> u8,
    pub scan_delete: unsafe extern "C" fn(),
    pub connect: unsafe extern "C" fn(*const c_char, *const c_char, u8),
    pub connection_status: unsafe extern "C" fn() -> i32,
    pub connection_ip: unsafe extern "C" fn(*mut c_char),
    pub connected_diagnostic: unsafe extern "C" fn(),
    pub clock_available: unsafe extern "C" fn() -> u8,
    pub clock_synced: unsafe extern "C" fn() -> u8,
    pub clock_sync: unsafe extern "C" fn() -> u8,
    pub clock_mark_and_save: unsafe extern "C" fn(),
    pub trusted_time_start: unsafe extern "C" fn(),
    pub plugin_drain: unsafe extern "C" fn(*mut c_void),
    pub diagnostic: unsafe extern "C" fn(Diagnostic, Bytes, u32),
    pub prepare_result_retire_zc1: unsafe extern "C" fn(*mut c_void, u8, Bytes, Bytes),
}
const _: () = {
    assert!(core::mem::size_of::<Physical>() == 39 * core::mem::size_of::<usize>());
    #[cfg(target_pointer_width = "32")]
    {
        assert!(core::mem::size_of::<CredentialView>() == 16);
        assert!(core::mem::size_of::<PaintData>() == 20);
    }
};

#[cfg(test)]
mod tests;
