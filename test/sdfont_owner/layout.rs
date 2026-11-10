#![no_std]
pub use xp_core::sdfont::{catalog, control, io, owner, parser};
pub use xp_core::{font, sdfont};
const _: () = {
    if cfg!(target_pointer_width = "32") {
        assert!(core::mem::size_of::<parser::IntervalRegistry>() == 192);
        assert!(core::mem::size_of::<io::Slot>() == 12);
        assert!(core::mem::align_of::<io::Slot>() == 4);
    }
};
