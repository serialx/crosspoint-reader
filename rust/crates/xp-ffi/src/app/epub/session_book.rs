extern crate alloc;

#[cfg(feature = "app-test")]
use super::test_support::legacy_book::LegacyBookServices;
use super::{
    abi, app_abi,
    book_owner::{queries, LoadError, ReaderBook},
    Text,
};
use core::{ffi::c_void, ptr::NonNull};

pub(super) use queries::decimal;

// Firmware has one implementation. The alternative type exists only for frozen
// synthetic session inputs, never for actual-owner composition or qualification.
#[cfg(feature = "app-test")]
pub(super) enum Loaded {
    Reader(NonNull<c_void>),
    Legacy {
        handle: NonNull<c_void>,
        services: &'static LegacyBookServices,
        path: Text,
        inline: bool,
    },
}

pub(super) enum Factory {
    Reader,
    #[cfg(feature = "app-test")]
    Legacy(&'static LegacyBookServices),
}

pub(super) enum Pending {
    Reader(Text),
    #[cfg(feature = "app-test")]
    Legacy {
        handle: Option<NonNull<c_void>>,
        services: &'static LegacyBookServices,
    },
}

impl Factory {
    pub(super) fn open(&self, path: &Text, leaves: &'static abi::XpEpubLeaves) -> Option<Pending> {
        match self {
            Self::Reader => {
                let hash = unsafe { (leaves.native_path_hash)(path.wire()) };
                let mut number = [0; 20];
                let mut cache = Text::default();
                cache
                    .assign_parts(&[b"/.crosspoint/epub_", decimal(hash, &mut number)])
                    .ok()?;
                Some(Pending::Reader(cache))
            }
            #[cfg(feature = "app-test")]
            Self::Legacy(services) => Some(Pending::Legacy {
                handle: Some(NonNull::new(unsafe { (services.book_open)(path.wire()) })?),
                services,
            }),
        }
    }
}

impl Pending {
    pub(super) fn cache_file(&self) -> Option<Text> {
        let mut path = Text::default();
        match self {
            Self::Reader(cache) => path.assign_parts(&[cache.bytes(), b"/book.bin"]).ok()?,
            #[cfg(feature = "app-test")]
            Self::Legacy { handle, services } => {
                let cache = Text::receive(|sink, context| unsafe {
                    (services.book_cache_path)(handle.unwrap().as_ptr(), sink, context)
                })?;
                path.assign_parts(&[cache.bytes(), b"/book.bin"]).ok()?;
            }
        }
        Some(path)
    }

    pub(super) fn load(
        &mut self,
        path: &mut Text,
        resources: &'static app_abi::Resources,
        leaves: &'static abi::XpEpubLeaves,
        now: unsafe extern "C" fn() -> u32,
        skip_css: bool,
    ) -> Result<NonNull<c_void>, LoadError> {
        match self {
            Self::Reader(cache) => {
                let book = ReaderBook::load(
                    path,
                    core::mem::take(cache),
                    resources,
                    leaves,
                    now,
                    true,
                    skip_css,
                )?;
                let raw = ReaderBook::into_raw(book);
                #[cfg(feature = "app-test")]
                {
                    let Ok(loaded) = allocate(Loaded::Reader(raw)) else {
                        unsafe {
                            *path = ReaderBook::take_path(raw);
                            ReaderBook::close(raw)
                        };
                        return Err(LoadError::OutOfMemory);
                    };
                    unsafe { assert_real(loaded) };
                    Ok(loaded)
                }
                #[cfg(not(feature = "app-test"))]
                Ok(raw)
            }
            #[cfg(feature = "app-test")]
            Self::Legacy { handle, services } => {
                let native = handle.unwrap();
                if unsafe { (services.book_load)(native.as_ptr(), 1, u8::from(skip_css)) } == 0 {
                    return Err(LoadError::Failed);
                }
                let entry_path = core::mem::take(path);
                let loaded = match allocate(Loaded::Legacy {
                    handle: native,
                    services,
                    path: entry_path,
                    inline: false,
                }) {
                    Ok(loaded) => loaded,
                    Err(Loaded::Legacy {
                        path: entry_path, ..
                    }) => {
                        *path = entry_path;
                        return Err(LoadError::OutOfMemory);
                    }
                    Err(Loaded::Reader(_)) => unreachable!(),
                };
                *handle = None;
                Ok(loaded)
            }
        }
    }

