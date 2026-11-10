#![no_std]
pub use xp_core::{font, sdfont};
#[path = "../../rust/crates/xp-core/src/sdfont/io.rs"]
pub mod io;
#[path = "../../rust/crates/xp-core/src/sdfont/owner.rs"]
pub mod owner;
#[path = "../../rust/crates/xp-core/src/sdfont/parser.rs"]
pub mod parser;
const _: () = {
    if cfg!(target_pointer_width = "32") {
        assert!(core::mem::size_of::<parser::IntervalRegistry>() == 192);
        assert!(core::mem::size_of::<io::Slot>() == 12);
        assert!(core::mem::align_of::<io::Slot>() == 4);
    }
};

#[path = "../../rust/crates/xp-core/src/sdfont/catalog.rs"]
pub mod catalog;
#[path = "../../rust/crates/xp-core/src/sdfont/catalog_storage.rs"]
mod catalog_storage;

#[path = "../../rust/crates/xp-core/src/sdfont/control.rs"]
pub mod control;
