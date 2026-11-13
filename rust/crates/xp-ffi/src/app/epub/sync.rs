use super::render_resources::physical::Query;
use super::{abi, app_abi, EpubSession, ResourceAccess, Text};
use core::{ffi::c_void, ptr::NonNull};

struct SavedProgress {
    xpath: Text,
    percentage: f32,
}

impl EpubSession {
    fn sync_heap_log(&self, access: &ResourceAccess<'_>, before: bool) {
        if unsafe { (self.leaves.log_enabled)(0) } == 0 {
            return;
        }
        self.check_resources(access);
        let heap = self.physical_query(access, Query::FreeHeap);
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
                (super::session_book::toc)(
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
        self.check_resources(access);

        let loan = super::section_owner::FramebufferLoan::new(
            self.leaves.framebuffer_lend,
            self.leaves.framebuffer_restore,
        );

        #[cfg(feature = "app-test")]
        {
            let mut saved = None;
            if unsafe {
                super::test_support::legacy_progress::saved(book, position, |row| {
                    let Some(bytes) = row.xpath.bytes() else {
                        return 0;
                    };
                    let Ok(xpath) = Text::copy(bytes) else {
                        return 0;
                    };
                    saved = Some(SavedProgress {
                        xpath,
                        percentage: row.percentage,
                    });
                    1
                })
            }
            .is_some()
            {
                return saved;
            }
        }
        let reader = unsafe { super::session_book::reader_pointer(book) }?;
        let saved = unsafe { self.saved_progress(access, reader, position) };
        drop(loan);
        saved.map(|saved| SavedProgress {
            xpath: saved.xpath,
            percentage: saved.percentage,
        })
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
            super::book::with_book_text(book, super::session_book::path, |path| {
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
            (super::session_book::toc_for_spine)(
                book.as_ptr(),
                self.model(|state| state.current.spine),
            )
        };
        let chapter = toc.and_then(|toc| self.sync_chapter(book, toc));
        let path = chapter.as_ref().and_then(|_| {
            Text::receive(|sink, context| unsafe {
                (super::session_book::path)(book.as_ptr(), sink, context)
            })
        });
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
        // Save the true local position even when optional outbound metadata is unreadable.
        let (Some(chapter), Some(path)) = (chapter, path) else {
            self.request_update();
            return;
        };
        self.sync_heap_log(&access, true);
        self.model(|state| {
            if state.facts.section.is_some() {
                state.next_page = state.current.page;
            }
        });
        self.discard_overlay_page(&access);
        unsafe {
            #[cfg(feature = "app-test")]
            (self.leaves.image_release_cache)();
            #[cfg(not(feature = "app-test"))]
            crate::assets::xp_assets_image_reset(true);
            self.set_book_extractor(&access, None);
        }
        self.drop_section(&access);
        unsafe { self.leaves.font_release(self.resources.font_owner) };
        let Some(saved) = self.sync_saved(&access, book, &position) else {
            self.set_book_extractor(&access, Some(book));
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
