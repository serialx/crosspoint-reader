#![allow(dead_code)]
use core::ffi::c_void;
#[path = "../../../rust/crates/xp-ffi/src/settings.rs"]
mod settings;
#[no_mangle]
pub unsafe extern "C" fn xp_crc32(bytes: *const u8, len: usize) -> u32 {
    let _ = (bytes, len);
    panic!("recents/state must not call credential CRC")
}
mod app_abi {
    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct Text {
        pub data: *const u8,
        pub len: usize,
    }
    impl Text {
        pub unsafe fn bytes<'a>(self) -> Option<&'a [u8]> {
            if self.len == 0 {
                Some(&[])
            } else if self.data.is_null() {
                None
            } else {
                Some(unsafe { core::slice::from_raw_parts(self.data, self.len) })
            }
        }
    }
    #[repr(C)]
    pub struct Book {
        pub path: Text,
        pub title: Text,
        pub author: Text,
        pub cover: Text,
    }
    #[repr(C)]
    pub struct Resources {
        pub path_exists: unsafe extern "C" fn(Text) -> u8,
    }
}
#[derive(Default)]
struct Text(Vec<u8>);
impl Text {
    fn assign_parts(&mut self, parts: &[&[u8]]) -> Result<(), std::collections::TryReserveError> {
        let size = parts.iter().map(|p| p.len()).sum::<usize>() + 1;
        self.0
            .try_reserve_exact(size.saturating_sub(self.0.len()))?;
        self.0.clear();
        for part in parts {
            self.0.extend_from_slice(part);
        }
        self.0.push(0);
        Ok(())
    }
    fn wire(&self) -> app_abi::Text {
        app_abi::Text {
            data: self.0.as_ptr(),
            len: self.0.len().saturating_sub(1),
        }
    }
}
mod abi {
    use super::*;
    #[repr(C)]
    pub struct XpEpubLeaves {
        pub store_rows: unsafe extern "C" fn(*mut c_void, *mut settings::Rows),
        pub store_rows_close: unsafe extern "C" fn(*mut c_void),
        pub store_move_row: unsafe extern "C" fn(usize, usize),
        pub store_document: unsafe extern "C" fn(
            u8,
            *mut c_void,
            *mut settings::JsonOps,
            *mut settings::PersistOps,
            *mut settings::State,
        ),
        pub log: unsafe extern "C" fn(u8, app_abi::Text, app_abi::Text),
    }
}
mod book_owner {
    pub mod log {
        pub fn with_tail(
            leaves: &super::super::abi::XpEpubLeaves,
            severity: u8,
            origin: &[u8],
            args: core::fmt::Arguments<'_>,
            tail: &[&[u8]],
        ) {
            let mut text = args.to_string().into_bytes();
            for part in tail {
                text.extend_from_slice(part);
            }
            unsafe {
                (leaves.log)(
                    severity,
                    crate::app_abi::Text {
                        data: origin.as_ptr(),
                        len: origin.len(),
                    },
                    crate::app_abi::Text {
                        data: text.as_ptr(),
                        len: text.len(),
                    },
                )
            };
        }
    }
}
#[path = "../../../rust/crates/xp-ffi/src/app/epub/stores.rs"]
mod stores;
#[no_mangle]
pub unsafe extern "C" fn r3_store_call(
    leaves: *const abi::XpEpubLeaves,
    resources: *const app_abi::Resources,
    operation: u8,
    args: *const app_abi::Book,
) -> bool {
    let leaves = unsafe { &*leaves };
    match operation {
        0 => unsafe {
            stores::save_state(leaves);
            true
        },
        1 => unsafe {
            stores::add(leaves, &*resources, &*args);
            true
        },
        2 => unsafe { stores::remove(leaves, (*args).path) },
        3 => unsafe {
            stores::update_path(
                leaves,
                (*args).path,
                (*args).title,
                (*args).author,
                (*args).cover,
            );
            true
        },
        _ => panic!("unknown operation"),
    }
}
