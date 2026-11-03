extern crate alloc;

use super::super::{File, Text};
use super::{InflateStorage, XpEpubLeaves};
use alloc::vec::Vec;
use core::ffi::c_void;

type Inspect = unsafe extern "C" fn(*mut c_void, *const u8, *const u8, usize, u8) -> u8;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_scratch(
    resources: &'static crate::app::abi::Resources,
    leaves: &'static XpEpubLeaves,
    kind: u8,
    inspect: Inspect,
    context: *mut c_void,
) -> u8 {
    let Some(mut storage) = InflateStorage::new(resources, leaves, kind != 5) else {
        return 1;
    };
    assert!(storage.history().iter().all(|&byte| byte == 0));
    let history = storage.history();
    if unsafe {
        inspect(
            context,
            storage.state.as_ptr().cast(),
            history.as_ptr(),
            history.len(),
            u8::from(storage.lease.is_some()),
        )
    } == 0
    {
        return 3;
    }
    match kind {
        1 => {
            let (status, _, _) = storage.step(&[255], false, 0);
            assert!(status < 0);
            2
        }
        2 => {
            let Ok(path) = Text::copy(b"source") else {
                return 1;
            };
            let Some(file) = File::open(leaves, &path, c"ZIP") else {
                return 2;
            };
            let mut bytes = [0; 4];
            if !file.read_at(0, &mut bytes) {
                return 2;
            }
            0
        }
        4 => {
            let mut bytes = Vec::<u8>::new();
            if bytes.try_reserve_exact(64).is_err() {
                return 1;
            }
            0
        }
        5 => 0,
        _ => {
            assert_eq!(storage.step(&[3, 0], false, 0), (1, 2, 0));
            0
        }
    }
}
