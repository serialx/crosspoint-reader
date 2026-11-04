// Compile the production application bridge with the host allocator and test runtime.
#[path = "../../../rust/crates/xp-ffi/src/app/mod.rs"]
pub mod app;
#[path = "../../../rust/crates/xp-ffi/src/ble.rs"]
mod ble;
#[path = "../../../rust/crates/xp-ffi/src/ble_runtime.rs"]
mod ble_runtime;
#[path = "../../../rust/crates/xp-ffi/src/book_css.rs"]
mod book_css;
#[path = "../../../rust/crates/xp-ffi/src/book_limits.rs"]
mod book_limits;
#[path = "../../../rust/crates/xp-ffi/src/book_policy.rs"]
mod book_policy;
#[path = "../../../rust/crates/xp-ffi/src/chapter.rs"]
mod chapter;
#[path = "../../../rust/crates/xp-ffi/src/css.rs"]
mod css;
#[path = "../../../rust/crates/xp-ffi/src/section_cache.rs"]
mod section_cache;
#[path = "../../../rust/crates/xp-ffi/src/settings.rs"]
mod settings;

pub unsafe extern "C" fn xp_crc32(data: *const u8, len: usize) -> u32 {
    let bytes = if len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, len)
    };
    xp_core::crc32(bytes)
}
