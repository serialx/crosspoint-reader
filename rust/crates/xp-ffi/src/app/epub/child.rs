use super::{
    abi, app_abi,
    page::Page,
    selection_metadata::SelectionMetadata,
    state::{ChildIntent, Notice, TimedNotice},
    EpubSession, ResourceAccess, Text,
};
use core::{ffi::c_void, ptr::NonNull};

pub(super) const SELECTION_CLIP: u8 = 0;
const SELECTION_LOOKUP: u8 = 1;
const SELECTION_DEFINITION: u8 = 2;
const PAGE_WINDOW: usize = 3;

struct SelectionPages([Option<Page>; PAGE_WINDOW]);

impl Drop for SelectionPages {
    fn drop(&mut self) {
        for page in self.0.iter_mut().rev() {
            drop(page.take());
        }
    }
}

#[derive(Clone, Copy)]
pub(super) struct SelectionCapture {
    pub(super) spine: u16,
    pub(super) page: i32,
    pub(super) count: u16,
    pub(super) signature: u32,
}

impl EpubSession {
    fn clipping_notice(&self, message: Notice) {
        let since = unsafe { (self.runtime.now_ms)() };
        self.model(|state| state.clipping_notice = Some(TimedNotice { message, since }));
        self.request_update();
    }

    pub(super) fn open_dictionary_word_select(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.dictionary)(hooks.context) };
            return;
        }
        if self.settings(false).dictionary_available == 0 {
            let since = unsafe { (self.runtime.now_ms)() };
            self.model(|state| {
                state.dictionary_notice = Some(TimedNotice {
                    message: Notice::Dictionary,
                    since,
                })
            });
            self.request_update();
        } else {
            self.start_clip_selection(-1, -1, SELECTION_LOOKUP);
        }
    }

    pub(super) fn start_clip_selection(&self, x: i32, y: i32, mode: u8) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.selection)(hooks.context, x, y, mode) };
            return;
        }
        if mode > SELECTION_DEFINITION {
            return;
        }
        let capture = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            if self.section(&access).is_none() || self.book(&access).is_none() {
                return;
            }
            let Some(capture) = self.model(|state| {
                let section = state.facts.section?;
                (state.current.page >= 0 && state.current.page < i32::from(section.pages))
                    .then_some(SelectionCapture {
                        spine: state.current.spine as u16,
                        page: state.current.page,
                        count: section.pages,
                        signature: 0,
                    })
            }) else {
                return;
            };
            if cfg!(feature = "reader-strips") {
                unsafe { (self.leaves.strip_release_idle)(self.native(&access)) };
            }
            capture
        };
        let Some(book) = self.loop_book() else {
            return;
        };
        let mut pages = SelectionPages(core::array::from_fn(|_| None));
        let mut loaded = 0;
        while loaded < PAGE_WINDOW
            && capture.page.wrapping_add(loaded as i32) < i32::from(capture.count)
        {
            let page = {
                let access = self.lock_resources();
                if self.model(|state| {
                    state.scene_gate.failed || state.current.spine != i32::from(capture.spine)
                }) {
                    drop(access);
                    self.request_update();
                    return;
                }
                let Some(section) = self.section(&access) else {
                    drop(access);
                    self.request_update();
                    return;
                };
                let raw = unsafe {
                    (self.leaves.section_load_page)(
                        section.as_ptr(),
                        capture.page.wrapping_add(loaded as i32),
                    )
                };
                self.publish_section(&access);
                unsafe { Page::from_raw(raw, self.leaves) }
            };
            let Some(page) = page else {
                break;
            };
            if loaded == 0 && x >= 0 && self.has_clippings() {
                let access = self.lock_resources();
                if self.model(|state| {
                    state.scene_gate.failed || state.current.spine != i32::from(capture.spine)
                }) || self.section(&access).is_none()
                {
                    drop(access);
                    self.request_update();
                    return;
                }
                let index = self.clipping_at_point(&access, page.handle, x, y);
                if index >= 0 {
                    let removed = unsafe { (self.leaves.clippings_remove)(index as u32) != 0 };
                    if !removed {
                        self.loop_log_formatted(
                            1,
                            b"CLIP",
                            format_args!("Failed to delete highlighted clipping {index}"),
                            &[],
                        );
                    }
                    self.clipping_notice(if removed {
                        Notice::ClipRemoved
                    } else {
                        Notice::ClipDeleteFailed
                    });
                    return;
                }
            }
            pages.0[loaded] = Some(page);
            loaded += 1;
        }
        if loaded == 0 {
            self.loop_log_formatted(
                1,
                b"CLIP",
                format_args!(
                    "Failed to load page {} for clipping selection",
                    capture.page
                ),
                &[],
            );
            self.request_update();
            return;
        }
        if self.model(|state| state.viewport.0 == 0 || state.viewport.1 == 0) {
            self.loop_log_at(
                1,
                b"CLIP",
                &[b"Cannot anchor clipping before the reader viewport is initialized"],
            );
            self.request_update();
            return;
        }
        let capture = SelectionCapture {
            signature: self.clipping_layout_signature(),
            ..capture
        };
        let mut margins = [0; 4];
        unsafe { (self.leaves.render_margins)(margins.as_mut_ptr()) };
        let toc =
            unsafe { (self.leaves.book_toc_for_spine)(book.as_ptr(), i32::from(capture.spine)) };
        let Some(metadata) = SelectionMetadata::capture(self, book, toc) else {
            self.request_update();
            return;
        };
        let margin = i32::from(self.settings(false).screen_margin);
        let args = abi::XpEpubSelectionArgs {
            left: margins[3] + margin,
            top: margins[0] + margin,
            x,
            y,
            mode,
        };
        let mut handles = pages.0.each_ref().map(|page| {
            page.as_ref()
                .map_or(core::ptr::null_mut(), |page| page.handle.as_ptr())
        });
        let child = NonNull::new(unsafe {
            (self.leaves.child_selection)(handles.as_mut_ptr(), loaded as u8, &args)
        });
        let Some(child) = child else {
            self.loop_log_at(
                1,
                b"CLIP",
                &[b"Failed to allocate clipping selection activity"],
            );
            self.request_update();
            return;
        };
        for page in pages.0.iter_mut().take(loaded) {
            if let Some(page) = page.take() {
                core::mem::forget(page);
            }
        }
        // The loop alone owns the pending result payload, including cancellation.
        unsafe { (*self.guarded.get()).selection_metadata = metadata };
        self.push_child(child, ChildIntent::Selection(capture));
    }

    fn result_section(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        if self.model(|state| state.scene_gate.failed) {
            return None;
        }
        self.section(access)
    }

    pub(super) fn selection_result(
        &self,
        capture: SelectionCapture,
        result: Option<&app_abi::XpAppClipResult>,
    ) {
        // Move out before any callback; every return path releases the old capture.
        let metadata = unsafe { core::mem::take(&mut (*self.guarded.get()).selection_metadata) };
        let Some(result) = result else {
            self.request_update();
            return;
        };
        let start = (capture.page as u16).wrapping_add(result.start_page_offset);
        let end = (capture.page as u16).wrapping_add(result.end_page_offset);
        {
            let access = self.lock_resources();
            if let Some(section) = self.result_section(&access) {
                if self.model(|state| state.current.spine == i32::from(capture.spine))
                    && end < unsafe { (self.leaves.section_page_count)(section.as_ptr()) }
                {
                    let offset = self.section_offset(&access, end);
                    self.model(|state| {
                        state.current.page = i32::from(end);
                        state.visible_offset = offset;
                    });
                }
            }
        }
        match result.action {
            2 => {
                self.add_bookmark();
                let since = unsafe { (self.runtime.now_ms)() };
                self.model(|state| {
                    state.bookmark_notice = Some(TimedNotice {
                        message: if state.bookmark_removed {
                            Notice::BookmarkRemoved
                        } else {
                            Notice::BookmarkAdded
                        },
                        since,
                    })
                });
                self.request_update();
            }
            1 => self.open_dictionary_lookup(result.text.bytes),
            _ => {
                let paragraph = {
                    let access = self.lock_resources();
                    self.result_section(&access)
                        .and_then(|_| self.section_paragraph(&access, start))
                        .unwrap_or(u16::MAX)
                };
                let row = abi::XpEpubClipInsert {
                    range: abi::XpEpubClip {
                        spine: capture.spine,
                        start_page: start,
                        end_page: end,
                        page_count: capture.count,
                        start_word: result.start_word,
                        end_word: result.end_word,
                        signature: capture.signature,
                        start_offset: result.start_offset,
                        end_offset: result.end_offset,
                    },
                    word_count: result.word_count,
                    paragraph,
                    chapter: metadata.chapter(),
                    text: result.text,
                };
                let added = {
                    let _access = self.lock_resources();
                    // A confirmed result survives scene failure; the store has one loop writer.
                    unsafe { (self.leaves.clippings_insert)(&row) }
                };
                let exported = added == 0
                    && unsafe {
                        (self.leaves.clippings_export)(
                            metadata.title(),
                            metadata.author(),
                            metadata.chapter(),
                            i32::from(start) + 1,
                            result.text,
                        ) != 0
                    };
                let message = if added == 1 {
                    Notice::ClipLimit
                } else if exported {
                    Notice::ClipSaved
                } else if added == 0 {
                    Notice::ClipExportFailed
                } else {
                    Notice::ClipFailed
                };
                self.clipping_notice(message);
            }
        }
    }

    fn open_dictionary_lookup(&self, initial: app_abi::Text) {
        let page = {
            let access = self.lock_resources();
            let section = self.result_section(&access);
            let raw = section.map_or(core::ptr::null_mut(), |section| unsafe {
                (self.leaves.section_load_page)(
                    section.as_ptr(),
                    self.model(|state| state.current.page),
                )
            });
            if section.is_some() {
                self.publish_section(&access);
            }
            unsafe { Page::from_raw(raw, self.leaves) }
        };
        let Some(page) = page else {
            self.loop_log_at(1, b"CLIP", &[b"Failed to load lookup page"]);
            self.request_update();
            return;
        };
        let mut margins = [0; 4];
        unsafe { (self.leaves.render_margins)(margins.as_mut_ptr()) };
        let margin = i32::from(self.settings(false).screen_margin);
        let child = NonNull::new(unsafe {
            (self.leaves.child_dictionary)(
                page.handle.as_ptr(),
                margins[3] + margin,
                margins[0] + margin,
                initial,
            )
        });
        let Some(child) = child else {
            self.loop_log_at(1, b"CLIP", &[b"OOM: dictionary lookup activity"]);
            self.request_update();
            return;
        };
        core::mem::forget(page);
        self.push_child(child, ChildIntent::Dictionary);
    }

    pub(super) fn dictionary_result(&self, cancelled: bool) {
        if !cancelled {
            self.open_dictionary_word_select();
        }
    }
}

