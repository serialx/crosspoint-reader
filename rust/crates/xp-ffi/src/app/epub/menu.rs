use super::{
    abi,
    book::with_book_text,
    state::{ChildIntent, Overlay},
    EpubSession, Text,
};
use core::ptr::NonNull;
use xp_ui::reader::{input::Query, menu_parent::Action};

#[derive(Clone, Copy)]
pub(super) enum MenuOrigin {
    Direct,
    ChildResult,
}

impl EpubSession {
    pub(super) fn open_reader_menu(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            unsafe { (hooks.menu)(hooks.context) };
            return;
        }
        self.model(|state| state.manual_turn = 0);
        if self.uses_toolbar_menu() {
            let cursor = self.mapped_query(Query::HasTouch) == 0;
            let toolbar = if let Some(toolbar) = self.loop_toolbar() {
                toolbar
            } else {
                let Some(toolbar) = NonNull::new(unsafe { (self.leaves.toolbar_open)() }) else {
                    self.request_update();
                    return;
                };
                let access = self.lock_resources();
                if self.model(|state| state.scene_gate.failed) {
                    unsafe { (self.leaves.toolbar_close)(toolbar.as_ptr()) };
                    drop(access);
                    self.request_update();
                    return;
                }
                self.set_toolbar(&access, Some(toolbar));
                toolbar
            };
            self.model(|state| {
                state.overlay = Overlay::Toolbar;
                state.focused_tool = 0;
                state.panel_hold_jumped = false;
                state.panel_cursor = cursor;
            });
            unsafe { (self.leaves.toolbar_begin)(toolbar.as_ptr()) };
            {
                let access = self.lock_resources();
                self.discard_overlay_page(&access);
            }
            self.request_update();
            return;
        }
        let position = self.chapter_position();
        let percent = self.book_percent_for(position);
        let footnotes = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            unsafe { (self.leaves.metadata_footnotes)(self.native(&access)) != 0 }
        };
        let Some(book) = self.loop_book() else { return };
        with_book_text(book, self.leaves.book_title, |title| {
            let bookmarks = self.loop_bookmark_handle().is_some_and(|bookmarks| unsafe {
                (self.leaves.bookmarks_count)(bookmarks.as_ptr()) != 0
            });
            let args = abi::XpEpubMenuArgs {
                title,
                display_page: position.display_page(),
                pages: position.count,
                percent,
                orientation: self.settings(false).orientation,
                footnotes: u8::from(footnotes),
                bookmarks: u8::from(bookmarks),
                clippings: u8::from(self.has_clippings()),
            };
            self.launch_menu_child(
                unsafe { (self.leaves.child_menu)(&args) },
                ChildIntent::Menu,
            );
        });
    }

    fn launch_menu_child(&self, child: *mut core::ffi::c_void, intent: ChildIntent) {
        if let Some(child) = NonNull::new(child) {
            self.push_child(child, intent);
        } else {
            self.request_update();
        }
    }

    fn open_chapters(&self) {
        let toc = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            let toc = self.current_toc_index(&access);
            self.cache_current_position(&access);
            self.drop_section(&access);
            toc
        };
        if let Some(book) = self.loop_book() {
            self.launch_menu_child(
                unsafe { (self.leaves.child_chapter)(book.as_ptr(), toc) },
                ChildIntent::Chapter,
            );
        }
    }

    pub(super) fn open_text_settings(&self, from_overlay: bool) {
        self.launch_menu_child(
            unsafe { (self.leaves.child_text_settings)() },
            ChildIntent::TextSettings { from_overlay },
        );
    }

    fn open_percent(&self) {
        let percent = self.book_percent_for(self.chapter_position());
        self.launch_menu_child(
            unsafe { (self.leaves.child_percent)(percent) },
            ChildIntent::Percent,
        );
    }

    fn open_qr(&self) {
        let text = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                None
            } else {
                self.section(&access).and_then(|section| {
                    let page = self.model(|state| state.current.page);
                    if page < 0
                        || page
                            >= i32::from(unsafe {
                                (self.leaves.section_page_count)(section.as_ptr())
                            })
                    {
                        return None;
                    }
                    Text::receive(|sink, context| unsafe {
                        (self.leaves.section_page_text)(section.as_ptr(), page, sink, context)
                    })
                })
            }
        };
        if let Some(text) = text.filter(|text| !text.bytes().is_empty()) {
            self.launch_menu_child(
                unsafe { (self.leaves.child_qr)(text.wire()) },
                ChildIntent::Qr,
            );
        } else {
            self.request_update();
        }
    }

    fn clear_book_cache(&self) {
        {
            let access = self.lock_resources();
            if let (Some(book), Some(section)) = (self.book(&access), self.section(&access)) {
                if !self.model(|state| state.scene_gate.failed) {
                    let (spine, page) =
                        self.model(|state| (state.current.spine as u16, state.current.page as u16));
                    let count = unsafe { (self.leaves.section_page_count)(section.as_ptr()) };
                    self.drop_section(&access);
                    unsafe {
                        (self.leaves.book_clear_cache)(book.as_ptr());
                        (self.leaves.book_setup_cache)(book.as_ptr());
                    }
                    if !self.save_progress(
                        &access,
                        i32::from(spine),
                        i32::from(page),
                        i32::from(count),
                    ) {
                        self.loop_log_at(
                            1,
                            b"ERS",
                            &[b"Failed to save progress before cache clear"],
                        );
                    }
                }
            }
        }
        self.go_home();
    }

    fn open_bookmarks(&self) {
        let Some(book) = self.loop_book() else { return };
        with_book_text(book, self.leaves.book_path, |path| {
            self.launch_menu_child(
                unsafe { (self.leaves.child_bookmarks)(book.as_ptr(), path) },
                ChildIntent::Bookmarks,
            );
        });
    }

    pub(super) fn confirm_reader_menu(&self, action: i32, origin: MenuOrigin) {
        if cfg!(feature = "reader-strips") {
            let access = self.lock_resources();
            if matches!(origin, MenuOrigin::Direct) && self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            unsafe { (self.leaves.strip_release_idle)(self.native(&access)) };
        }
        match action {
            x if x == Action::Chapter as i32 => self.open_chapters(),
            x if x == Action::Footnotes as i32 => self.open_footnotes(true),
            x if x == Action::Text as i32 => self.open_text_settings(false),
            x if x == Action::Percent as i32 => self.open_percent(),
            x if x == Action::Dictionary as i32 => self.open_dictionary_word_select(),
            x if x == Action::Qr as i32 => self.open_qr(),
            x if x == Action::Home as i32 => self.go_home(),
            x if x == Action::DeleteCache as i32 => self.clear_book_cache(),
            x if x == Action::Screenshot as i32 => {
                {
                    let access = self.lock_resources();
                    if matches!(origin, MenuOrigin::Direct)
                        && self.model(|state| state.scene_gate.failed)
                    {
                        drop(access);
                        self.request_update();
                        return;
                    }
                    self.model(|state| state.screenshot = true);
                }
                self.request_update();
            }
            x if x == Action::Sync as i32 => self.open_sync(),
            x if x == Action::Bookmarks as i32 => self.open_bookmarks(),
            x if x == Action::SaveClipping as i32 => {
                self.start_clip_selection(-1, -1, super::child::SELECTION_CLIP)
            }
            x if x == Action::ViewClippings as i32 => self.open_clippings(),
            x if x == Action::ToggleBookmark as i32 => self.add_bookmark(),
            _ => {}
        }
    }
}
