use crate::clippings::io::{File, Io, Mode};
use std::{cell::RefCell, rc::Rc};

#[derive(Default)]
struct State {
    files: [Option<Vec<u8>>; 3],
    operations: usize,
    fail_write: bool,
    log: Option<Log>,
}
struct Log {
    bytes: [u8; 256],
    length: usize,
    severity: u8,
}
impl Log {
    fn append(&mut self, bytes: &[u8]) {
        let length = bytes.len().min(self.bytes.len() - self.length);
        self.bytes[self.length..self.length + length].copy_from_slice(&bytes[..length]);
        self.length += length;
    }
}
impl core::fmt::Write for Log {
    fn write_str(&mut self, text: &str) -> core::fmt::Result {
        self.append(text.as_bytes());
        Ok(())
    }
}
#[derive(Clone)]
pub struct Volume(Rc<RefCell<State>>);
pub struct Handle {
    volume: Volume,
    file: usize,
    position: usize,
}

fn slot(path: &[u8]) -> usize {
    if path.ends_with(b".tmp") {
        1
    } else if path.ends_with(b".bak") {
        2
    } else {
        0
    }
}
impl Volume {
    pub fn new() -> Self {
        Self(Rc::new(RefCell::new(State::default())))
    }
    pub fn source(&self) -> Option<Vec<u8>> {
        self.0.borrow().files[0].clone()
    }
    pub fn operations(&self) -> usize {
        self.0.borrow().operations
    }
    pub fn fail_write(&self) {
        self.0.borrow_mut().fail_write = true;
    }
    pub fn last_log(&self) -> (u8, String) {
        let state = self.0.borrow();
        let log = state.log.as_ref().unwrap();
        (
            log.severity,
            String::from_utf8(log.bytes[..log.length].to_vec()).unwrap(),
        )
    }
}
impl Io for Volume {
    type File = Handle;
    fn open(&self, path: &[u8], mode: Mode) -> Option<Handle> {
        let file = slot(path);
        let mut state = self.0.borrow_mut();
        state.operations += 1;
        if matches!(mode, Mode::Rewrite) {
            state.files[file] = Some(Vec::new());
        }
        state.files[file].as_ref()?;
        Some(Handle {
            volume: self.clone(),
            file,
            position: 0,
        })
    }
    fn open_read(&self, path: &[u8]) -> Option<Handle> {
        self.open(path, Mode::Read)
    }
    fn exists(&self, path: &[u8]) -> bool {
        self.0.borrow().files[slot(path)].is_some()
    }
    fn mkdir(&self, _: &[u8]) {
        self.0.borrow_mut().operations += 1;
    }
    fn remove(&self, path: &[u8]) -> bool {
        let mut state = self.0.borrow_mut();
        state.operations += 1;
        state.files[slot(path)].take().is_some()
    }
    fn rename(&self, from: &[u8], to: &[u8]) -> bool {
        let mut state = self.0.borrow_mut();
        state.operations += 1;
        if state.files[slot(to)].is_some() {
            return false;
        }
        let Some(file) = state.files[slot(from)].take() else {
            return false;
        };
        state.files[slot(to)] = Some(file);
        true
    }
    fn path_hash(&self, _: &[u8]) -> u64 {
        0
    }
    fn epoch_seconds(&self) -> i64 {
        1700000000
    }
    fn random(&self, out: &mut [u8]) {
        out.fill(1);
    }
    fn log(&self, severity: u8, arguments: core::fmt::Arguments<'_>, tail: &[&[u8]]) {
        use core::fmt::Write;
        let mut log = Log {
            bytes: [0; 256],
            length: 0,
            severity,
        };
        log.write_fmt(arguments).unwrap();
        for part in tail {
            log.append(part);
        }
        self.0.borrow_mut().log = Some(log);
    }
}
impl File for Handle {
    fn read(&mut self, out: &mut [u8]) -> i32 {
        let state = self.volume.0.borrow();
        let bytes = state.files[self.file].as_ref().unwrap();
        let count = out.len().min(bytes.len() - self.position);
        out[..count].copy_from_slice(&bytes[self.position..self.position + count]);
        self.position += count;
        count as i32
    }
    fn write(&mut self, input: &[u8]) -> usize {
        let mut state = self.volume.0.borrow_mut();
        state.operations += 1;
        if state.fail_write {
            state.fail_write = false;
            return 0;
        }
        let bytes = state.files[self.file].as_mut().unwrap();
        bytes.resize(bytes.len().max(self.position + input.len()), 0);
        bytes[self.position..self.position + input.len()].copy_from_slice(input);
        self.position += input.len();
        input.len()
    }
    fn seek(&mut self, offset: u32) -> bool {
        if offset > self.size() {
            return false;
        }
        self.position = offset as usize;
        true
    }
    fn seek_relative(&mut self, offset: u32) -> bool {
        self.seek(self.position() + offset)
    }
    fn position(&self) -> u32 {
        self.position as u32
    }
    fn available(&self) -> i32 {
        self.size() as i32 - self.position as i32
    }
    fn size(&self) -> u32 {
        self.volume.0.borrow().files[self.file]
            .as_ref()
            .unwrap()
            .len() as u32
    }
    fn truncate(&mut self, size: u32) -> bool {
        self.volume.0.borrow_mut().files[self.file]
            .as_mut()
            .unwrap()
            .truncate(size as usize);
        true
    }
    fn flush(&mut self) {}
    fn close(&mut self) {}
    fn is_directory(&self) -> bool {
        false
    }
    fn next(&mut self) -> Result<Option<Self>, crate::clippings::io::DirectoryError> {
        Ok(None)
    }
    fn name(&self, _: &mut [u8]) -> usize {
        0
    }
}
