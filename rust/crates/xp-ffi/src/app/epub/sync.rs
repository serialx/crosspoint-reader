use super::{abi, app_abi, EpubSession, ResourceAccess, Text};
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::render::Query;

struct SavedProgress {
    xpath: Text,
    percentage: f32,
}

impl EpubSession {
    fn sync_heap_log(&self, access: &ResourceAccess<'_>, before: bool) {
        if unsafe { (self.leaves.log_enabled)(0) } == 0 {
            return;
        }
        let services = unsafe { (self.leaves.render_services)(self.native(access)).as_ref() }
            .expect("physical renderer services");
        let heap = unsafe {
            services.read.expect("physical renderer query")(services.context, Query::FreeHeap as u8)
        };
        if before {
            self.loop_log_formatted(
                0,
                b"KOSync",
                format_args!("Releasing epub for sync (heap before: {heap})"),
                &[],
            );
        } else {
            self.loop_log_formatted(
                0,
                b"KOSync",
                format_args!("Epub released (heap after: {heap})"),
                &[],
            );
        }
    }

    fn sync_chapter(&self, book: NonNull<c_void>, toc: i32) -> Option<Text> {
        if toc < 0 {
            return Some(Text::default());
        }
        struct Sink {
            sink: app_abi::TextSink,
            context: *mut c_void,
        }
        unsafe extern "C" fn title(context: *mut c_void, row: *const abi::XpEpubToc) -> u8 {
            let capture = &*context.cast::<Sink>();
            let Some(row) = row.as_ref() else { return 0 };
            (capture.sink)(capture.context, row.title)
        }
        Text::receive(|sink, context| {
            let mut capture = Sink { sink, context };
            unsafe {
                (self.leaves.book_toc)(
                    book.as_ptr(),
                    toc,
                    title,
                    (&mut capture as *mut Sink).cast(),
                )
            }
        })
    }

    fn sync_saved(
        &self,
        access: &ResourceAccess<'_>,
        book: NonNull<c_void>,
        position: &abi::XpEpubPosition,
    ) -> Option<SavedProgress> {
        unsafe extern "C" fn capture(
            context: *mut c_void,
            row: *const abi::XpEpubSavedProgress,
        ) -> u8 {
            let Some(row) = row.as_ref() else { return 0 };
            let Some(bytes) = row.xpath.bytes() else {
                return 0;
            };
            let Ok(xpath) = Text::copy(bytes) else {
                return 0;
            };
            *context.cast::<Option<SavedProgress>>() = Some(SavedProgress {
                xpath,
                percentage: row.percentage,
            });
            1
        }
        let mut saved: Option<SavedProgress> = None;
        unsafe {
            (self.leaves.loan_begin)(self.native(access));
            (self.leaves.progress_to_saved)(
                book.as_ptr(),
                position,
                capture,
                (&mut saved as *mut Option<SavedProgress>).cast(),
            );
            (self.leaves.loan_drop)(self.native(access));
        }
        saved
    }

    fn replace_with_sync(
        &self,
        path: app_abi::Text,
        position: &abi::XpEpubPosition,
        saved: &abi::XpEpubSavedProgress,
        chapter: app_abi::Text,
        events_only: bool,
    ) {
        let child = NonNull::new(unsafe {
            (self.leaves.child_sync)(path, position, saved, chapter, u8::from(events_only))
        });
        if let Some(child) = child {
            self.replace_child(child);
        } else if events_only {
            self.request_update();
        } else {
            self.loop_log_at(1, b"KOSync", &[b"OOM: KOReaderSyncActivity"]);
            self.finish();
        }
    }

    pub(super) fn open_sync(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.sync)(hooks.context) };
            return;
        }
        self.flush_session();
        let Some(book) = self.loop_book() else { return };
        if unsafe { (self.leaves.has_sync_credentials)() } == 0 {
            // The constructor copies the borrowed path before the sink returns.
            super::book::with_book_text(book, self.leaves.book_path, |path| {
                let position = self.loop_current_position();
                let saved = abi::XpEpubSavedProgress {
                    xpath: app_abi::Text::from_bytes(&b"\0"[..0]),
                    percentage: 0.0,
                };
                self.replace_with_sync(
                    path,
                    &position,
                    &saved,
                    app_abi::Text::from_bytes(&b"\0"[..0]),
                    true,
                );
            });
            return;
        }
        let access = self.lock_resources();
        let page = self.model(|state| {
            if state.facts.section.is_some() {
                state.current.page
            } else {
                state.next_page
            }
        });
        let count = self
            .estimated_pages()
            .unwrap_or_else(|| self.model(|state| state.cached.pages));
        let position = self.current_position(&access);
        let toc = unsafe {
            (self.leaves.book_toc_for_spine)(book.as_ptr(), self.model(|state| state.current.spine))
        };
        let Some(chapter) = self.sync_chapter(book, toc) else {
            self.request_update();
            return;
        };
        let Some(path) = Text::receive(|sink, context| unsafe {
            (self.leaves.book_path)(book.as_ptr(), sink, context)
        }) else {
            self.request_update();
            return;
        };
        let spine = self.model(|state| state.current.spine);
        if !self.save_progress(&access, spine, page, count) {
            self.loop_log_at(
                1,
                b"KOSync",
                &[b"Aborting sync because current progress could not be saved"],
            );
            self.model(|state| state.sync_save_error = true);
            self.request_update();
            return;
        }
        self.sync_heap_log(&access, true);
        self.model(|state| {
            if state.facts.section.is_some() {
                state.next_page = state.current.page;
            }
        });
        self.discard_overlay_page(&access);
        unsafe {
            (self.leaves.image_release_cache)();
            (self.leaves.book_clear_extractor)();
        }
        self.drop_section(&access);
        unsafe { (self.leaves.fonts_release_caches)() };
        let Some(saved) = self.sync_saved(&access, book, &position) else {
            self.request_update();
            return;
        };
        if self.model(|state| state.history.depth > 0) {
            self.save_links(&access);
        }
        self.drop_book(&access);
        self.sync_heap_log(&access, false);
        let wire = abi::XpEpubSavedProgress {
            xpath: saved.xpath.wire(),
            percentage: saved.percentage,
        };
        self.replace_with_sync(path.wire(), &position, &wire, chapter.wire(), false);
    }
}
