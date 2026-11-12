// The fixture's stand-in for the production `app` module. The mounted App files
// resolve `super::` siblings and `crate::app::*` here as they do in production.

#[path = "../../rust/crates/xp-ffi/src/app/abi.rs"]
pub(crate) mod abi;
#[path = "../../rust/crates/xp-ffi/src/app/bookmarks/mod.rs"]
pub(crate) mod bookmarks;
#[path = "../../rust/crates/xp-ffi/src/app/clippings/io.rs"]
pub(crate) mod clipping_io;
pub(crate) mod clippings {
    pub(crate) use super::clipping_io as io;
}
pub(crate) mod epub;
#[path = "../../rust/crates/xp-ffi/src/app/resources.rs"]
pub(crate) mod resources;
// Owner fixtures observe commands through their historical paint callback.
pub(crate) mod scene {
    pub(crate) unsafe fn paint(
        services: &super::abi::Resources,
        scene: *const xp_ui::ffi::SceneView,
        resources: *const xp_ui::ffi::Paint,
    ) -> u8 {
        unsafe { (services.paint_scene)(scene, resources) }
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/result.rs"]
pub(crate) mod result;
#[path = "../../rust/crates/xp-ffi/src/app/stable_owner.rs"]
pub(crate) mod stable_owner;
#[path = "../../rust/crates/xp-ffi/src/app/sync.rs"]
pub(crate) mod sync;
#[cfg(test)]
#[path = "../../rust/crates/xp-ffi/src/app/test_resources.rs"]
pub(crate) mod test_resources;

// Complete the shared test resource table's catalog bindings; children must never call them.
pub(crate) mod home_books {
    pub mod tests {
        use super::super::abi::{BookSink, Text};
        use core::ffi::c_void;
        pub unsafe extern "C" fn recent_missing(_: Text) -> u8 {
            panic!("catalog access")
        }
        pub unsafe extern "C" fn library_open() -> *mut c_void {
            panic!("catalog access")
        }
        pub unsafe extern "C" fn library_count(_: *mut c_void) -> u32 {
            panic!("catalog access")
        }
        pub unsafe extern "C" fn library_row(
            _: *mut c_void,
            _: u32,
            _: u8,
            _: BookSink,
            _: *mut c_void,
        ) -> u8 {
            panic!("catalog access")
        }
        pub unsafe extern "C" fn library_close(_: *mut c_void) {
            panic!("catalog access")
        }
    }
}
