//! ArduinoJson conversion leaves; field names, defaults, and ordering belong to Rust.
use super::mapping::Span;
use core::ffi::{c_char, c_void};
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Read {
    CString,
    String,
    U8,
    U8Default,
    Bool,
    F32,
    I64,
    U32,
    U16,
    Null,
    Object,
    Raw,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Type {
    String,
    U8,
    Bool,
    F32,
    I64,
    U32,
    U16,
    Secret,
    Raw,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Value {
    pub kind: u32,
    pub number: i64,
    pub real: f64,
    pub text: Span,
    pub valid: u8,
}
impl Value {
    pub fn num(kind: Type, n: i64) -> Self {
        Self {
            kind: kind as u32,
            number: n,
            real: 0.0,
            text: Span::new(&[]),
            valid: 1,
        }
    }
    pub fn string(s: &[u8]) -> Self {
        Self {
            kind: Type::String as u32,
            number: 0,
            real: 0.0,
            text: Span::new(s),
            valid: 1,
        }
    }
}
#[repr(C)]
pub struct Json {
    pub ctx: *mut c_void,
    pub get: unsafe extern "C" fn(*mut c_void, u32, *const c_char, u32) -> Value,
    pub set: unsafe extern "C" fn(*mut c_void, u32, *const c_char, Value),
    pub select: unsafe extern "C" fn(*mut c_void, u32, *const c_char, usize, u32, u32),
    pub count: unsafe extern "C" fn(*mut c_void, u32) -> usize,
    pub key: unsafe extern "C" fn(*mut c_void, u32, usize) -> *const c_char,
}
impl Json {
    pub fn get(&self, parent: u32, key: &[u8], mode: Read) -> Value {
        unsafe { (self.get)(self.ctx, parent, key.as_ptr().cast(), mode as u32) }
    }
    pub fn set(&self, parent: u32, key: &[u8], v: Value) {
        unsafe {
            (self.set)(self.ctx, parent, key.as_ptr().cast(), v);
        }
    }
    pub fn select(&self, parent: u32, key: &[u8], index: usize, to: u32, kind: u32) {
        unsafe {
            (self.select)(
                self.ctx,
                parent,
                if key.is_empty() {
                    core::ptr::null()
                } else {
                    key.as_ptr().cast()
                },
                index,
                to,
                kind,
            );
        }
    }
    pub fn count(&self, parent: u32) -> usize {
        unsafe { (self.count)(self.ctx, parent) }
    }
}
#[repr(C)]
pub struct Model {
    pub ctx: *mut c_void,
    pub get: unsafe extern "C" fn(*mut c_void, u32) -> Value,
    pub set: unsafe extern "C" fn(*mut c_void, u32, Value),
}
impl Model {
    pub fn get(&self, field: u32) -> Value {
        unsafe { (self.get)(self.ctx, field) }
    }
    pub fn set(&self, field: u32, v: Value) {
        unsafe {
            (self.set)(self.ctx, field, v);
        }
    }
}

pub mod store_field {
    pub const USERNAME: u32 = 0;
    pub const PASSWORD: u32 = 1;
    pub const SERVER_URL: u32 = 2;
    pub const SERVER_TYPE: u32 = 3;
    pub const MATCH_METHOD: u32 = 4;
    pub const SEND_METADATA: u32 = 5;
    pub const SYNC_CLIPPINGS: u32 = 6;
    pub const BEHAVIOR: u32 = 7;
    pub const HEADER0_NAME: u32 = 8;
    pub const HEADER0_VALUE: u32 = 9;
    pub const HEADER1_NAME: u32 = 10;
    pub const HEADER1_VALUE: u32 = 11;
    pub const EXTRACTED_PASSWORD: u32 = 12;
    pub const BASE_URL: u32 = 13;
}
pub mod protocol_field {
    pub const USERNAME: u32 = 0;
    pub const PASSWORD: u32 = 1;
    pub const MD5: u32 = 2;
    pub const BASIC: u32 = 3;
    pub const HEADER0_NAME: u32 = 4;
    pub const HEADER0_VALUE: u32 = 5;
    pub const HEADER1_NAME: u32 = 6;
    pub const HEADER1_VALUE: u32 = 7;
    pub const SERVER_TYPE: u32 = 8;
    pub const DOCUMENT: u32 = 100;
    pub const PROGRESS: u32 = 101;
    pub const PERCENTAGE: u32 = 102;
    pub const DEVICE: u32 = 103;
    pub const DEVICE_ID: u32 = 104;
    pub const TIMESTAMP: u32 = 105;
    pub const HAS_METADATA: u32 = 110;
    pub const FILENAME: u32 = 111;
    pub const TITLE: u32 = 112;
    pub const AUTHORS: u32 = 113;
    pub const EXTRA_JSON: u32 = 114;
    pub const ISBN: u32 = 115;
    pub const ASIN: u32 = 116;
    pub const SERIES: u32 = 117;
    pub const SERIES_INDEX: u32 = 118;
    pub const HAS_POSITION: u32 = 120;
    pub const PCT_Q: u32 = 121;
    pub const SPINE: u32 = 122;
    pub const PAGE: u32 = 123;
    pub const PAGES: u32 = 124;
    pub const PARAGRAPH: u32 = 125;
    pub const XPATH: u32 = 126;
}
