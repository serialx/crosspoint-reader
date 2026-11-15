// Host-only physical adapter for the independent native export corpus.
use super::super::clippings::io::{DirectoryError, File, Io, Mode};
use super::{
    export::{self, Calendar, Clipping},
    host::Bytes,
};
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct Operations {
    pub open: unsafe extern "C" fn(Bytes) -> *mut c_void,
    pub size: unsafe extern "C" fn(*mut c_void) -> u32,
    pub write: unsafe extern "C" fn(*mut c_void, Bytes) -> usize,
    pub truncate: unsafe extern "C" fn(*mut c_void, u32) -> u8,
    pub flush: unsafe extern "C" fn(*mut c_void),
    pub drop: unsafe extern "C" fn(*mut c_void),
    pub calendar: unsafe extern "C" fn(*mut Calendar) -> u8,
}
struct Host(&'static Operations);
struct Handle {
    operations: &'static Operations,
    pointer: Option<NonNull<c_void>>,
}
impl Handle {
    fn raw(&self) -> *mut c_void {
        self.pointer.expect("open export file").as_ptr()
    }
}
impl Drop for Handle {
    fn drop(&mut self) {
        self.close();
    }
}
impl File for Handle {
    fn size(&self) -> u32 {
        unsafe { (self.operations.size)(self.raw()) }
    }
    fn write(&mut self, bytes: &[u8]) -> usize {
        unsafe { (self.operations.write)(self.raw(), Bytes::from(bytes)) }
    }
    fn truncate(&mut self, size: u32) -> bool {
        unsafe { (self.operations.truncate)(self.raw(), size) != 0 }
    }
    fn flush(&mut self) {
        unsafe { (self.operations.flush)(self.raw()) };
    }
    fn close(&mut self) {
        if let Some(pointer) = self.pointer.take() {
            unsafe { (self.operations.drop)(pointer.as_ptr()) };
        }
    }
    fn read(&mut self, _: &mut [u8]) -> i32 {
        unreachable!("export does not read")
    }
    fn seek(&mut self, _: u32) -> bool {
        unreachable!("export does not seek")
    }
    fn seek_relative(&mut self, _: u32) -> bool {
        unreachable!("export does not seek")
    }
    fn position(&self) -> u32 {
        unreachable!("export does not query position")
    }
    fn available(&self) -> i32 {
        unreachable!("export does not query availability")
    }
    fn is_directory(&self) -> bool {
        unreachable!("export is not a directory")
    }
    fn next(&mut self) -> Result<Option<Self>, DirectoryError> {
        unreachable!("export is not a directory")
    }
    fn name(&self, _: &mut [u8]) -> usize {
        unreachable!("export has a fixed path")
    }
}
impl Io for Host {
    type File = Handle;
    fn open(&self, path: &[u8], mode: Mode) -> Option<Handle> {
        assert!(matches!(mode, Mode::Append));
        NonNull::new(unsafe { (self.0.open)(Bytes::from(path)) }).map(|pointer| Handle {
            operations: self.0,
            pointer: Some(pointer),
        })
    }
    fn open_read(&self, _: &[u8]) -> Option<Handle> {
        unreachable!("export appends")
    }
    fn exists(&self, _: &[u8]) -> bool {
        unreachable!("export opens directly")
    }
    fn mkdir(&self, _: &[u8]) {
        unreachable!("export uses root")
    }
    fn remove(&self, _: &[u8]) -> bool {
        unreachable!("export rolls back by truncation")
    }
    fn rename(&self, _: &[u8], _: &[u8]) -> bool {
        unreachable!("export does not rename")
    }
    fn path_hash(&self, _: &[u8]) -> u64 {
        unreachable!("export has a fixed path")
    }
    fn epoch_seconds(&self) -> i64 {
        unreachable!("export uses local calendar")
    }
    fn random(&self, _: &mut [u8]) {
        unreachable!("export has no random state")
    }
}

#[no_mangle]
/// # Safety
/// Input spans are readable for this call and the physical callbacks own their
/// returned handles until drop; no callback retains a borrowed write span.
pub unsafe extern "C" fn xp_bookmarks_test_export(
    operations: &'static Operations,
    title: Bytes,
    author: Bytes,
    chapter: Bytes,
    page: i32,
    text: Bytes,
) -> u8 {
    let clock = || {
        let mut value = Calendar {
            year: 0,
            month: 0,
            day: 0,
            weekday: 0,
            hour: 0,
            minute: 0,
        };
        (unsafe { (operations.calendar)(&mut value) } != 0).then_some(value)
    };
    u8::from(export::write(
        &Host(operations),
        clock,
        Clipping {
            title: unsafe { title.bytes() },
            author: unsafe { author.bytes() },
            chapter: unsafe { chapter.bytes() },
            page,
            text: unsafe { text.bytes() },
        },
    ))
}

#[cfg(test)]
mod tests {
    use super::super::alloc::{boxed::Box, vec::Vec};
    use super::*;
    use std::cell::{Cell, RefCell};

    thread_local! {
        static CLOSED: Cell<usize> = const { Cell::new(0) };
        static FLUSHED: Cell<usize> = const { Cell::new(0) };
        static CONTENT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    }
    unsafe extern "C" fn open(_: Bytes) -> *mut c_void {
        Box::into_raw(Box::new(Vec::<u8>::new())).cast()
    }
    unsafe extern "C" fn size(file: *mut c_void) -> u32 {
        unsafe { (&*file.cast::<Vec<u8>>()).len() as u32 }
    }
    unsafe extern "C" fn write(file: *mut c_void, bytes: Bytes) -> usize {
        unsafe { (&mut *file.cast::<Vec<u8>>()).extend_from_slice(bytes.bytes()) };
        bytes.length
    }
    unsafe extern "C" fn truncate(_: *mut c_void, _: u32) -> u8 {
        panic!("successful export must not truncate")
    }
    unsafe extern "C" fn flush(_: *mut c_void) {
        FLUSHED.with(|n| n.set(n.get() + 1));
    }
    unsafe extern "C" fn drop_file(file: *mut c_void) {
        let bytes = unsafe { Box::from_raw(file.cast::<Vec<u8>>()) };
        CONTENT.with(|out| *out.borrow_mut() = *bytes);
        CLOSED.with(|n| n.set(n.get() + 1));
    }
    unsafe extern "C" fn calendar(out: *mut Calendar) -> u8 {
        unsafe {
            out.write(Calendar {
                year: 124,
                month: 4,
                day: 9,
                weekday: 4,
                hour: 17,
                minute: 32,
            })
        };
        1
    }
    static OPS: Operations = Operations {
        open,
        size,
        write,
        truncate,
        flush,
        drop: drop_file,
        calendar,
    };

    #[test]
    fn physical_export_borrows_spans_and_drops_the_handle_once() {
        CLOSED.with(|n| n.set(0));
        FLUSHED.with(|n| n.set(0));
        let mut title = b"title".to_vec();
        assert_eq!(
            unsafe {
                xp_bookmarks_test_export(
                    &OPS,
                    Bytes::from(&title),
                    Bytes::from(b"author"),
                    Bytes::from(b"chapter"),
                    12,
                    Bytes::from(b"selected"),
                )
            },
            1
        );
        title.fill(0);
        assert_eq!(CLOSED.with(Cell::get), 1);
        assert_eq!(FLUSHED.with(Cell::get), 1);
        CONTENT.with(|bytes| assert_eq!(*bytes.borrow(), b"title (author)\n- Your Highlight on Page 12 | chapter | Added on Thursday, May 9, 2024, 05:32 PM\n\nselected\n==========\n"));
        CONTENT.with(|bytes| bytes.borrow_mut().clear());
    }
}
