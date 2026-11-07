use crate::app::abi;
use core::ffi::c_void;

#[repr(C)]
pub(crate) struct LookupStorage {
    pub(super) words: [usize; 90],
}
pub(crate) type DefinitionFill = unsafe extern "C" fn(*mut c_void, *mut u8, u32) -> u8;

// Typed native resources and drawing facts; the child owns ordering and policy.
#[repr(C)]
pub(crate) struct Physical {
    pub(super) lookup_init: unsafe extern "C" fn(*mut LookupStorage, abi::Text),
    pub(super) lookup_drop: unsafe extern "C" fn(*mut LookupStorage),
    pub(super) lookup_open: unsafe extern "C" fn(*mut LookupStorage, *mut u8) -> u8,
    pub(super) lookup_index: unsafe extern "C" fn(*mut LookupStorage, *mut u8) -> u8,
    pub(super) lookup_find: unsafe extern "C" fn(*mut LookupStorage, *mut u8) -> u8,
    pub(super) lookup_discard: unsafe extern "C" fn(*mut LookupStorage),
    pub(crate) definition: super::definition_physical::DefinitionPhysical,
    pub(super) directional_hints: unsafe extern "C" fn(*const abi::Text),
    pub(super) dictionary_popup_layout: unsafe extern "C" fn(*mut xp_ui::dictionary::popup::Layout),
    pub(super) wait_paint: unsafe extern "C" fn(),
}
const _: () = {
    assert!(core::mem::size_of::<Physical>() == 24 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<LookupStorage>() == 90 * core::mem::size_of::<usize>());
};
