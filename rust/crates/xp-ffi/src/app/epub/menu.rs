use super::session_section::Section as ReaderSection;
use super::{
    abi,
    book::with_book_text,
    state::{ChildIntent, Overlay},
    EpubSession,
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
                let Some(toolbar) = super::children::ToolbarSlot::open(self) else {
                    self.request_update();
                    return;
                };
                let access = self.lock_resources();
                if self.model(|state| state.scene_gate.failed) {
                    toolbar.close(self);
                    drop(access);
                    self.request_update();
                    return;
                }
                self.install_toolbar(&access, toolbar)
            };
            self.model(|state| {
                state.overlay = Overlay::Toolbar;
                state.focused_tool = 0;
                state.panel_hold_jumped = false;
                state.panel_cursor = cursor;
            });
            toolbar.begin(self);
            {
                let access = self.lock_resources();
                self.discard_overlay_page(&access);
            }
            self.request_update();
            return;
        }
        let position = self.chapter_position();
        let percent = self.book_percent_for(position);
        if percent < 0 {
            return;
        }
        let footnotes = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            self.metadata_footnotes(&access) != 0
        };
        let Some(book) = self.loop_book() else { return };
        with_book_text(book, super::session_book::title, |title| {
            let bookmarks = self.loop_bookmark_count() != 0;
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
            #[cfg(feature = "app-test")]
            if let Some(legacy) = self.legacy_children.get() {
                self.launch_menu_child(unsafe { (legacy.child_menu)(&args) }, ChildIntent::Menu);
                return;
            }
            self.launch_owned_menu(&args);
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
        if let Some(_book) = self.loop_book() {
            #[cfg(feature = "app-test")]
            if let Some(legacy) = self.legacy_children.get() {
                self.launch_menu_child(
                    unsafe {
                        (legacy.child_chapter)(super::session_book::facade(_book.as_ptr()), toc)
                    },
                    ChildIntent::Chapter,
                );
                return;
            }
            self.launch_owned_chapters(toc);
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
        if percent < 0 {
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(legacy) = self.legacy_children.get() {
            self.launch_menu_child(
                unsafe { (legacy.child_percent)(percent) },
                ChildIntent::Percent,
            );
            return;
        }
        self.launch_owned_percent(percent);
    }

    fn open_qr(&self) {
        let text = {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                None
            } else {
                self.section(&access).and_then(|section| {
                    let page = self.model(|state| state.current.page);
                    if page < 0 || page >= i32::from(unsafe { ReaderSection::page_count(section) })
                    {
                        return None;
                    }
                    unsafe { self.page_text(section, page) }
                })
            }
        };
        if let Some(text) = text.filter(|text| !text.bytes().is_empty()) {
            #[cfg(feature = "app-test")]
            if let Some(legacy) = self.legacy_children.get() {
                self.launch_menu_child(unsafe { (legacy.child_qr)(text.wire()) }, ChildIntent::Qr);
                return;
            }
            self.launch_owned_qr(text);
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
                    let count = unsafe { ReaderSection::page_count(section) };
                    self.drop_section(&access);
                    unsafe {
                        (super::session_book::clear_cache)(book.as_ptr());
                        (super::session_book::setup_cache)(book.as_ptr());
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
        if self.loop_book().is_some() {
            #[cfg(feature = "app-test")]
            if let Some(hooks) = self.menu_hooks {
                self.model(|state| state.child = ChildIntent::Bookmarks);
                unsafe { (hooks.bookmarks)(hooks.context) };
                return;
            }
            self.launch_owned_bookmarks();
        }
    }

    pub(super) fn confirm_reader_menu(&self, action: i32, origin: MenuOrigin) {
        if cfg!(feature = "reader-strips") {
            let access = self.lock_resources();
            if matches!(origin, MenuOrigin::Direct) && self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            self.release_idle_strips(&access);
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
