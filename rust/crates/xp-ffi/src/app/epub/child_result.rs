use super::{
    app_abi,
    children::ReaderChildResult,
    state::{ChildIntent, Overlay},
    EpubSession, Text,
};

impl EpubSession {
    fn menu_result(&self, action: Option<i32>, orientation: u8, page_turn: u8) {
        if self.settings(false).orientation != orientation {
            self.apply_orientation(orientation);
        }
        self.toggle_auto_page_turn(page_turn);
        if let Some(action) = action {
            self.confirm_reader_menu(action, super::menu::MenuOrigin::ChildResult);
        }
    }

    fn chapter_result(&self, chapter: Option<&app_abi::XpAppChapterResult>) {
        let Some(chapter) = chapter else {
            self.open_reader_menu();
            return;
        };
        let Some(bytes) = (unsafe { chapter.anchor.bytes() }) else {
            return;
        };
        let Ok(anchor) = Text::copy(bytes) else {
            self.request_update();
            return;
        };
        self.apply_chapter_result(chapter.spine, anchor);
    }

    fn apply_chapter_result(&self, spine: i32, anchor: Text) {
        let access = self.lock_resources();
        self.clear_deferred();
        self.model(|state| {
            state.current.spine = spine;
            state.next_page = 0;
        });
        self.set_anchor(&access, anchor);
        self.drop_section(&access);
        self.request_update();
    }

    pub(in crate::app) fn deliver_reader_child(&self, result: ReaderChildResult) {
        let intent = self.model(|state| core::mem::take(&mut state.child));
        match (intent, result) {
            (
                ChildIntent::Menu,
                ReaderChildResult::Menu {
                    action,
                    orientation,
                    page_turn,
                },
            ) => {
                self.menu_result(action.map(|action| action as i32), orientation, page_turn);
            }
            (ChildIntent::Chapter, ReaderChildResult::Chapter(Some(chapter))) => {
                self.apply_chapter_result(chapter.spine, chapter.anchor);
            }
            (ChildIntent::Percent, ReaderChildResult::Percent(Some(value))) => {
                self.jump_to_percent(i32::from(value));
            }
            (ChildIntent::Footnote { reopen_menu }, ReaderChildResult::Footnote(href)) => {
                self.footnote_result(reopen_menu, href.as_ref().map(Text::bytes));
            }
            (ChildIntent::Dictionary, ReaderChildResult::LookupClosed { cancelled }) => {
                self.dictionary_result(cancelled)
            }
            (ChildIntent::Clippings, ReaderChildResult::Clippings(Some(position))) => {
                self.apply_progress(&app_abi::XpAppProgressResult {
                    spine: position.spine,
                    page: position.page,
                    pages: 0,
                    xpath: app_abi::Text::from_terminated(b"\0"),
                    percentage: 0.0,
                    offset: position.offset,
                    has_saved_progress: 0,
                    has_offset: 1,
                });
            }
            (ChildIntent::Chapter, ReaderChildResult::Chapter(None))
            | (ChildIntent::Percent, ReaderChildResult::Percent(None))
            | (ChildIntent::Qr, ReaderChildResult::QrClosed) => self.open_reader_menu(),
            _ => self.request_update(),
        }
    }

    fn text_settings_result(&self, from_overlay: bool) {
        if from_overlay {
            self.apply_text_settings();
            self.model(|state| {
                state.overlay = Overlay::Text;
                state.panel_index = 0;
            });
            if let Some(toolbar) = self.loop_toolbar() {
                toolbar.begin(self);
            }
            self.request_update();
        } else {
            {
                let access = self.lock_resources();
                self.cache_current_position(&access);
                self.drop_section(&access);
            }
            self.open_reader_menu();
        }
    }

    // The native result and its strings are borrowed only until this call returns.
    #[inline(never)]
    pub(in crate::app) unsafe fn child_result(&self, result: Option<&app_abi::XpAppResultView>) {
        let intent = self.model(|state| core::mem::take(&mut state.child));
        let cancelled = result.is_none_or(|result| result.cancelled != 0);
        match intent {
            ChildIntent::None => {}
            ChildIntent::Menu => {
                if let Some(result) = result.filter(|result| result.kind == app_abi::RESULT_MENU) {
                    let menu = &result.value.menu;
                    self.menu_result(
                        (!cancelled).then_some(menu.action),
                        menu.orientation,
                        menu.page_turn_option,
                    );
                } else {
                    self.request_update();
                }
            }
            ChildIntent::Chapter => self.chapter_result(
                result
                    .filter(|result| !cancelled && result.kind == app_abi::RESULT_CHAPTER)
                    .map(|result| &result.value.chapter),
            ),
            ChildIntent::Percent => {
                if let Some(result) =
                    result.filter(|result| !cancelled && result.kind == app_abi::RESULT_PERCENT)
                {
                    self.jump_to_percent(result.value.signed_value);
                } else {
                    self.open_reader_menu();
                }
            }
            ChildIntent::TextSettings { from_overlay } => self.text_settings_result(from_overlay),
            ChildIntent::Footnote { reopen_menu } => {
                let href = result
                    .filter(|result| !cancelled && result.kind == app_abi::RESULT_FOOTNOTE)
                    .and_then(|result| result.value.text.bytes());
                self.footnote_result(reopen_menu, href);
            }
            ChildIntent::Selection(capture) => self.selection_result(
                capture,
                result
                    .filter(|result| !cancelled && result.kind == app_abi::RESULT_CLIPPING)
                    .map(|result| &result.value.clipping),
            ),
            ChildIntent::Dictionary => self.dictionary_result(cancelled),
            ChildIntent::Bookmarks => {
                {
                    let access = self.lock_resources();
                    self.load_cached_bookmarks(&access);
                }
                if let Some(result) =
                    result.filter(|result| !cancelled && result.kind == app_abi::RESULT_PROGRESS)
                {
                    self.apply_progress(&result.value.progress);
                } else {
                    self.open_reader_menu();
                }
            }
            ChildIntent::Clippings => self.clippings_result(
                result
                    .filter(|result| !cancelled && result.kind == app_abi::RESULT_PROGRESS)
                    .map(|result| &result.value.progress),
            ),
            ChildIntent::Qr => self.open_reader_menu(),
            ChildIntent::Wifi => self.loan_wifi_result(cancelled),
        }
    }
}
