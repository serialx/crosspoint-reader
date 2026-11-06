use crate::bookmarks::export::{self, Calendar};
use crate::clipping_io::{DirectoryError, File, Io, Mode};
use std::{cell::RefCell, rc::Rc};

#[test]
fn kindle_header_clock_spacing_and_separator_match_native_bytes() {
    let clock = Calendar {
        year: 124,
        month: 4,
        day: 9,
        weekday: 4,
        hour: 17,
        minute: 32,
    };
    let output = export::bytes(
        b"Book A",
        b"Author A",
        b"Chapter One",
        12,
        b"Kerning is the adjustment of spacing",
        Some(clock),
    )
    .unwrap();
    assert_eq!(output, b"Book A (Author A)\n- Your Highlight on Page 12 | Chapter One | Added on Thursday, May 9, 2024, 05:32 PM\n\nKerning is the adjustment of spacing\n==========\n");
}

struct State {
    events: Vec<String>,
    bytes: Vec<u8>,
    open: bool,
    write_limit: usize,
    rollback: bool,
}
struct Volume(Rc<RefCell<State>>);
struct Handle {
    state: Rc<RefCell<State>>,
    open: bool,
}
impl Drop for Handle {
    fn drop(&mut self) {
        self.close();
    }
}
impl File for Handle {
    fn size(&self) -> u32 {
        let mut state = self.state.borrow_mut();
        state.events.push("size".into());
        state.bytes.len() as u32
    }
    fn write(&mut self, bytes: &[u8]) -> usize {
        let mut state = self.state.borrow_mut();
        state.events.push("write".into());
        let count = bytes.len().min(state.write_limit);
        state.bytes.extend_from_slice(&bytes[..count]);
        count
    }
    fn truncate(&mut self, size: u32) -> bool {
        let mut state = self.state.borrow_mut();
        state.events.push("truncate".into());
        if state.rollback {
            state.bytes.truncate(size as usize);
        }
        state.rollback
    }
    fn flush(&mut self) {
        self.state.borrow_mut().events.push("flush".into());
    }
    fn close(&mut self) {
        if self.open {
            self.open = false;
            self.state.borrow_mut().events.push("close".into());
        }
    }
    fn read(&mut self, _: &mut [u8]) -> i32 {
        unreachable!()
    }
    fn seek(&mut self, _: u32) -> bool {
        unreachable!()
    }
    fn seek_relative(&mut self, _: u32) -> bool {
        unreachable!()
    }
    fn position(&self) -> u32 {
        unreachable!()
    }
    fn available(&self) -> i32 {
        unreachable!()
    }
    fn is_directory(&self) -> bool {
        unreachable!()
    }
    fn next(&mut self) -> Result<Option<Self>, DirectoryError> {
        unreachable!()
    }
    fn name(&self, _: &mut [u8]) -> usize {
        unreachable!()
    }
}
impl Io for Volume {
    type File = Handle;
    fn open(&self, path: &[u8], mode: Mode) -> Option<Handle> {
        assert_eq!(path, b"/My Clippings.txt");
        assert!(matches!(mode, Mode::Append));
        let mut state = self.0.borrow_mut();
        state.events.push("open".into());
        state.open.then(|| Handle {
            state: self.0.clone(),
            open: true,
        })
    }
    fn log(&self, level: u8, message: core::fmt::Arguments<'_>, tail: &[&[u8]]) {
        let mut line = format!("log{level}:{message}");
        for bytes in tail {
            line.push_str(&String::from_utf8_lossy(bytes));
        }
        self.0.borrow_mut().events.push(line);
    }
    fn open_read(&self, _: &[u8]) -> Option<Handle> {
        unreachable!()
    }
    fn exists(&self, _: &[u8]) -> bool {
        unreachable!()
    }
    fn mkdir(&self, _: &[u8]) {
        unreachable!()
    }
    fn remove(&self, _: &[u8]) -> bool {
        unreachable!()
    }
    fn rename(&self, _: &[u8], _: &[u8]) -> bool {
        unreachable!()
    }
    fn path_hash(&self, _: &[u8]) -> u64 {
        unreachable!()
    }
    fn epoch_seconds(&self) -> i64 {
        unreachable!()
    }
    fn random(&self, _: &mut [u8]) {
        unreachable!()
    }
}
fn volume() -> Volume {
    Volume(Rc::new(RefCell::new(State {
        events: Vec::new(),
        bytes: b"old".to_vec(),
        open: true,
        write_limit: 5,
        rollback: true,
    })))
}
fn write(io: &Volume) -> bool {
    export::write(
        io,
        || {
            io.0.borrow_mut().events.push("calendar".into());
            None
        },
        export::Clipping {
            title: b"title",
            author: b"author",
            chapter: b"chapter",
            page: -12,
            text: b"text",
        },
    )
}

#[test]
fn short_write_rolls_back_then_flushes_before_error_and_close() {
    for rollback in [true, false] {
        let io = volume();
        io.0.borrow_mut().rollback = rollback;
        assert!(!write(&io));
        let state = io.0.borrow();
        let mut expected = vec!["open", "size", "calendar", "write", "truncate", "flush"];
        if !rollback {
            expected.push("log1:Failed to roll back partial write to /My Clippings.txt");
        }
        expected.extend([
            "log1:Failed to write clipping to /My Clippings.txt",
            "close",
        ]);
        assert_eq!(state.events, expected);
        assert_eq!(
            state.bytes,
            if rollback {
                b"old".as_slice()
            } else {
                b"oldtitle"
            }
        );
    }
}

#[test]
fn failed_open_skips_clock_and_success_never_truncates() {
    let io = volume();
    io.0.borrow_mut().open = false;
    assert!(!write(&io));
    assert_eq!(
        io.0.borrow().events,
        ["open", "log1:Failed to open /My Clippings.txt for append"]
    );
    let io = volume();
    io.0.borrow_mut().write_limit = usize::MAX;
    assert!(write(&io));
    assert_eq!(
        io.0.borrow().events,
        ["open", "size", "calendar", "write", "flush", "close"]
    );
    assert!(io
        .0
        .borrow()
        .bytes
        .starts_with(b"oldtitle (author)\n- Your Highlight on Page -12"));
}
