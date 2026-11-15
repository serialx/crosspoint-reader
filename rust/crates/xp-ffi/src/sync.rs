extern crate alloc;
use alloc::alloc::{alloc, dealloc};
use core::{alloc::Layout, ffi::c_void, ptr};
use xp_core::sync::{
    document,
    mapping::{self, Position, Rich, SaveServices, Services, Span},
    reverse::Reverse,
    xpath::{Mode, XPath},
};
unsafe fn bytes<'a>(s: Span) -> &'a [u8] {
    if s.len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(s.data, s.len)
    }
}
/// # Safety
/// All spans have `len` readable bytes. Returned spans borrow their inputs.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_filename(path: Span) -> Span {
    Span::new(document::filename(bytes(path)))
}
/// # Safety
/// The callback accepts ctx and returns the number of bytes hashed for each sample.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_samples(
    ctx: *mut c_void,
    size: usize,
    read: unsafe extern "C" fn(*mut c_void, usize, usize) -> usize,
) -> usize {
    document::samples(ctx, size, read)
}
/// # Safety
/// The two position pointers are readable for this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_compare(
    l: *const Position,
    lp: f32,
    r: *const Position,
    rp: f32,
) -> u8 {
    mapping::compare(&*l, lp, &*r, rp)
}
/// # Safety
/// Services and spans are valid for this synchronous call; out is writable.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_map(
    s: *const Services,
    path: Span,
    pct: f32,
    current: i32,
    pages: i32,
    fallback: i32,
    out: *mut Position,
) {
    out.write(mapping::map(
        &*s,
        bytes(path),
        pct,
        current,
        pages,
        fallback,
    ));
}
/// # Safety
/// Same contract as xp_sync_map. Returns zero without writing out on missing layout.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_rich(
    s: *const Services,
    r: *const Rich,
    path: Span,
    tried: u8,
    out: *mut Position,
) -> u8 {
    match mapping::rich(&*s, &*r, bytes(path), tried != 0) {
        Some(p) => {
            out.write(p);
            1
        }
        None => 0,
    }
}
/// # Safety
/// Services and position are valid for this call. Callbacks do not retain borrowed spans.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_save(s: *const SaveServices, p: *const Position) -> f32 {
    mapping::save(&*s, &*p)
}
/// # Safety
/// The reverse pointer is exclusively borrowed during its stream callback.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_reverse_feed(
    r: *mut Reverse,
    data: Span,
    entity: unsafe extern "C" fn(*const u8, usize) -> i32,
) {
    (*r).feed(bytes(data), Some(entity));
}
/// # Safety
/// out is writable. The returned handle has one owner and must be destroyed once.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_xpath_create(
    mode: u32,
    spine: i32,
    target: usize,
    out: *mut *mut XPath,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    out.write(ptr::null_mut());
    let mode = match mode {
        0 => Mode::Paragraph(target as u16),
        1 => Mode::Offset {
            target,
            inclusive: false,
        },
        2 => Mode::Offset {
            target,
            inclusive: true,
        },
        3 => Mode::Count,
        _ => return -1,
    };
    let p = alloc(Layout::new::<XPath>()).cast::<XPath>();
    if p.is_null() {
        return -2;
    }
    p.write(XPath::new(mode, spine));
    out.write(p);
    0
}
/// # Safety
/// Handle is live and exclusively borrowed, data is readable for its length.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_xpath_feed(p: *mut XPath, data: Span, final_input: u8) -> u32 {
    (*p).feed(bytes(data), final_input != 0)
}
/// # Safety
/// Handle is live. The output span expires at the next feed or destroy call.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_xpath_output(p: *const XPath) -> Span {
    Span::new((*p).output())
}
/// # Safety
/// Handle is live.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_xpath_count(p: *const XPath) -> usize {
    (*p).count()
}
/// # Safety
/// Handle is null or the sole live owner; no borrows survive destruction.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_xpath_destroy(p: *mut XPath) {
    if !p.is_null() {
        ptr::drop_in_place(p);
        dealloc(p.cast(), Layout::new::<XPath>());
    }
}
#[no_mangle]
pub extern "C" fn xp_sync_xpath_target(total: usize, progress: f32) -> usize {
    let product = progress.clamp(0.0, 1.0) * total as f32;
    let n = product as usize;
    let ceiling = if (n as f32) < product {
        n.saturating_add(1)
    } else {
        n
    };
    ceiling.min(total).max(1)
}
use xp_core::sync::{
    credentials,
    json::{Json, Model},
};
/// # Safety
/// Span is readable; prefix is writable. Returned span borrows input or static storage.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_base_url(input: Span, prefix: *mut u8) -> Span {
    let (url, http) = credentials::base_url(bytes(input));
    prefix.write(u8::from(http));
    Span::new(url)
}
#[no_mangle]
pub extern "C" fn xp_sync_server_type(v: u8) -> u8 {
    credentials::server_type(v)
}
#[no_mangle]
pub extern "C" fn xp_sync_behavior(v: u8) -> u8 {
    credentials::behavior(v)
}
/// # Safety
/// Both descriptors and their callbacks are valid for this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_store_save(j: *const Json, m: *const Model) {
    credentials::save(&*j, &*m);
}
/// # Safety
/// Both descriptors and their callbacks are valid; diagnostics do not reenter Rust.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_store_load(
    j: *const Json,
    m: *const Model,
    resave: u8,
    diag: credentials::Diagnostic,
) -> u8 {
    u8::from(credentials::load(&*j, &*m, resave != 0, diag))
}
use xp_core::sync::protocol::{self, Transport};
/// # Safety
/// Descriptors and callbacks remain live for the synchronous request. No callback
/// may retain borrowed spans. Only defined model fields are requested by Rust.
#[no_mangle]
pub unsafe extern "C" fn xp_sync_request(
    method: u32,
    j: *const Json,
    m: *const Model,
    t: *const Transport,
) -> u32 {
    protocol::run(method, &*j, &*m, &*t)
}
#[no_mangle]
pub extern "C" fn xp_sync_error_string(error: u32) -> *const core::ffi::c_char {
    protocol::error_string(error).as_ptr().cast()
}
