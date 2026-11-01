extern crate alloc;

use super::abi;
use alloc::{collections::TryReserveError, vec::Vec};
use core::{ffi::c_void, ptr::NonNull};

/// Owned text has one trailing NUL so native painting borrows without allocating.
#[derive(Default)]
pub struct Text {
    bytes: Vec<u8>,
}

impl Text {
    pub fn copy(bytes: &[u8]) -> Result<Self, TryReserveError> {
        let mut text = Self::default();
        text.assign(bytes)?;
        Ok(text)
    }

    pub fn assign(&mut self, bytes: &[u8]) -> Result<(), TryReserveError> {
        if bytes.is_empty() {
            self.bytes.clear();
            return Ok(());
        }
        let needed = bytes.len().saturating_add(1);
        self.bytes
            .try_reserve_exact(needed.saturating_sub(self.bytes.len()))?;
        self.bytes.clear();
        self.bytes.extend_from_slice(bytes);
        self.bytes.push(0);
        Ok(())
    }

    pub fn bytes(&self) -> &[u8] {
        self.bytes
            .get(..self.bytes.len().saturating_sub(1))
            .unwrap_or(&[])
    }

    pub fn wire(&self) -> abi::Text {
        if self.bytes.is_empty() {
            abi::Text::from_bytes(&b"\0"[..0])
        } else {
            abi::Text::from_bytes(self.bytes())
        }
    }

    pub fn is_empty(&self) -> bool {
        self.bytes().is_empty()
    }

    pub fn clear(&mut self) {
        self.bytes.clear();
    }

    pub fn receive(call: impl FnOnce(abi::TextSink, *mut c_void) -> u8) -> Option<Self> {
        struct Capture(Option<Text>);
        unsafe extern "C" fn sink(context: *mut c_void, text: abi::Text) -> u8 {
            let capture = unsafe { &mut *context.cast::<Capture>() };
            let Some(bytes) = (unsafe { text.bytes() }) else {
                return 0;
            };
            let Ok(owned) = Text::copy(bytes) else {
                return 0;
            };
            capture.0 = Some(owned);
            1
        }
        let mut capture = Capture(None);
        if call(sink, (&mut capture as *mut Capture).cast()) == 0 {
            return None;
        }
        capture.0
    }
}

pub struct Identity {
    pub path: Text,
    pub title: Text,
    pub author: Text,
}

pub struct Recent {
    pub identity: Identity,
    pub cover: Text,
}

impl Recent {
    pub unsafe fn copy(book: &abi::Book) -> Option<Self> {
        Some(Self {
            identity: Identity {
                path: Text::copy(unsafe { book.path.bytes() }?).ok()?,
                title: Text::copy(unsafe { book.title.bytes() }?).ok()?,
                author: Text::copy(unsafe { book.author.bytes() }?).ok()?,
            },
            cover: Text::copy(unsafe { book.cover.bytes() }?).ok()?,
        })
    }
}

pub struct Buffer {
    data: NonNull<u8>,
    len: usize,
    domain: abi::MemoryDomain,
    services: &'static abi::Resources,
}

impl Buffer {
    pub fn allocate(
        services: &'static abi::Resources,
        domain: abi::MemoryDomain,
        len: usize,
    ) -> Option<Self> {
        if len == 0 {
            return None;
        }
        let data = NonNull::new(unsafe { (services.buffer_allocate)(domain, len) })?;
        Some(Self {
            data,
            len,
            domain,
            services,
        })
    }

    pub fn len(&self) -> usize {
        self.len
    }

    fn span(&self, offset: usize, bytes: usize) -> Option<*mut u8> {
        if bytes == 0 || offset.checked_add(bytes)? > self.len {
            return None;
        }
        Some(unsafe { self.data.as_ptr().add(offset) })
    }

    pub fn read(&mut self, rect: xp_ui::scene::Rect, offset: usize, bytes: usize) -> bool {
        self.span(offset, bytes)
            .is_some_and(|data| unsafe { (self.services.region_read)(rect, data, bytes) != 0 })
    }

