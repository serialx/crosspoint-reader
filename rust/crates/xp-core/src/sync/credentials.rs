use super::json::store_field as field;
use super::json::{Json, Model, Read, Type, Value};
use core::ffi::c_void;
pub const DEFAULT: &[u8] = b"https://sync.crosspointreader.com";
pub const LEGACY: &[u8] = b"https://sync.koreader.rocks:443";
pub fn base_url(url: &[u8]) -> (&[u8], bool) {
    let url = if url.is_empty() { DEFAULT } else { url };
    let prefix = !url.windows(3).any(|w| w == b"://");
    let end = url.iter().rposition(|&b| b != b'/').map_or(0, |n| n + 1);
    if prefix && end == 0 {
        (b"http:", false)
    } else {
        (&url[..end], prefix)
    }
}
pub fn server_type(v: u8) -> u8 {
    if v <= 2 {
        v
    } else {
        1
    }
}
pub fn behavior(v: u8) -> u8 {
    if v <= 1 {
        v
    } else {
        0
    }
}
const FIELDS: [(&[u8], u32); 8] = [
    (b"username\0", field::USERNAME),
    (b"password_obf\0", field::PASSWORD),
    (b"serverUrl\0", field::SERVER_URL),
    (b"serverType\0", field::SERVER_TYPE),
    (b"matchMethod\0", field::MATCH_METHOD),
    (b"sendMetadata\0", field::SEND_METADATA),
    (b"syncClippings\0", field::SYNC_CLIPPINGS),
    (b"syncBehavior\0", field::BEHAVIOR),
];
pub fn save(j: &Json, m: &Model) {
    j.set(0, b"cfgVersion\0", Value::num(Type::U8, 3));
    for &(key, field) in &FIELDS {
        let mut v = m.get(field);
        if field == field::PASSWORD {
            v.kind = Type::Secret as u32;
        }
        j.set(0, key, v);
    }
    j.select(0, b"headers\0", 0, 1, 2);
    for index in 0..2 {
        j.select(1, &[], index, 2, 1);
        j.set(2, b"name\0", m.get(8 + index as u32 * 2));
        let mut v = m.get(9 + index as u32 * 2);
        v.kind = Type::Secret as u32;
        j.set(2, b"value_obf\0", v);
    }
}
pub type Diagnostic = Option<unsafe extern "C" fn(*mut c_void, u32, u32)>;
fn log(m: &Model, diag: Diagnostic, event: u32, value: u32) {
    if let Some(f) = diag {
        unsafe {
            f(m.ctx, event, value);
        }
    }
}
/// Password extraction and header deobfuscation use the settings store's existing leaves.
pub fn load(j: &Json, m: &Model, mut resave: bool, diag: Diagnostic) -> bool {
    m.set(field::USERNAME, j.get(0, b"username\0", Read::CString));
    m.set(field::PASSWORD, m.get(field::EXTRACTED_PASSWORD));
    m.set(field::SERVER_URL, j.get(0, b"serverUrl\0", Read::CString));
    let cfg = j.get(0, b"cfgVersion\0", Read::U8Default);
    let cfg = if cfg.valid != 0 { cfg.number as u8 } else { 1 };
    if cfg < 2 {
        if m.get(field::SERVER_URL).text.len == 0
            && m.get(field::USERNAME).text.len != 0
            && m.get(field::PASSWORD).text.len != 0
        {
            log(m, diag, 1, 0);
            m.set(field::SERVER_URL, Value::string(LEGACY));
        }
        resave = true;
    }
    if cfg < 3 || j.get(0, b"serverType\0", Read::Null).number != 0 {
        let base = m.get(field::BASE_URL);
        let is_default = base.text.len == DEFAULT.len()
            && unsafe { core::slice::from_raw_parts(base.text.data, base.text.len) } == DEFAULT;
        m.set(
            field::SERVER_TYPE,
            Value::num(Type::U8, if is_default { 0 } else { 1 }),
        );
        resave = true;
    } else {
        let n = j.get(0, b"serverType\0", Read::U8).number as u8;
        if n > 2 {
            log(m, diag, 2, n as u32);
            resave = true;
        }
        m.set(
            field::SERVER_TYPE,
            Value::num(Type::U8, server_type(n) as i64),
        );
    }
    let method = j.get(0, b"matchMethod\0", Read::U8Default);
    let n = if method.valid != 0 {
        method.number as u8
    } else {
        0
    };
    if n > 1 {
        log(m, diag, 3, n as u32);
    }
    m.set(
        field::MATCH_METHOD,
        Value::num(Type::U8, if n <= 1 { n as i64 } else { 0 }),
    );
    m.set(
        field::SEND_METADATA,
        j.get(0, b"sendMetadata\0", Read::Bool),
    );
    m.set(
        field::SYNC_CLIPPINGS,
        j.get(0, b"syncClippings\0", Read::Bool),
    );
    j.select(0, b"headers\0", 0, 1, 0);
    for i in 0..j.count(1).min(2) {
        j.select(1, &[], i, 2, 0);
        m.set(8 + i as u32 * 2, j.get(2, b"name\0", Read::CString));
        let mut v = j.get(2, b"value_obf\0", Read::CString);
        v.kind = Type::Secret as u32;
        m.set(9 + i as u32 * 2, v);
    }
    let missing = j.get(0, b"syncBehavior\0", Read::Null).number != 0;
    let v = j.get(0, b"syncBehavior\0", Read::U8Default);
    let n = if v.valid != 0 { v.number as u8 } else { 0 };
    if n <= 1 {
        resave |= missing;
    } else {
        log(m, diag, 4, n as u32);
        resave = true;
    }
    m.set(field::BEHAVIOR, Value::num(Type::U8, behavior(n) as i64));
    resave
}
