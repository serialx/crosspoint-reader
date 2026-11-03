extern crate alloc;

use super::super::{
    abi::XpEpubLeaves,
    app_abi,
    book_owner::{CacheGuard, File, OpenError, Protection},
    Text,
};
use alloc::vec::Vec;
use core::ffi::c_void;
use xp_core::book::io::Sink;

// Physical leaves are supplied by the native composition fixture. No owner hook
// or substitute file/protection policy is installed in the production owner.
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_book_resources(
    leaves: &'static XpEpubLeaves,
    path: app_abi::Text,
    kind: u8,
    sink: Sink,
    context: *mut c_void,
) -> u32 {
    let Some(path) = (unsafe { path.bytes() }) else {
        return 20;
    };
    let Ok(path) = Text::copy(path) else { return 3 };
    match kind {
        0 => {
            let Some(file) = File::open(leaves, &path) else {
                return 4;
            };
            if file.size() != 8 {
                return 5;
            }
            let mut bytes = [0; 4];
            if !file.read_at(1, &mut bytes) {
                return 6;
            }
            if unsafe { sink(context, bytes.as_ptr(), bytes.len()) } == 0 {
                return 7;
            }
            1
        }
        1 => {
            let protection = match Protection::open(leaves, &path) {
                Ok(Protection::Plain) => return 0,
                Ok(value) => value,
                Err(OpenError::OutOfMemory) => return 3,
                Err(OpenError::Message(error)) => {
                    unsafe {
                        sink(context, error.bytes().as_ptr(), error.bytes().len());
                    }
                    return 2;
                }
            };
            let href = b"OPS/\xffchapter.xhtml";
            if !protection.is_encrypted(href) || protection.size(href) != Some(4) {
                return 9;
            }
            if !unsafe { protection.stream(href, sink, context) } {
                return 8;
            }
            1
        }
        2 => {
            let _guard = CacheGuard::acquire(leaves);
            let mut bytes = Vec::<u8>::new();
            if bytes.try_reserve_exact(64).is_err() {
                return 3;
            }
            1
        }
        3 => u32::from(unsafe { (leaves.path_remove_tree)(path.wire()) } != 0),
        _ => 20,
    }
}
