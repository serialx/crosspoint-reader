// Compile the production application bridge with the host allocator and test runtime.
#[path = "../../../rust/crates/xp-ffi/src/app/mod.rs"]
pub mod app;
#[path = "../../../rust/crates/xp-ffi/src/ble.rs"]
mod ble;
#[path = "../../../rust/crates/xp-ffi/src/ble_runtime.rs"]
mod ble_runtime;
#[path = "../../../rust/crates/xp-ffi/src/book_policy.rs"]
mod book_policy;
