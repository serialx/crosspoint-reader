extern crate alloc;

use super::super::{abi, app_abi, read_folder, EpubSession, Text};
use alloc::boxed::Box;
use core::ffi::c_void;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_read_folder_create(
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
        .map_or(core::ptr::null_mut(), |owner| Box::into_raw(owner).cast())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_read_folder_destroy(owner: *mut c_void) {
    drop(Box::from_raw(owner.cast::<EpubSession>()));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_read_folder_classify(path: app_abi::Text) -> u8 {
    path.bytes()
        .map_or(0, |bytes| u8::from(read_folder::is_in_read_folder(bytes)))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_read_folder_destination(
    owner: *mut c_void,
    source: app_abi::Text,
    sink: app_abi::TextSink,
    context: *mut c_void,
) -> u8 {
    let Some(source) = source.bytes().and_then(|bytes| Text::copy(bytes).ok()) else {
        return 0;
    };
    let Some(destination) = (&*owner.cast::<EpubSession>()).read_folder_destination(&source) else {
        return 0;
    };
    sink(context, destination.wire())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_read_folder_move(
    owner: *mut c_void,
    source: app_abi::Text,
    destination: app_abi::Text,
    old_cache: app_abi::Text,
) -> u8 {
    let Some(source) = source.bytes().and_then(|bytes| Text::copy(bytes).ok()) else {
        return 0;
    };
    let Some(destination) = destination.bytes().and_then(|bytes| Text::copy(bytes).ok()) else {
        return 0;
    };
    let Some(old_cache) = old_cache.bytes().and_then(|bytes| Text::copy(bytes).ok()) else {
        return 0;
    };
    (&*owner.cast::<EpubSession>()).move_to_read_folder(&source, &destination, &old_cache);
    1
}
