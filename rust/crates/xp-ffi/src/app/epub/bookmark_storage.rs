use super::{EpubSession, ResourceAccess, Text};
use crate::app::bookmarks::{self, keyboard_name::KeyboardName, persistence, Record};
use crate::app::keyboard::NativeMemory;
use core::mem::MaybeUninit;

pub(super) type Name = KeyboardName<NativeMemory>;
pub(super) type Row = Record<Name>;
struct Storage<'a>(&'a EpubSession);
impl persistence::Storage for Storage<'_> {
    fn exists(&self, path: &[u8]) -> bool {
        Text::copy(path)
            .is_ok_and(|path| unsafe { (self.0.resources.path_exists)(path.wire()) != 0 })
    }
    fn mkdir(&self, path: &[u8]) {
        if let Ok(path) = Text::copy(path) {
            unsafe { (self.0.leaves.path_mkdir)(path.wire()) };
        }
    }
    fn read_file<T>(&self, path: &[u8], consume: impl FnOnce(&[u8]) -> T) -> Option<T> {
        super::persistence::read_file(self.0.leaves, &Text::copy(path).ok()?, consume)
    }
    fn write_file_atomic(&self, path: &[u8], bytes: &[u8]) -> bool {
        let (Ok(path), Ok(size)) = (Text::copy(path), u32::try_from(bytes.len())) else {
            return false;
        };
        unsafe { (self.0.leaves.storage_write_file_atomic)(path.wire(), bytes.as_ptr(), size) != 0 }
    }
    fn log(
        &self,
        severity: u8,
        origin: &[u8],
        arguments: core::fmt::Arguments<'_>,
        tail: &[&[u8]],
    ) {
        super::book_owner::log::with_tail(self.0.leaves, severity, origin, arguments, tail);
    }
}
impl EpubSession {
    #[inline(never)]
    pub(super) fn read_bookmarks(&self, access: &ResourceAccess<'_>) {
        self.mutate_bookmarks(access, |rows| *rows = Default::default());
        if self.book(access).is_none() {
            return;
        }
        // The native child and reader both use the EPUB path, including odd filenames.
        let path = self.path().bytes();
        if let Some(rows) = persistence::read::<Name>(&Storage(self), path) {
            self.mutate_bookmarks(access, |target| *target = rows);
        }
    }
    #[inline(never)]
    pub(super) fn write_bookmarks(&self, access: &ResourceAccess<'_>) -> bool {
        let prepared = self.with_bookmarks(access, persistence::prepare);
        persistence::write(&Storage(self), self.path().bytes(), prepared)
    }
    #[inline(never)]
    pub(super) fn save_bookmarks(&self) -> bool {
        let prepared = {
            let access = self.lock_resources();
            self.with_bookmarks(&access, persistence::prepare)
        };
        persistence::write(&Storage(self), self.path().bytes(), prepared)
    }
    #[inline(never)]
    pub(super) fn export_clipping(
        &self,
        title: &[u8],
        author: &[u8],
        chapter: &[u8],
        page: i32,
        text: &[u8],
    ) -> bool {
        let Some(physical) = (unsafe { self.leaves.clipping_physical.as_ref() }) else {
            return false;
        };
        let io = crate::app::clippings_native::Native {
            leaves: self.leaves,
            resources: self.resources,
            physical,
        };
        bookmarks::export::write(
            &io,
            || {
                let mut value = MaybeUninit::uninit();
                (unsafe { (self.leaves.local_calendar)(value.as_mut_ptr()) } != 0)
                    .then(|| unsafe { value.assume_init() })
            },
            bookmarks::export::Clipping {
                title,
                author,
                chapter,
                page,
                text,
            },
        )
    }
}
