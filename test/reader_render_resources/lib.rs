#![allow(dead_code)]

// Use the production allocator adapter and its complete ABI. This unused opaque
// leaf type only completes Platform's pointer; no reader service is substituted.
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
mod app {
    pub(crate) use super::{abi, resources};
}
#[path = "../../rust/crates/xp-ffi/src/app/epub/render_resources.rs"]
mod render_resources;

#[cfg(test)]
mod tests;

#[cfg(feature = "reader-strips")]
#[path = "../../rust/crates/xp-ffi/src/display/source.rs"]
mod display;
#[cfg(feature = "reader-strips")]
pub(crate) enum Command {}
#[cfg(feature = "reader-strips")]
#[path = "../../rust/crates/xp-ffi/src/page_plan/storage.rs"]
mod page_render;