    pub(super) fn error(&self, error: LoadError) -> LoadError {
        #[cfg(feature = "app-test")]
        if let Self::Legacy {
            handle: Some(handle),
            services,
        } = self
        {
            if let Some(message) = Text::receive(|sink, context| unsafe {
                (services.book_protection_error)(handle.as_ptr(), sink, context)
            }) {
                return LoadError::Protection(super::book_owner::OpenError::Message(message));
            }
        }
        error
    }
}

impl Drop for Pending {
    fn drop(&mut self) {
        #[cfg(feature = "app-test")]
        if let Self::Legacy {
            handle: Some(handle),
            services,
        } = self
        {
            unsafe { (services.book_close)(handle.as_ptr()) };
        }
    }
}

#[cfg(feature = "app-test")]
fn allocate(value: Loaded) -> Result<NonNull<c_void>, Loaded> {
    let Some(raw) = NonNull::new(
        unsafe { alloc::alloc::alloc(core::alloc::Layout::new::<Loaded>()) }.cast::<Loaded>(),
    ) else {
        return Err(value);
    };
    unsafe { raw.as_ptr().write(value) };
    Ok(raw.cast())
}

#[cfg(feature = "app-test")]
pub(super) unsafe fn bind_legacy_fixture(
    owner: *mut super::EpubSession,
    handle: *mut c_void,
    services: &'static LegacyBookServices,
) {
    let access = unsafe { (*owner).borrowed_resources() };
    assert!(unsafe { (*owner).book(&access) }.is_none());
    let Some(handle) = NonNull::new(handle) else {
        return;
    };
    let pending = unsafe { (*owner).path.get() };
    let slot = unsafe { (*owner).legacy_fixture.get().cast::<Loaded>() };
    unsafe {
        slot.write(Loaded::Legacy {
            handle,
            services,
            path: core::mem::take(&mut *pending),
            inline: true,
        });
        (*owner).set_book(&access, NonNull::new(slot.cast()));
    }
}

#[cfg(feature = "app-test")]
pub(super) unsafe fn publish_legacy_fixture(
    owner: alloc::boxed::Box<super::EpubSession>,
    handle: *mut c_void,
    services: &'static LegacyBookServices,
) -> *mut c_void {
    // Publish from the final allocation; no Box reborrow follows the inline pointer.
    let raw = alloc::boxed::Box::into_raw(owner);
    unsafe { bind_legacy_fixture(raw, handle, services) };
    raw.cast()
}

#[cfg(feature = "app-test")]
unsafe fn take_loaded(raw: NonNull<c_void>) -> Loaded {
    let slot = raw.cast::<Loaded>().as_ptr();
    if matches!(unsafe { &*slot }, Loaded::Legacy { inline: true, .. }) {
        unsafe { slot.read() }
    } else {
        unsafe { *alloc::boxed::Box::from_raw(slot) }
    }
}

#[cfg(feature = "app-test")]
impl super::EpubSession {
    // Fixtures own synthetic handles externally. Reborrow inline storage from the
    // current owner, since Box reclamation invalidates its published raw pointer.
    pub(super) unsafe fn discard_legacy_fixture(&self) {
        let access = unsafe { self.borrowed_resources() };
        let Some(book) = self.take_book(&access) else {
            return;
        };
        let slot = self.legacy_fixture.get().cast::<Loaded>();
        let value = if book.as_ptr() == slot.cast() {
            unsafe { slot.read() }
        } else {
            unsafe { take_loaded(book) }
        };
        let Loaded::Legacy { path, inline, .. } = value else {
            panic!("fixture teardown cannot discard a ReaderBook");
        };
        if inline {
            unsafe { *self.path.get() = path };
        }
    }
}

#[cfg(feature = "app-test")]
impl Drop for super::EpubSession {
    fn drop(&mut self) {
        unsafe { self.discard_legacy_fixture() };
    }
}

pub(super) unsafe fn close(raw: NonNull<c_void>) {
    #[cfg(feature = "app-test")]
    {
        let loaded = unsafe { take_loaded(raw) };
        match loaded {
            Loaded::Reader(book) => unsafe { ReaderBook::close(book) },
            Loaded::Legacy {
                handle, services, ..
            } => unsafe { (services.book_close)(handle.as_ptr()) },
        }
    }
    #[cfg(not(feature = "app-test"))]
    unsafe {
        ReaderBook::close(raw)
    };
}

pub(super) unsafe fn take_path(raw: NonNull<c_void>) -> Text {
    #[cfg(feature = "app-test")]
    {
        // Field access ends before any native callback. This operation only moves text.
        match unsafe { &mut *raw.cast::<Loaded>().as_ptr() } {
            Loaded::Reader(book) => unsafe { ReaderBook::take_path(*book) },
            Loaded::Legacy { path, .. } => core::mem::take(path),
        }
    }
    #[cfg(not(feature = "app-test"))]
    unsafe {
        ReaderBook::take_path(raw)
    }
}

pub(super) unsafe fn open_facade(raw: NonNull<c_void>, leaves: &'static abi::XpEpubLeaves) -> bool {
    #[cfg(feature = "app-test")]
    {
        match unsafe { &*raw.cast::<Loaded>().as_ptr() } {
            Loaded::Reader(book) => unsafe { ReaderBook::open_facade(*book) },
            Loaded::Legacy {
                handle, services, ..
            } => {
                unsafe {
                    (leaves.image_clear_failures)();
                    (services.book_install_extractor)(handle.as_ptr())
                };
                true
            }
        }
    }
    #[cfg(not(feature = "app-test"))]
    {
        let _ = leaves;
        unsafe { ReaderBook::open_facade(raw) }
    }
}

pub(super) unsafe fn facade(raw: *mut c_void) -> *mut c_void {
    #[cfg(feature = "app-test")]
    {
        match unsafe { &*raw.cast::<Loaded>() } {
            Loaded::Reader(book) => unsafe { queries::facade(book.as_ptr()) },
            Loaded::Legacy { handle, .. } => handle.as_ptr(),
        }
    }
    #[cfg(not(feature = "app-test"))]
    unsafe {
        queries::facade(raw)
    }
}

pub(super) unsafe fn path_text<'a>(raw: *mut c_void) -> &'a Text {
    #[cfg(feature = "app-test")]
    {
        match unsafe { &*raw.cast::<Loaded>() } {
            Loaded::Reader(book) => unsafe { queries::path_text(book.as_ptr()) },
            Loaded::Legacy { path, .. } => path,
        }
    }
    #[cfg(not(feature = "app-test"))]
    unsafe {
        queries::path_text(raw)
    }
}

macro_rules! query {
    ($name:ident, $legacy:ident, ($($arg:ident: $type:ty),*) -> $result:ty) => {
        pub(super) unsafe fn $name(raw: *mut c_void, $($arg: $type),*) -> $result {
            #[cfg(feature = "app-test")]
            {
                match unsafe { &*raw.cast::<Loaded>() } {
                    Loaded::Reader(book) => unsafe { queries::$name(book.as_ptr(), $($arg),*) },
                    Loaded::Legacy { handle, services, .. } => unsafe { (services.$legacy)(handle.as_ptr(), $($arg),*) },
                }
            }
            #[cfg(not(feature = "app-test"))]
            unsafe { queries::$name(raw, $($arg),*) }
        }
    }
}
query!(size, book_size, () -> u32);
query!(spine_count, book_spine_count, () -> i32);
query!(toc_count, book_toc_count, () -> i32);
query!(toc_for_spine, book_toc_for_spine, (index: i32) -> i32);
query!(cumulative_size, book_cumulative_size, (index: i32) -> u32);
query!(spine_for_size, book_spine_for_size, (bytes: u32) -> i32);
query!(text_reference, book_text_reference, () -> i32);
query!(progress, book_progress, (spine: i32, fraction: f32) -> f32);
query!(resolve_href, book_resolve_href, (href: app_abi::Text) -> i32);
query!(setup_cache, book_setup_cache, () -> ());
query!(clear_cache, book_clear_cache, () -> ());
query!(spine, book_spine, (index: i32, sink: abi::XpEpubSpineSink, context: *mut c_void) -> u8);
query!(toc, book_toc, (index: i32, sink: abi::XpEpubTocSink, context: *mut c_void) -> u8);
query!(path, book_path, (sink: app_abi::TextSink, context: *mut c_void) -> u8);
query!(cache_path, book_cache_path, (sink: app_abi::TextSink, context: *mut c_void) -> u8);
query!(title, book_title, (sink: app_abi::TextSink, context: *mut c_void) -> u8);
query!(author, book_author, (sink: app_abi::TextSink, context: *mut c_void) -> u8);
query!(language, book_language, (sink: app_abi::TextSink, context: *mut c_void) -> u8);

pub(super) unsafe fn load_clippings(raw: *mut c_void, leaves: &'static abi::XpEpubLeaves) -> bool {
    #[cfg(feature = "app-test")]
    let raw = match unsafe { &*raw.cast::<Loaded>() } {
        Loaded::Reader(book) => book.as_ptr(),
        Loaded::Legacy {
            handle, services, ..
        } => return unsafe { (services.clippings_load)(handle.as_ptr()) != 0 },
    };
    let value = unsafe { queries::book_view(raw) };
    unsafe { (leaves.clippings_load)(&value) != 0 }
}

pub(super) unsafe fn add_recent(raw: *mut c_void, leaves: &'static abi::XpEpubLeaves) {
    #[cfg(feature = "app-test")]
    let raw = match unsafe { &*raw.cast::<Loaded>() } {
        Loaded::Reader(book) => book.as_ptr(),
        Loaded::Legacy {
            handle, services, ..
        } => return unsafe { (services.recents_add)(handle.as_ptr()) },
    };
    let mut value = unsafe { queries::book_view(raw) };
    let cache = unsafe { queries::cache_bytes(raw) };
    let mut cover = [0; 64];
    let suffix = b"/thumb_[HEIGHT].bmp";
    let length = cache.len() + suffix.len();
    assert!(length < cover.len());
    cover[..cache.len()].copy_from_slice(cache);
    cover[cache.len()..length].copy_from_slice(suffix);
    value.cover = app_abi::Text::from_bytes(&cover[..length]);
    unsafe { (leaves.recents_add)(&value) };
}

pub(super) unsafe fn load_bookmarks(
    raw: *mut c_void,
    rows: *mut c_void,
    leaves: &'static abi::XpEpubLeaves,
) -> u8 {
    #[cfg(feature = "app-test")]
    let raw = match unsafe { &*raw.cast::<Loaded>() } {
        Loaded::Reader(book) => book.as_ptr(),
        Loaded::Legacy {
            handle, services, ..
        } => return unsafe { (services.bookmarks_load)(rows, handle.as_ptr()) },
    };
    unsafe { (leaves.bookmarks_load)(rows, queries::path_text(raw).wire()) }
}
pub(super) unsafe fn save_bookmarks(
    raw: *mut c_void,
    rows: *mut c_void,
    leaves: &'static abi::XpEpubLeaves,
) -> u8 {
    #[cfg(feature = "app-test")]
    let raw = match unsafe { &*raw.cast::<Loaded>() } {
        Loaded::Reader(book) => book.as_ptr(),
        Loaded::Legacy {
            handle, services, ..
        } => return unsafe { (services.bookmarks_save)(rows, handle.as_ptr()) },
    };
    unsafe { (leaves.bookmarks_save)(rows, queries::path_text(raw).wire()) }
}

#[cfg(feature = "app-test")]
pub(super) unsafe fn assert_real(raw: NonNull<c_void>) {
    assert!(matches!(
        unsafe { &*raw.cast::<Loaded>().as_ptr() },
        Loaded::Reader(_)
    ));
}
