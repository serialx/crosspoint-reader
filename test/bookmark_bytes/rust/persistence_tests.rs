use crate::bookmarks::{
    self,
    persistence::{self, Storage},
};
use std::cell::{Cell, RefCell};

struct Volume {
    body: RefCell<Vec<u8>>,
    events: RefCell<Vec<String>>,
    present: bool,
    writes: bool,
    reading: Cell<bool>,
}
impl Volume {
    fn new(body: &[u8]) -> Self {
        Self {
            body: RefCell::new(body.to_vec()),
            events: RefCell::new(Vec::new()),
            present: true,
            writes: true,
            reading: Cell::new(false),
        }
    }
    fn event(&self, text: String) {
        self.events.borrow_mut().push(text);
    }
}
impl Storage for Volume {
    fn exists(&self, _: &[u8]) -> bool {
        self.event("exists".into());
        self.present
    }
    fn mkdir(&self, path: &[u8]) {
        self.event(format!("mkdir:{}", String::from_utf8_lossy(path)));
    }
    fn read_file<T>(&self, _: &[u8], consume: impl FnOnce(&[u8]) -> T) -> Option<T> {
        self.event("read".into());
        let result = {
            let input = self.body.borrow();
            if input.is_empty() {
                return None;
            }
            self.reading.set(true);
            let value = consume(&input);
            self.reading.set(false);
            value
        };
        // Destroy the borrowed source before the loaded records are used.
        self.body.borrow_mut().fill(0);
        Some(result)
    }
    fn write_file_atomic(&self, path: &[u8], bytes: &[u8]) -> bool {
        assert!(!self.reading.get());
        self.event(format!("write:{}", String::from_utf8_lossy(path)));
        if self.writes {
            *self.body.borrow_mut() = bytes.to_vec();
        }
        self.writes
    }
    fn log(&self, level: u8, origin: &[u8], arguments: core::fmt::Arguments<'_>, tail: &[&[u8]]) {
        assert!(
            !self.reading.get(),
            "publish/log only after the read loan ends"
        );
        let mut text = format!("{level}:{}:{arguments}", String::from_utf8_lossy(origin));
        for part in tail {
            text.push_str(&String::from_utf8_lossy(part));
        }
        self.event(text);
    }
}

