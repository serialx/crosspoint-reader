#[derive(Clone, Copy)]
pub enum Mode {
    Read,
    Rewrite,
    Append,
    ReadWrite,
}

pub trait File {
    fn read(&mut self, bytes: &mut [u8]) -> i32;
    fn write(&mut self, bytes: &[u8]) -> usize;
    fn seek(&mut self, offset: u32) -> bool;
    fn seek_relative(&mut self, offset: u32) -> bool;
    fn position(&self) -> u32;
    fn available(&self) -> i32;
    fn size(&self) -> u32;
    fn truncate(&mut self, size: u32) -> bool;
    fn flush(&mut self);
    fn close(&mut self);
    fn is_directory(&self) -> bool;
    fn next(&mut self) -> Option<Self>
    where
        Self: Sized;
    fn name(&self, out: &mut [u8]) -> usize;
}

// Implementations own HalFile-backed handles, closing each exactly once on Drop.
pub trait Io {
    type File: File;
    fn open(&self, path: &[u8], mode: Mode) -> Option<Self::File>;
    fn open_read(&self, path: &[u8]) -> Option<Self::File>;
    fn exists(&self, path: &[u8]) -> bool;
    fn mkdir(&self, path: &[u8]);
    fn remove(&self, path: &[u8]) -> bool;
    fn rename(&self, from: &[u8], to: &[u8]) -> bool;
    fn path_hash(&self, path: &[u8]) -> u64;
    fn epoch_seconds(&self) -> i64;
    fn random(&self, out: &mut [u8]);
}
