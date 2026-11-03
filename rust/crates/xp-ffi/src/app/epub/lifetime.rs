use super::{
    book::with_book_text,
    book_owner::{LoadError, OpenError},
    state::LoadFailure,
    storage::{File, FileMode},
    EpubSession, ResourceAccess, Text,
};
use core::{ffi::c_void, ptr::NonNull};

struct LoadedBook {
    handle: Option<NonNull<c_void>>,
}
impl Drop for LoadedBook {
    fn drop(&mut self) {
        if let Some(book) = self.handle {
            unsafe { super::session_book::close(book) };
        }
    }
}

struct LoadLoan<'a> {
    access: &'a ResourceAccess<'a>,
}
impl Drop for LoadLoan<'_> {
    fn drop(&mut self) {
        let owner = self.access.owner;
        unsafe { (owner.leaves.loan_drop)(owner.native(self.access)) };
    }
}

impl EpubSession {
    pub(super) fn entry_load_book(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            let loaded = unsafe { (hook.load)(hook.context) != 0 };
            self.model(|state| state.ready = true);
            return loaded;
        }
        let loaded = {
            let access = self.entry_resources();
            self.load_book(&access)
        };
        self.model(|state| state.ready = true);
        loaded
    }

    pub(super) fn load_progress<'a>(&self, access: &'a ResourceAccess<'a>) -> Option<File<'a>> {
        let path = self.cache_path(access, b"/progress.bin")?;
        let file = self.open_file(access, &path, FileMode::Read)?;
        let mut bytes = [0; 10];
        let size = file.read(&mut bytes);
        if matches!(size, 4 | 6 | 10) {
            if let Some((progress, sentinel)) =
                xp_ui::reader::progress_decode(&bytes[..size as usize])
            {
                self.model(|state| {
                    state.current.spine = i32::from(progress.spine);
                    state.next_page = i32::from(progress.page);
                });
                if sentinel {
                    self.loop_log_parts(
                        0,
                        &[b"Ignoring stale last-page sentinel from progress cache"],
                    );
                }
                self.model(|state| state.cached.spine = state.current.spine);
                self.loop_log(
                    format_args!("Loaded cache: {}, {}", progress.spine, progress.page),
                    &[],
                );
                self.model(|state| {
                    if progress.has_count != 0 {
                        state.cached.pages = i32::from(progress.count);
                    }
                    if progress.has_offset != 0 {
                        state.cached.offset = Some(progress.offset);
                    }
                });
            }
        }
        Some(file)
    }

    pub(super) fn load_book(&self, access: &ResourceAccess<'_>) -> bool {
        let Some(mut pending) = self.book_factory().open(self.path(), self.leaves) else {
            self.loop_log_parts(1, &[b"Failed to allocate EPUB object"]);
            return false;
        };
        let Some(cache) = pending.cache_file() else {
            return false;
        };
        let uncached = unsafe { (self.resources.path_exists)(cache.wire()) } == 0;
        drop(cache);
        if uncached {
            self.disable_fast_initial_refresh();
            unsafe {
                let label = (self.resources.translate)(xp_core::i18n::ids::STR_INDEXING);
                (self.resources.popup_draw)(label);
            }
        }
        // The pending path is moved into the loaded owner, or restored on failure.
        let mut path = unsafe { core::mem::take(&mut *self.path.get()) };
        let result = {
            let _loan = if uncached {
                unsafe { (self.leaves.loan_begin)(self.native(access)) };
                Some(LoadLoan { access })
            } else {
                None
            };
            pending.load(
                &mut path,
                self.resources,
                self.leaves,
                self.runtime.now_ms,
                self.settings(false).embedded_style == 0,
            )
        };
        unsafe { *self.path.get() = path };
        let book = match result {
            Ok(book) => book,
            Err(error) => {
                let error = pending.error(error);
                let bytes = match &error {
                    LoadError::Protection(OpenError::Message(message)) => message.bytes(),
                    _ => b"",
                };
                let failure = match bytes {
                    b"" => LoadFailure::None,
                    b"access expired" => LoadFailure::Expired,
                    b"loan date unverified" => LoadFailure::Unverified,
                    _ => LoadFailure::Protected,
                };
                self.model(|state| state.failure = failure);
                self.loop_log_parts(
                    1,
                    &[
                        b"Failed to load EPUB",
                        if bytes.is_empty() { b"" } else { b": " },
                        bytes,
                    ],
                );
                return false;
            }
        };
        let mut loaded = LoadedBook { handle: Some(book) };
        if !unsafe { super::session_book::open_facade(book, self.leaves) } {
            unsafe { *self.path.get() = super::session_book::take_path(book) };
            self.loop_log_parts(1, &[b"Failed to allocate EPUB object"]);
            return false;
        }
        assert!(self.book(access).is_none());
        self.set_book(access, loaded.handle.take());
        self.model(|state| state.facts.book = true);
        unsafe { super::session_book::setup_cache(book.as_ptr()) };
        // Keep this file alive through all three restoration calls.
        let _progress_file = self.load_progress(access);
        if self.model(|state| state.current.spine == 0) {
            let reference = unsafe { (super::session_book::text_reference)(book.as_ptr()) };
            if reference != 0 {
                self.model(|state| {
                    state.current.spine = reference;
                    state.cached.offset = None;
                });
                self.loop_log(
                    format_args!(
                        "Opened for first time, navigating to text reference at index {reference}"
                    ),
                    &[],
                );
            }
        }
        self.load_links(access);
        self.load_cached_bookmarks(access);
        if !self.load_clippings(book) {
            with_book_text(book, super::session_book::path, |path| {
                self.loop_log_at(
                    1,
                    b"CLIP",
                    &[
                        b"Failed to load clippings for ",
                        unsafe { path.bytes() }.unwrap_or_default(),
                    ],
                );
            });
        }
        true
    }

    pub(super) fn drop_book(&self, access: &ResourceAccess<'_>) {
        let book = self.take_book(access);
        self.model(|state| {
            state.facts.book = false;
            state.facts.spine_count = 0;
        });
        if let Some(book) = book {
            unsafe {
                *self.path.get() = super::session_book::take_path(book);
                super::session_book::close(book)
            };
        }
    }

    pub(super) fn teardown_book(&self, access: &ResourceAccess<'_>) {
        self.set_book_extractor(access, None);
        self.settle_overlay_refresh(access);
        self.discard_overlay_page(access);
        if self.model(|state| state.facts.radio_released) {
            unsafe { (self.leaves.radio_after_paint)() };
        }
        if self.model(|state| state.history.depth > 0) && self.book(access).is_some() {
            self.save_links(access);
        }
        self.drop_section(access);
        unsafe { (self.leaves.clippings_unload)() };
        let paths = if self.model(|state| state.move_to_read) {
            self.book(access).and_then(|book| {
                let source = Text::receive(|sink, context| unsafe {
                    (super::session_book::path)(book.as_ptr(), sink, context)
                })?;
                let cache = self.book_cache_path(book, b"")?;
                let destination = self.read_folder_destination(&source)?;
                Some((source, cache, destination))
            })
        } else {
            None
        };
        self.drop_book(access);
        if let Some((source, cache, destination)) = paths {
            self.move_to_read_folder(&source, &destination, &cache);
        }
    }
}
