#![allow(dead_code)]

pub mod chapter_trace;

mod epub {
    pub mod abi {
        pub enum XpEpubLeaves {}
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/abi.rs"]
mod abi;
#[path = "../../rust/crates/xp-ffi/src/app/resources.rs"]
mod resources;
#[path = "../../rust/crates/xp-ffi/src/app/result.rs"]
mod result;
#[path = "../../rust/crates/xp-ffi/src/app/test_resources.rs"]
mod test_resources;
mod app {
    pub(crate) use super::{abi, resources, stable_owner, sync};
}
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/commands.rs"]
mod commands;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/owned.rs"]
mod owned;
#[cfg(test)]
mod owner_tests;
#[path = "../../rust/crates/xp-ffi/src/app/sync.rs"]
mod sync;

#[cfg(test)]
mod tests;

// Complete the shared test resource table's catalog bindings; children must never call them.
mod home_books {
    pub mod tests {
        use crate::abi::{BookSink, Text};
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
#[cfg(test)]
use xp_ui::reader::percent;
#[cfg(test)]
mod chapter_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/chapters.rs"]
mod chapters;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/result.rs"]
mod child_result;
#[cfg(test)]
mod lifecycle_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/menu.rs"]
mod menu;
#[cfg(test)]
mod menu_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/percent.rs"]
mod percent_adapter;
#[cfg(test)]
mod percent_adapter_tests;
#[cfg(test)]
mod percent_scene_tests;
#[cfg(test)]
mod percent_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/physical.rs"]
mod physical;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/qr.rs"]
mod qr;
#[cfg(test)]
mod qr_adapter_tests;

#[path = "../../rust/crates/xp-ffi/src/app/epub/children/list.rs"]
mod list;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/menu_view.rs"]
mod menu_view;

#[path = "../../rust/crates/xp-ffi/src/app/epub/children/chapter_view.rs"]
mod chapter_view;

#[cfg(test)]
mod list_adapter_tests;

#[cfg(test)]
mod chapter_scene_tests;

#[cfg(test)]
mod toolbar_adapter_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/toolbar_view.rs"]
mod toolbar_view;

#[cfg(test)]
mod toolbar_scene_tests;

#[path = "../../rust/crates/xp-ffi/src/app/epub/children/input.rs"]
mod input;
#[cfg(test)]
mod input_tests;

#[cfg(test)]
mod menu_scene_tests;

#[path = "../../rust/crates/xp-ffi/src/app/stable_owner.rs"]
mod stable_owner;
