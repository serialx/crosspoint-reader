use super::{
    app_abi,
    state::{ChildIntent, Overlay},
    EpubSession, Text,
};

impl EpubSession {
    fn menu_result(&self, result: &app_abi::XpAppMenuResult, cancelled: bool) {
        if self.settings(false).orientation != result.orientation {
            self.apply_orientation(result.orientation);
        }
        self.toggle_auto_page_turn(result.page_turn_option);
        if !cancelled {
            self.confirm_reader_menu(result.action);
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
        let access = self.lock_resources();
        self.clear_deferred();
        self.model(|state| {
            state.current.spine = chapter.spine;
            state.next_page = 0;
        });
        unsafe { (*self.guarded(&access)).anchor = anchor };
        self.drop_section(&access);
        self.request_update();
    }

    fn text_settings_result(&self, from_overlay: bool) {
        if from_overlay {
            self.apply_text_settings();
            self.model(|state| {
                state.overlay = Overlay::Text;
                state.panel_index = 0;
            });
            if let Some(toolbar) = self.loop_toolbar() {
                unsafe { (self.leaves.toolbar_begin)(toolbar.as_ptr()) };
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
    pub(super) unsafe fn child_result(&self, result: Option<&app_abi::XpAppResultView>) {
        let intent = self.model(|state| core::mem::take(&mut state.child));
        let cancelled = result.is_none_or(|result| result.cancelled != 0);
        match intent {
            ChildIntent::None => {}
            ChildIntent::Menu => {
                if let Some(result) = result.filter(|result| result.kind == 3) {
                    self.menu_result(&result.value.menu, cancelled);
                } else {
                    self.request_update();
                }
            }
            ChildIntent::Chapter => self.chapter_result(
                result
                    .filter(|result| !cancelled && result.kind == 4)
                    .map(|result| &result.value.chapter),
            ),
            ChildIntent::Percent => {
                if let Some(result) = result.filter(|result| !cancelled && result.kind == 5) {
                    self.jump_to_percent(result.value.signed_value);
                } else {
                    self.open_reader_menu();
                }
            }
            ChildIntent::TextSettings { from_overlay } => self.text_settings_result(from_overlay),
            ChildIntent::Footnote { reopen_menu } => {
                let href = result
                    .filter(|result| !cancelled && result.kind == 11)
                    .and_then(|result| result.value.text.bytes());
                self.footnote_result(reopen_menu, href);
            }
            ChildIntent::Selection(capture) => self.selection_result(
                capture,
                result
                    .filter(|result| !cancelled && result.kind == 8)
                    .map(|result| &result.value.clipping),
            ),
            ChildIntent::Dictionary => self.dictionary_result(cancelled),
            ChildIntent::Bookmarks => {
                {
                    let access = self.lock_resources();
                    self.load_cached_bookmarks(&access);
                }
                if let Some(result) = result.filter(|result| !cancelled && result.kind == 9) {
                    self.apply_progress(&result.value.progress);
                } else {
                    self.open_reader_menu();
                }
            }
            ChildIntent::Clippings => self.clippings_result(
                result
                    .filter(|result| !cancelled && result.kind == 9)
                    .map(|result| &result.value.progress),
            ),
            ChildIntent::Qr | ChildIntent::Reopen => self.open_reader_menu(),
            ChildIntent::Wifi => self.loan_wifi_result(cancelled),
        }
    }
}
