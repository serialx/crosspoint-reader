#![allow(dead_code)]
#[path = "../frozen/raw.rs"]
mod raw;
#[path = "../frozen/utf8-next.rs"]
mod utf8;
use raw::Glyph;
#[path = "../frozen/vector.rs"]
mod legacy;
#[no_mangle]
unsafe extern "C" fn vector_oracle_glyph(io: *const legacy::Services, cp: u32) -> *const Glyph {
    legacy::glyph(&*io, cp)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_kern(io: *const legacy::Services, l: u32, r: u32) -> i8 {
    legacy::kern(&*io, l, r)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_init(io: *const legacy::Services, s: *const legacy::Source) {
    legacy::init(&*io, s)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_load(
    f: *const legacy::Family,
    p: u16,
    t: bool,
    c: usize,
    m: u16,
) -> bool {
    legacy::load(&*f, p, t, c, m)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_clear(f: *const legacy::Family, r: bool) {
    legacy::clear(&*f, r)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_prewarm(
    f: *const legacy::Family,
    t: *const u8,
    n: usize,
    r: bool,
) -> bool {
    legacy::prewarm(
        &*f,
        if t.is_null() {
            None
        } else {
            Some(core::slice::from_raw_parts(t, n))
        },
        r,
    )
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_coverage(io: *const legacy::Services, cp: u32) -> bool {
    legacy::coverage(&*io, cp)
}
#[no_mangle]
unsafe extern "C" fn vector_oracle_metrics(io: *const legacy::Services, d: *mut raw::FontData) {
    legacy::metrics(&*io, d)
}