#[test]
fn loaded_fields_survive_the_sdk_buffer_and_read_loan() {
    let io = Volume::new(br#"{"bookmarks":[{"xpath":"p","summary":"s","name":"named","vo":0}]}"#);
    let rows = persistence::read::<Vec<u8>>(&io, b"/a.epub").unwrap();
    assert_eq!(rows[0].xpath, b"p");
    assert_eq!(rows[0].name, b"named");
    assert_eq!(rows[0].visible_offset, Some(0));
    assert!(io.body.borrow().iter().all(|b| *b == 0));
    assert_eq!(
        *io.events.borrow(),
        ["exists", "read", "0:BKM:Loaded 1 bookmarks from file"]
    );
}

#[test]
fn missing_empty_and_malformed_reads_keep_native_diagnostics() {
    let mut io = Volume::new(b"");
    io.present = false;
    assert!(persistence::read::<Vec<u8>>(&io, b"/a.epub").is_none());
    assert_eq!(*io.events.borrow(), ["exists"]);
    io.present = true;
    io.events.borrow_mut().clear();
    assert!(persistence::read::<Vec<u8>>(&io, b"/a.epub").is_none());
    assert_eq!(
        *io.events.borrow(),
        [
            "exists",
            "read",
            "1:PERSIST:Failed to read /.crosspoint/bookmarks/a.json (empty)"
        ]
    );
    *io.body.borrow_mut() = b"{broken".to_vec();
    io.events.borrow_mut().clear();
    assert!(persistence::read::<Vec<u8>>(&io, b"/a.epub").is_none());
    assert_eq!(
        *io.events.borrow(),
        [
            "exists",
            "read",
            "1:PERSIST:JSON parse error in /.crosspoint/bookmarks/a.json: IncompleteInput"
        ]
    );
}

#[test]
fn prepared_bytes_outlive_records_and_keep_write_error_order() {
    let mut io = Volume::new(b"");
    io.writes = false;
    let prepared = {
        let rows = bookmarks::decode::<Vec<u8>>(br#"{"bookmarks":[{"name":"owned"}]}"#).unwrap();
        persistence::prepare(&rows)
    };
    assert!(!persistence::write(&io, b"/a.epub", prepared));
    assert_eq!(
        *io.events.borrow(),
        [
            "0:BKM:Saving 1 bookmarks to file",
            "mkdir:/.crosspoint/bookmarks/",
            "mkdir:/.crosspoint",
            "write:/.crosspoint/bookmarks/a.json",
            "1:PERSIST:Failed to write /.crosspoint/bookmarks/a.json"
        ]
    );
}

#[test]
fn invalid_name_has_no_directory_or_write_effect() {
    let io = Volume::new(b"");
    let mut rows = bookmarks::decode::<Vec<u8>>(br#"{"bookmarks":[{}]}"#).unwrap();
    rows[0].name.resize(129, b'x');
    assert!(!persistence::write(
        &io,
        b"/a.epub",
        persistence::prepare(&rows)
    ));
    assert_eq!(
        *io.events.borrow(),
        ["1:BKM:Bookmark name exceeds 128 bytes"]
    );
}

#[test]
fn failed_rename_restores_old_storage_but_failed_delete_stays_deleted() {
    use bookmarks::mutations;
    let mut io = Volume::new(b"");
    io.writes = false;
    let mut rows =
        bookmarks::decode::<Vec<u8>>(br#"{"bookmarks":[{"name":"old"},{"name":"last"}]}"#).unwrap();
    let old_pointer = rows[0].name.as_ptr();
    let new = b"new".to_vec();
    let new_pointer = new.as_ptr();
    let old = mutations::replace_name(&mut rows, 0, new).unwrap();
    assert_eq!(rows[0].name.as_ptr(), new_pointer);
    assert!(!persistence::write(
        &io,
        b"/a.epub",
        persistence::prepare(&rows)
    ));
    drop(mutations::replace_name(&mut rows, 0, old).unwrap());
    assert_eq!(rows[0].name.as_ptr(), old_pointer);
    assert_eq!(rows[0].name, b"old");
    drop(mutations::remove(&mut rows, 0).unwrap());
    assert!(!persistence::write(
        &io,
        b"/a.epub",
        persistence::prepare(&rows)
    ));
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0].name, b"last");
    assert!(mutations::remove(&mut rows, 1).is_none());
    assert!(mutations::replace_name(&mut rows, 1, b"stale".to_vec()).is_err());
    drop(mutations::replace_name(&mut rows, 0, Vec::new()).unwrap());
    assert!(rows[0].name.is_empty());
}

#[cfg(feature = "app-test")]
#[test]
fn physical_read_callback_loan_ends_before_native_input_is_destroyed() {
    use bookmarks::host::{Bytes, Host, Operations};
    use core::ffi::c_void;
    unsafe extern "C" fn exists(_: Bytes) -> u8 {
        1
    }
    unsafe extern "C" fn mkdir(_: Bytes) {}
    unsafe extern "C" fn read(
        _: Bytes,
        receive: unsafe extern "C" fn(*mut c_void, Bytes),
        context: *mut c_void,
    ) -> u8 {
        let mut data = br#"{"bookmarks":[{"name":"borrowed source"}]}"#.to_vec();
        unsafe {
            receive(
                context,
                Bytes {
                    data: data.as_ptr(),
                    length: data.len(),
                },
            )
        };
        data.fill(0);
        1
    }
    unsafe extern "C" fn write(_: Bytes, data: Bytes) -> u8 {
        let bytes = unsafe { core::slice::from_raw_parts(data.data, data.length) };
        u8::from(bytes.windows(15).any(|s| s == b"borrowed source"))
    }
    static OPS: Operations = Operations {
        exists,
        mkdir,
        read,
        write,
    };
    let io = Host(&OPS);
    let rows = persistence::read::<Vec<u8>>(&io, b"/a.epub").unwrap();
    assert_eq!(rows[0].name, b"borrowed source");
    let prepared = persistence::prepare(&rows);
    drop(rows);
    assert!(persistence::write(&io, b"/a.epub", prepared));
}