impl EpubSession {
    pub(super) fn open_footnotes(&self, reopen_menu: bool) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.footnotes)(hooks.context, u8::from(reopen_menu)) };
            return;
        }
        enum Open {
            Href(Text),
            Page(Page),
        }
        let action = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            let Some(section) = self.section(&access) else {
                return;
            };
            if unsafe { (self.leaves.metadata_footnotes)(self.native(&access)) } == 0 {
                return;
            }
            if unsafe { (self.leaves.metadata_footnotes)(self.native(&access)) } == 1 {
                struct Sink {
                    sink: app_abi::TextSink,
                    context: *mut c_void,
                }
                unsafe extern "C" fn href(
                    context: *mut c_void,
                    row: *const abi::XpEpubFootnote,
                ) -> u8 {
                    let capture = &*context.cast::<Sink>();
                    let Some(row) = row.as_ref() else {
                        return 0;
                    };
                    (capture.sink)(capture.context, row.href)
                }
                let Some(href) = Text::receive(|sink, context| {
                    let mut capture = Sink { sink, context };
                    unsafe {
                        (self.leaves.metadata_footnote)(
                            self.native(&access),
                            0,
                            href,
                            (&mut capture as *mut Sink).cast(),
                        )
                    }
                }) else {
                    return;
                };
                Open::Href(href)
            } else {
                let page = self.model(|state| state.current.page);
                let raw = unsafe { (self.leaves.section_load_page)(section.as_ptr(), page) };
                self.publish_section(&access);
                let Some(page) = (unsafe { Page::from_raw(raw, self.leaves) }) else {
                    return;
                };
                Open::Page(page)
            }
        };
        match action {
            Open::Href(href) => self.navigate_href(href.bytes(), true),
            Open::Page(page) => {
                let mut margins = [0; 4];
                unsafe { (self.leaves.render_margins)(margins.as_mut_ptr()) };
                let margin = i32::from(self.settings(false).screen_margin);
                let child = NonNull::new(unsafe {
                    (self.leaves.child_footnote)(
                        page.handle.as_ptr(),
                        margins[3] + margin,
                        margins[0] + margin,
                    )
                });
                let Some(child) = child else {
                    self.loop_log_at(1, b"ERA", &[b"OOM: EpubReaderFootnoteSelectActivity"]);
                    return;
                };
                core::mem::forget(page);
                self.push_child(child, ChildIntent::Footnote { reopen_menu });
            }
        }
    }

    pub(super) fn footnote_result(&self, reopen_menu: bool, href: Option<&[u8]>) {
        if let Some(href) = href {
            self.navigate_href(href, true);
            self.request_update();
        } else if reopen_menu {
            self.open_reader_menu();
        } else {
            self.request_update();
        }
    }

    pub(super) fn open_clippings(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.clippings)(hooks.context) };
            return;
        }
        if unsafe { (self.leaves.clippings_count)() } == 0 {
            self.request_update();
            return;
        }
        let child = NonNull::new(unsafe { (self.leaves.child_clippings)() });
        let Some(child) = child else {
            self.loop_log_at(1, b"CLIP", &[b"OOM: clipping list"]);
            return;
        };
        self.push_child(child, ChildIntent::Clippings);
    }

    pub(super) fn clippings_result(&self, progress: Option<&app_abi::XpAppProgressResult>) {
        if let Some(progress) = progress {
            self.apply_progress(progress);
        } else {
            self.request_update();
        }
    }
}
