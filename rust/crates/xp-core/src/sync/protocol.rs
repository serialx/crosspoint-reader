//! KOSync request/response policy; HTTP, crypto, and JSON conversion remain leaves.
use super::json::protocol_field as field;
use super::{
    json::{Json, Model, Read, Type, Value},
    mapping::Span,
};
use core::ffi::c_void;
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Op {
    Status,
    UrlReset,
    UrlAppend,
    Heap,
    Insecure,
    Begin,
    Header,
    Get,
    Post,
    Put,
    End,
    JsonReset,
    ParseResponse,
    ParseExtra,
    Serialize,
    ReleaseExtra,
}
#[repr(C)]
pub struct Transport {
    pub ctx: *mut c_void,
    pub call: unsafe extern "C" fn(*mut c_void, u32, Span, Value) -> Value,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, i32)>,
}
impl Transport {
    fn call(&self, op: Op, key: &[u8], v: Value) -> Value {
        unsafe { (self.call)(self.ctx, op as u32, Span::new(key), v) }
    }
    #[inline(never)]
    fn simple(&self, op: Op) -> Value {
        self.call(op, &[], Value::num(Type::I64, 0))
    }
    fn log(&self, event: u32, n: i32) {
        if let Some(f) = self.diagnostic {
            unsafe {
                f(self.ctx, event, n);
            }
        }
    }
    fn header(&self, key: &[u8], v: Value) {
        self.call(Op::Header, key, v);
    }
}
fn string(s: &[u8]) -> Value {
    Value::string(s)
}
unsafe fn bytes<'a>(v: Value) -> &'a [u8] {
    if v.text.len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(v.text.data, v.text.len)
    }
}
fn custom(t: &Transport, m: &Model) {
    for i in 0..2 {
        let key = m.get(4 + i * 2);
        if key.text.len > 0 {
            t.header(unsafe { bytes(key) }, m.get(5 + i * 2));
        }
    }
}
fn auth(t: &Transport, m: &Model) {
    t.header(b"Accept", string(b"application/vnd.koreader.v1+json"));
    t.header(b"x-auth-user", m.get(field::USERNAME));
    t.header(b"x-auth-key", m.get(field::MD5));
    t.header(b"Authorization", m.get(field::BASIC));
}
fn write_progress(j: &Json, m: &Model, t: &Transport) {
    j.set(0, b"document\0", m.get(field::DOCUMENT));
    if m.get(field::HAS_METADATA).number != 0 {
        j.select(0, b"metadata\0", 0, 1, 1);
        for (key, field) in [
            (b"filename\0".as_slice(), field::FILENAME),
            (b"title\0", field::TITLE),
            (b"authors\0", field::AUTHORS),
        ] {
            j.set(1, key, m.get(field));
        }
        if m.get(field::SERVER_TYPE).number == 2 {
            for (key, field) in [
                (b"isbn\0".as_slice(), field::ISBN),
                (b"asin\0", field::ASIN),
                (b"series\0", field::SERIES),
            ] {
                let v = m.get(field);
                if v.text.len > 0 {
                    j.set(1, key, v);
                }
            }
            let v = m.get(field::SERIES_INDEX);
            if v.valid != 0 {
                j.set(1, b"series_index\0", v);
            }
        }
        let extra = m.get(field::EXTRA_JSON);
        if extra.text.len != 0 && t.call(Op::ParseExtra, &[], extra).number != 0 {
            let mut i = 0;
            loop {
                let key = unsafe { (j.key)(j.ctx, 3, i) };
                if key.is_null() {
                    break;
                }
                let v = unsafe { (j.get)(j.ctx, 3, key, Read::Raw as u32) };
                if v.valid != 0 && unsafe { (j.get)(j.ctx, 1, key, Read::Null as u32) }.number != 0
                {
                    unsafe {
                        (j.set)(j.ctx, 1, key, v);
                    }
                }
                i += 1;
            }
        }
        t.simple(Op::ReleaseExtra);
    }
    j.set(0, b"progress\0", m.get(field::PROGRESS));
    j.set(0, b"percentage\0", m.get(field::PERCENTAGE));
    j.set(0, b"device\0", string(b"CrossPoint"));
    j.set(0, b"device_id\0", string(b"crosspoint-reader"));
    if m.get(field::HAS_POSITION).number != 0 && m.get(field::SERVER_TYPE).number != 1 {
        j.select(0, b"position\0", 0, 2, 1);
        for (key, field) in [
            (b"pctQ\0".as_slice(), field::PCT_Q),
            (b"spine\0", field::SPINE),
            (b"page\0", field::PAGE),
            (b"pages\0", field::PAGES),
        ] {
            j.set(2, key, m.get(field));
        }
        let para = m.get(field::PARAGRAPH);
        if para.valid != 0 {
            j.set(2, b"para\0", para);
        }
        let path = m.get(field::XPATH);
        if path.text.len > 0 && path.text.len <= 120 {
            j.set(2, b"xpath\0", path);
        }
    }
}
fn read_progress(j: &Json, m: &Model, t: &Transport) {
    m.set(field::DOCUMENT, m.get(field::DOCUMENT));
    for (key, field, mode) in [
        (b"progress\0".as_slice(), field::PROGRESS, Read::String),
        (b"percentage\0", field::PERCENTAGE, Read::F32),
        (b"device\0", field::DEVICE, Read::String),
        (b"device_id\0", field::DEVICE_ID, Read::String),
        (b"timestamp\0", field::TIMESTAMP, Read::I64),
    ] {
        m.set(field, j.get(0, key, mode));
    }
    m.set(field::HAS_POSITION, Value::num(Type::Bool, 0));
    if m.get(field::SERVER_TYPE).number != 1 && j.get(0, b"position\0", Read::Object).number != 0 {
        m.set(field::HAS_POSITION, Value::num(Type::Bool, 1));
        j.select(0, b"position\0", 0, 2, 0);
        for (key, field, mode) in [
            (b"pctQ\0".as_slice(), field::PCT_Q, Read::U32),
            (b"spine\0", field::SPINE, Read::U16),
            (b"page\0", field::PAGE, Read::U16),
        ] {
            m.set(field, j.get(2, key, mode));
        }
        let mut pages = j.get(2, b"pages\0", Read::U16);
        pages.number = pages.number.max(1);
        m.set(field::PAGES, pages);
        let para = j.get(2, b"para\0", Read::U16);
        if para.number > 0 {
            m.set(field::PARAGRAPH, para);
        }
        m.set(field::XPATH, j.get(2, b"xpath\0", Read::CString));
        t.log(12, para.number as i32);
    }
    t.log(13, 0);
}
fn status(method: u32, code: i32) -> u32 {
    if code <= 0 {
        return 2;
    }
    if method == 2 && code == 204 {
        return 6;
    }
    if (200..300).contains(&code) {
        return 0;
    }
    if method == 1 {
        return if code == 402 { 8 } else { 4 };
    }
    if code == 401 {
        return 3;
    }
    if method == 2 && code == 404 {
        return 6;
    }
    4
}
pub fn run(method: u32, j: &Json, m: &Model, t: &Transport) -> u32 {
    t.call(Op::Status, &[], Value::num(Type::I64, 0));
    if m.get(field::USERNAME).text.len == 0 || m.get(field::PASSWORD).text.len == 0 {
        t.log(0, 0);
        return 1;
    }
    t.simple(Op::UrlReset);
    let endpoint: &[u8] = match method {
        0 => b"/users/auth",
        1 => b"/users/create",
        2 => b"/syncs/progress/",
        _ => b"/syncs/progress",
    };
    t.call(Op::UrlAppend, endpoint, string(&[]));
    if method == 2 {
        t.call(
            Op::UrlAppend,
            unsafe { bytes(m.get(field::DOCUMENT)) },
            string(&[]),
        );
        t.call(
            Op::UrlAppend,
            b"?position_kinds=locator,percentage",
            string(&[]),
        );
    }
    t.log(1 + method, 0);
    let heap = t.simple(Op::Heap);
    if heap.number < 35000 || heap.real < 20000.0 {
        t.log(5, 0);
        return 7;
    }
    if method == 1 || method == 3 {
        t.simple(Op::JsonReset);
        if method == 1 {
            j.set(0, b"username\0", m.get(field::USERNAME));
            j.set(0, b"password\0", m.get(field::MD5));
        } else {
            write_progress(j, m, t);
        }
        t.simple(Op::Serialize);
        if method == 3 {
            t.log(14, 0);
        }
    }
    t.simple(Op::Insecure);
    if t.simple(Op::Begin).number == 0 {
        t.log(6, 0);
        return 2;
    }
    custom(t, m);
    if method == 1 {
        t.header(b"Accept", string(b"application/vnd.koreader.v1+json"));
    } else {
        auth(t, m);
    }
    if method == 1 || method == 3 {
        t.header(b"Content-Type", string(b"application/json"));
    }
    let code = t
        .simple(match method {
            1 => Op::Post,
            3 => Op::Put,
            _ => Op::Get,
        })
        .number as i32;
    if method != 2 {
        t.simple(Op::End);
    }
    t.call(Op::Status, &[], Value::num(Type::I64, code as i64));
    t.log(
        match method {
            0 => 7,
            1 => 8,
            2 => 9,
            _ => 15,
        },
        code,
    );
    let result = status(method, code);
    if method != 2 {
        return result;
    }
    if result != 0 {
        t.simple(Op::End);
        return result;
    }
    t.simple(Op::JsonReset);
    let parsed = t.simple(Op::ParseResponse).number != 0;
    t.simple(Op::End);
    if !parsed {
        t.log(11, 0);
        return 5;
    }
    read_progress(j, m, t);
    0
}
pub fn error_string(error: u32) -> &'static [u8] {
    match error {
        0 => b"Success\0",
        1 => b"No credentials configured\0",
        2 => b"Network error\0",
        3 => b"Authentication failed\0",
        4 => b"Server error (try again later)\0",
        5 => b"JSON parse error\0",
        6 => b"No progress found\0",
        7 => "Not enough memory for sync — please retry\0".as_bytes(),
        _ => b"Unknown error\0",
    }
}
