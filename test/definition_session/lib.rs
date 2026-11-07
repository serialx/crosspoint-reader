#![allow(dead_code)]
mod epub {
    pub mod abi {
        pub enum XpEpubLeaves {}
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/abi.rs"]
mod abi;
mod app {
    pub(crate) use super::abi;
}
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/definition_text.rs"]
mod definition_text;
#[cfg(test)]
mod text_tests;
