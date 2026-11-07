extern crate alloc;
use super::{definition_owned::DefinitionOwner, selection_physical::LookupStorage};
use crate::app::{abi, epub::abi::XpEpubLeaves};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr};
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_definition_owner_new(
    runtime: &'static abi::Runtime,
    leaves: &'static XpEpubLeaves,
    lookup: *mut LookupStorage,
) -> *mut c_void {
    unsafe {
        DefinitionOwner::try_lookup(runtime, &leaves.child_selection_physical.definition, lookup)
    }
    .map_or(ptr::null_mut(), |owner| {
        Box::into_raw(Box::new(owner)).cast()
    })
}
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_definition_owner_drop(owner: *mut c_void) {
    unsafe { drop(Box::from_raw(owner.cast::<DefinitionOwner>())) };
}
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_definition_owner_result(
    owner: *mut c_void,
    runtime: &abi::Runtime,
    leaves: &XpEpubLeaves,
    result: &abi::XpAppResultView,
) {
    unsafe { (&*owner.cast::<DefinitionOwner>()).selection_result(runtime, leaves, result) };
}
