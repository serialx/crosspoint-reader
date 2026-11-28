#![cfg(test)]
#![allow(dead_code)]
use core::ffi::c_void;
#[path = "../../rust/crates/xp-ffi/src/hal/time.rs"]
pub mod time;
mod hal {
    pub use crate::time;
}
mod app_abi {
    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct Text {
        pub data: *const u8,
        pub len: usize,
    }
    impl Text {
        pub fn from_terminated(value: &[u8]) -> Self {
            Self {
                data: value.as_ptr(),
                len: value.len() - 1,
            }
        }
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
}
#[derive(Default)]
struct Text(Vec<u8>);
impl Text {
    fn assign_parts(&mut self, parts: &[&[u8]]) -> Result<(), ()> {
        self.0 = parts.concat();
        self.0.push(0);
        Ok(())
    }
    fn bytes(&self) -> &[u8] {
        &self.0[..self.0.len().saturating_sub(1)]
    }
    fn wire(&self) -> app_abi::Text {
        app_abi::Text {
            data: self.0.as_ptr(),
            len: self.bytes().len(),
        }
    }
}
mod abi {
    use super::*;
    pub struct XpEpubLeaves {
        pub time_services: unsafe extern "C" fn() -> *const crate::time::Services,
        pub time_start_sync: unsafe extern "C" fn() -> crate::time::SyncServices,
        pub file_open_write: unsafe extern "C" fn(app_abi::Text, app_abi::Text) -> *mut c_void,
        pub file_write: unsafe extern "C" fn(*mut c_void, *const u8, u32) -> u32,
        pub file_flush: unsafe extern "C" fn(*mut c_void),
        pub file_close: unsafe extern "C" fn(*mut c_void) -> u8,
        pub path_remove: unsafe extern "C" fn(app_abi::Text) -> u8,
        pub path_rename: unsafe extern "C" fn(app_abi::Text, app_abi::Text) -> u8,
        pub storage_read_file: unsafe extern "C" fn(app_abi::Text, *mut c_void) -> app_abi::Text,
        pub storage_read_release: unsafe extern "C" fn(*mut c_void),
    }
}
mod book_owner {
    pub mod log {
        pub fn with_tail(
            _: &super::super::abi::XpEpubLeaves,
            severity: u8,
            origin: &[u8],
            args: core::fmt::Arguments<'_>,
            tail: &[&[u8]],
        ) {
            assert_eq!((severity, origin), (1, &b"PRG"[..]));
            let mut text = args.to_string().into_bytes();
            for part in tail {
                text.extend_from_slice(part);
            }
            crate::tests::record(format!("error:{}", String::from_utf8(text).unwrap()));
        }
    }
}
#[path = "../../rust/crates/xp-ffi/src/app/epub/persistence.rs"]
mod persistence;
#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    #[derive(Default)]
    struct State {
        events: Vec<String>,
        failure: &'static str,
        saved: Vec<u8>,
        temporary: Vec<u8>,
        open: bool,
        epoch: i64,
        floor: i64,
        persisted: i64,
        ticks: u32,
        ready_at: u32,
    }
    thread_local! { static STATE: RefCell<State> = RefCell::new(State::default()); }
    pub fn record(event: String) {
        STATE.with_borrow_mut(|s| s.events.push(event));
    }
    fn text(value: app_abi::Text) -> String {
        String::from_utf8(unsafe { value.bytes().unwrap() }.to_vec()).unwrap()
    }
    fn path(value: app_abi::Text, expected: &str) {
        assert_eq!(text(value), expected);
    }
    #[repr(C)]
    struct TimeServices {
        now: unsafe extern "C" fn() -> i64,
        read: unsafe extern "C" fn() -> i64,
        write: unsafe extern "C" fn(i64),
        raise: unsafe extern "C" fn(i64) -> i64,
        set: unsafe extern "C" fn(i64),
    }
    #[repr(C)]
    struct SyncServices {
        completed: unsafe extern "C" fn() -> bool,
        millis: unsafe extern "C" fn() -> u32,
        delay: unsafe extern "C" fn(u32),
    }
    unsafe extern "C" fn epoch() -> i64 {
        record("now".into());
        STATE.with_borrow(|s| s.epoch)
    }
    unsafe extern "C" fn read_floor() -> i64 {
        record("read-floor".into());
        STATE.with_borrow(|s| s.persisted)
    }
    unsafe extern "C" fn write_floor(value: i64) {
        record("write-floor".into());
        STATE.with_borrow_mut(|s| s.persisted = value);
    }
    unsafe extern "C" fn raise(value: i64) -> i64 {
        record("raise-floor".into());
        STATE.with_borrow_mut(|s| {
            s.floor = s.floor.max(value);
            s.floor
        })
    }
    unsafe extern "C" fn set(_: i64) {
        panic!("unexpected clock set");
    }
    static TIME: TimeServices = TimeServices {
        now: epoch,
        read: read_floor,
        write: write_floor,
        raise,
        set,
    };
    const _: () = assert!(
        core::mem::size_of::<TimeServices>() == core::mem::size_of::<crate::time::Services>()
    );
    unsafe extern "C" fn time_services() -> *const crate::time::Services {
        (&TIME as *const TimeServices).cast()
    }
    unsafe extern "C" fn completed() -> bool {
        STATE.with_borrow(|s| s.ticks >= s.ready_at)
    }
    unsafe extern "C" fn millis() -> u32 {
        STATE.with_borrow(|s| s.ticks)
    }
    unsafe extern "C" fn delay(ms: u32) {
        assert_eq!(ms, 100);
        record("delay".into());
        STATE.with_borrow_mut(|s| s.ticks += ms);
    }
    unsafe extern "C" fn time_start_sync() -> crate::time::SyncServices {
        record("start-sync".into());
        unsafe {
            core::mem::transmute(SyncServices {
                completed,
                millis,
                delay,
            })
        }
    }
    const LEAVES: abi::XpEpubLeaves = abi::XpEpubLeaves {
        time_services,
        time_start_sync,
        file_open_write: open,
        file_write: write,
        file_flush: flush,
        file_close: close,
        path_remove: remove,
        path_rename: rename,
        storage_read_file: read,
        storage_read_release: release,
    };
    unsafe extern "C" fn open(p: app_abi::Text, origin: app_abi::Text) -> *mut c_void {
        path(p, "/cache/progress.bin.tmp");
        path(origin, "PRG");
        STATE.with_borrow_mut(|s| {
            s.events.push("open".into());
            assert!(!s.open);
            if s.failure == "open" {
                return core::ptr::null_mut();
            }
            s.open = true;
            Box::into_raw(Box::new(0u8)).cast()
        })
    }
    unsafe extern "C" fn write(file: *mut c_void, bytes: *const u8, len: u32) -> u32 {
        assert!(!file.is_null());
        STATE.with_borrow_mut(|s| {
            assert!(s.open);
            s.events.push("write".into());
            let written = if s.failure == "write" { len - 1 } else { len };
            s.temporary = unsafe { core::slice::from_raw_parts(bytes, written as usize) }.to_vec();
            written
        })
    }
    unsafe extern "C" fn flush(_: *mut c_void) {
        STATE.with_borrow_mut(|s| {
            assert!(s.open);
            s.events.push("flush".into());
        });
    }
    unsafe extern "C" fn close(file: *mut c_void) -> u8 {
        STATE.with_borrow_mut(|s| {
            assert!(s.open);
            s.open = false;
            s.events.push("close".into());
        });
        unsafe {
            drop(Box::from_raw(file.cast::<u8>()));
        }
        u8::from(STATE.with_borrow(|s| s.failure != "close"))
    }
    unsafe extern "C" fn remove(p: app_abi::Text) -> u8 {
        path(p, "/cache/progress.bin");
        STATE.with_borrow_mut(|s| {
            assert!(!s.open);
            s.events.push("remove".into());
            if s.failure == "remove" {
                0
            } else {
                s.saved.clear();
                1
            }
        })
    }
    unsafe extern "C" fn rename(from: app_abi::Text, to: app_abi::Text) -> u8 {
        path(from, "/cache/progress.bin.tmp");
        path(to, "/cache/progress.bin");
        STATE.with_borrow_mut(|s| {
            assert!(!s.open);
            s.events.push("rename".into());
            if s.failure == "rename" || s.failure == "remove" {
                0
            } else {
                s.saved = core::mem::take(&mut s.temporary);
                1
            }
        })
    }
    unsafe extern "C" fn read(p: app_abi::Text, storage: *mut c_void) -> app_abi::Text {
        path(p, "/cache");
        let bytes = STATE.with_borrow(|s| s.saved.clone());
        unsafe {
            storage.cast::<Vec<u8>>().write(bytes);
        }
        let bytes = unsafe { &*storage.cast::<Vec<u8>>() };
        record("read".into());
        app_abi::Text {
            data: bytes.as_ptr(),
            len: bytes.len(),
        }
    }
    unsafe extern "C" fn release(storage: *mut c_void) {
        record("release".into());
        unsafe {
            core::ptr::drop_in_place(storage.cast::<Vec<u8>>());
        }
    }
    fn cache() -> Text {
        let mut text = Text::default();
        text.assign_parts(&[b"/cache"]).unwrap();
        text
    }
    #[test]
    fn progress_write_order_and_failures() {
        for failure in ["", "open", "write", "close", "remove", "rename"] {
            STATE.with_borrow_mut(|s| {
                *s = State {
                    saved: b"old".to_vec(),
                    failure,
                    ..Default::default()
                }
            });
            let ok = persistence::save_progress(&LEAVES, &cache(), b"0123456789");
            STATE.with_borrow(|s| {
                let expected: &[&str] = match failure {
                    "open" => &["open", "error:Could not open temp progress file for write: /cache/progress.bin.tmp"],
                    "write" => &["open", "write", "error:Short write saving progress to /cache/progress.bin.tmp: 9/10 bytes", "close"],
                    "remove" | "rename" => &["open", "write", "flush", "close", "remove", "rename", "error:Failed to rename temp progress into place: /cache/progress.bin"],
                    _ => &["open", "write", "flush", "close", "remove", "rename"],
                };
                assert_eq!(s.events, expected, "{failure}"); assert!(!s.open);
                assert_eq!(ok, failure.is_empty() || failure == "close");
                let saved: &[u8] = match failure { "open" | "write" | "remove" => b"old", "rename" => b"", _ => b"0123456789" };
                assert_eq!(s.saved, saved, "{failure}");
            });
        }
    }
    #[test]
    fn storage_borrow_ends_before_release_and_empty_is_not_consumed() {
        for bytes in [&b""[..], &b"{\"bookmarks\":[]}"[..]] {
            STATE.with_borrow_mut(|s| {
                *s = State {
                    saved: bytes.to_vec(),
                    ..Default::default()
                }
            });
            let result = persistence::read_file(&LEAVES, &cache(), |actual| {
                record("consume".into());
                actual.to_vec()
            });
            assert_eq!(result.as_deref(), (!bytes.is_empty()).then_some(bytes));
            STATE.with_borrow(|s| {
                assert_eq!(
                    s.events,
                    if bytes.is_empty() {
                        vec!["read", "release"]
                    } else {
                        vec!["read", "consume", "release"]
                    }
                )
            });
        }
    }
    #[test]
    fn trusted_time_and_sync_preserve_floor_and_save_order() {
        STATE.with_borrow_mut(|s| *s = State::default());
        assert_eq!(unsafe { persistence::now(&LEAVES) }, 0);
        STATE.with_borrow_mut(|s| {
            s.epoch = 1_735_689_700;
            s.persisted = 1_735_689_600;
        });
        assert_eq!(unsafe { persistence::now(&LEAVES) }, 1_735_689_700);
        STATE.with_borrow_mut(|s| s.epoch = 1_735_689_650);
        assert_eq!(unsafe { persistence::now(&LEAVES) }, 1_735_689_700);
        STATE.with_borrow_mut(|s| {
            s.events.clear();
            s.ready_at = 200;
            s.epoch = 1_735_689_750;
        });
        assert!(unsafe { persistence::sync_time(&LEAVES, 5000) });
        STATE.with_borrow(|s| {
            assert_eq!(s.persisted, s.epoch);
            assert_eq!(
                s.events,
                [
                    "start-sync",
                    "delay",
                    "delay",
                    "now",
                    "raise-floor",
                    "read-floor",
                    "write-floor"
                ]
            );
        });
        STATE.with_borrow_mut(|s| {
            s.events.clear();
            s.ticks = 0;
            s.ready_at = 6000;
        });
        assert!(!unsafe { persistence::sync_time(&LEAVES, 5000) });
        STATE.with_borrow(|s| {
            assert_eq!(s.ticks, 5000);
            assert_eq!(s.events.len(), 51);
            assert!(!s.events.iter().any(|e| e == "write-floor"));
        });
    }
}
