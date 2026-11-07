use core::ffi::{c_char, c_void};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bytes {
    pub data: *const u8,
    pub length: usize,
}
impl Bytes {
    pub const EMPTY: Self = Self {
        data: core::ptr::null(),
        length: 0,
    };
    pub fn from_slice(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            length: bytes.len(),
        }
    }
    pub unsafe fn slice<'a>(self) -> &'a [u8] {
        if self.length == 0 {
            &[]
        } else {
            core::slice::from_raw_parts(self.data, self.length)
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct NativeText {
    pub native_string: *const c_void,
    pub bytes: Bytes,
}
impl NativeText {
    pub const EMPTY: Self = Self {
        native_string: core::ptr::null(),
        bytes: Bytes::EMPTY,
    };
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct MutableText {
    pub native_string: *mut c_void,
    pub data: *mut u8,
    pub length: usize,
    pub capacity: usize,
}
impl MutableText {
    pub fn view(&self) -> NativeText {
        NativeText {
            native_string: self.native_string,
            bytes: Bytes {
                data: self.data,
                length: self.length,
            },
        }
    }
}
#[repr(C)]
pub struct TextStorage {
    pub words: [usize; 6],
}
#[repr(C)]
pub struct TextLeaves {
    pub initialize: unsafe extern "C" fn(*mut TextStorage, usize, *mut MutableText),
    pub resize: unsafe extern "C" fn(*mut MutableText, usize),
    pub release: unsafe extern "C" fn(*mut MutableText),
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Facts {
    pub status: i32,
    pub length: usize,
    pub known_length: bool,
    pub complete: bool,
    pub aborted: bool,
}
#[repr(C)]
pub struct Callbacks {
    pub context: *mut c_void,
    pub chunk: unsafe extern "C" fn(*mut c_void, Bytes, *const Facts) -> bool,
    pub should_abort: Option<unsafe extern "C" fn(*mut c_void) -> bool>,
}
#[repr(C)]
pub struct Configuration {
    pub timeout_ms: u32,
    pub insecure: bool,
    pub basic_auth: bool,
    pub user_agent: Bytes,
    pub username: NativeText,
    pub password: NativeText,
}
#[repr(C)]
pub struct SdkLeaves {
    pub acquire: unsafe extern "C" fn(*mut c_void, NativeText) -> bool,
    pub release: unsafe extern "C" fn(*mut c_void),
    pub configure: unsafe extern "C" fn(*mut c_void, *const Configuration),
    pub add_header: unsafe extern "C" fn(*mut c_void, NativeText, NativeText),
    pub send: unsafe extern "C" fn(
        *mut c_void,
        *const c_char,
        Bytes,
        *const Callbacks,
        *mut Facts,
    ) -> i32,
    pub response_header:
        unsafe extern "C" fn(*mut c_void, *const c_char, *mut TextStorage, *mut MutableText),
}
#[repr(C)]
pub struct FileLeaves {
    pub open_write: unsafe extern "C" fn(*mut c_void, NativeText) -> bool,
    pub write: unsafe extern "C" fn(*mut c_void, Bytes) -> usize,
    pub close: unsafe extern "C" fn(*mut c_void, bool),
    pub remove: unsafe extern "C" fn(NativeText) -> bool,
    pub replace: unsafe extern "C" fn(NativeText, NativeText) -> bool,
}
#[repr(C)]
pub struct RadioLeaves {
    pub get_ps: unsafe extern "C" fn(*mut i32) -> i32,
    pub set_ps: unsafe extern "C" fn(i32) -> i32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub enum Diagnostic {
    Fetching,
    Downloading,
    PostDownload,
    GetHeap,
    Get,
    Post,
    BadUrl,
    Unauthorized,
    Failed,
    Incomplete,
    OpenFailed,
    PostFailed,
    Empty,
    PostIncomplete,
    ReplaceFailed,
    Downloaded,
    GuardEnterFailed,
    GuardRestoreFailed,
}
#[repr(C)]
pub struct Diagnostics {
    pub emit: unsafe extern "C" fn(Diagnostic, Bytes, Bytes, usize, usize),
}
#[repr(C)]
pub struct Leaves {
    pub sdk: *const SdkLeaves,
    pub file: *const FileLeaves,
    pub radio: *const RadioLeaves,
    pub text: *const TextLeaves,
    pub diagnostics: *const Diagnostics,
}
#[repr(C)]
pub struct Consumer {
    pub context: *mut c_void,
    pub header_count: usize,
    pub header_at: unsafe extern "C" fn(*mut c_void, usize, *mut NativeText, *mut NativeText),
    pub sink: Option<unsafe extern "C" fn(*mut c_void, Bytes) -> bool>,
    pub progress: Option<unsafe extern "C" fn(*mut c_void, usize, usize)>,
}
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
// Every variant is constructed by the native request facade.
#[allow(dead_code)]
pub enum Kind {
    Fetch,
    Catalog,
    Form,
    Download,
    PostFile,
}
#[repr(C)]
pub struct Request {
    pub kind: Kind,
    pub url: NativeText,
    pub username: NativeText,
    pub password: NativeText,
    pub bearer: NativeText,
    pub accept: NativeText,
    pub accept_language: NativeText,
    pub body: NativeText,
    pub content_type: NativeText,
    pub destination: NativeText,
    pub form_response: *mut MutableText,
    pub consumer: *const Consumer,
    pub cancel_flag: *const bool,
    pub status_output: *mut i32,
    pub last_status: *mut i32,
    pub last_redirected: *mut bool,
    pub capture_error_body: bool,
    pub downgrade_redirects: bool,
    pub client_storage: *mut c_void,
    pub file_storage: *mut c_void,
}
#[repr(C)]
pub struct GuardToken {
    pub previous_mode: i32,
}