    pub fn write(&self, rect: xp_ui::scene::Rect, offset: usize, bytes: usize) -> bool {
        self.span(offset, bytes)
            .is_some_and(|data| unsafe { (self.services.region_write)(rect, data, bytes) != 0 })
    }
}

impl Drop for Buffer {
    fn drop(&mut self) {
        unsafe { (self.services.buffer_release)(self.domain, self.data.as_ptr()) };
    }
}

pub struct Book {
    object: NonNull<c_void>,
    services: &'static abi::Resources,
}

impl Book {
    pub fn open(
        services: &'static abi::Resources,
        kind: abi::BookKind,
        path: &Text,
    ) -> Option<Self> {
        Some(Self {
            object: NonNull::new(unsafe { (services.book_open)(kind, path.wire()) })?,
            services,
        })
    }

    pub fn thumbnail(&self, height: i32) -> Option<Text> {
        Text::receive(|sink, context| unsafe {
            (self.services.book_thumb_path)(self.object.as_ptr(), height, sink, context)
        })
    }

    pub fn load(&mut self, build_missing: bool, skip_css: bool) -> bool {
        unsafe {
            (self.services.book_load)(
                self.object.as_ptr(),
                u8::from(build_missing),
                u8::from(skip_css),
            ) != 0
        }
    }

    pub fn generate(&mut self, height: i32, from_source: bool) -> bool {
        let generate = if from_source {
            self.services.book_generate_thumb_from_source
        } else {
            self.services.book_generate_thumb
        };
        unsafe { generate(self.object.as_ptr(), height) != 0 }
    }
}

impl Drop for Book {
    fn drop(&mut self) {
        unsafe { (self.services.book_close)(self.object.as_ptr()) };
    }
}

pub struct Bitmap {
    object: NonNull<c_void>,
    services: &'static abi::Resources,
}

impl Bitmap {
    pub fn open(services: &'static abi::Resources, path: &Text) -> Option<Self> {
        Some(Self {
            object: NonNull::new(unsafe { (services.bitmap_open)(path.wire()) })?,
            services,
        })
    }

    pub fn info(&self) -> abi::BitmapInfo {
        unsafe { (self.services.bitmap_info)(self.object.as_ptr()) }
    }

    pub fn draw(&mut self, rect: xp_ui::scene::Rect, shift: i32) -> bool {
        unsafe { (self.services.bitmap_draw)(self.object.as_ptr(), rect, shift) != 0 }
    }
}

impl Drop for Bitmap {
    fn drop(&mut self) {
        unsafe { (self.services.bitmap_close)(self.object.as_ptr()) };
    }
}

pub struct Library {
    object: NonNull<c_void>,
    services: &'static abi::Resources,
}

impl Library {
    pub fn open(services: &'static abi::Resources) -> Option<Self> {
        Some(Self {
            object: NonNull::new(unsafe { (services.library_open)() })?,
            services,
        })
    }

    /// Native ordinals are u16; MAX denotes an index that did not open.
    pub fn count(&self) -> Option<u32> {
        let count = unsafe { (self.services.library_count)(self.object.as_ptr()) };
        (count != u32::MAX).then_some(count)
    }

    pub fn row(&mut self, row: u32, metadata: bool) -> Result<Option<Recent>, ()> {
        struct Capture(Option<Recent>, bool);
        unsafe extern "C" fn sink(context: *mut c_void, book: *const abi::Book) -> u8 {
            let capture = unsafe { &mut *context.cast::<Capture>() };
            capture.0 = unsafe { book.as_ref().and_then(|book| Recent::copy(book)) };
            capture.1 = capture.0.is_none();
            u8::from(!capture.1)
        }
        let mut capture = Capture(None, false);
        let status = unsafe {
            (self.services.library_row)(
                self.object.as_ptr(),
                row,
                u8::from(metadata),
                sink,
                (&mut capture as *mut Capture).cast(),
            )
        };
        if status > 1 || capture.1 {
            Err(())
        } else {
            Ok(capture.0)
        }
    }
}

impl Drop for Library {
    fn drop(&mut self) {
        unsafe { (self.services.library_close)(self.object.as_ptr()) };
    }
}
