use super::{
    book::with_book_text,
    child::SELECTION_CLIP,
    read_folder::is_in_read_folder,
    state::{Notice, Overlay, State, TimedNotice},
    EpubSession,
};
use xp_ui::{
    model::{BACK, CONFIRM, DOWN, POWER},
    reader::{
        input::{ButtonQuery, Query},
        values, GO_HOME_MS, SKIP_HOLD_MS,
    },
};

impl EpubSession {
    fn loop_now(&self) -> u32 {
        unsafe { (self.runtime.now_ms)() }
    }
    fn loop_busy(&self) -> bool {
        unsafe { (self.leaves.render_busy)() != 0 }
    }
    fn loop_has_section(&self) -> bool {
        self.model(|state| state.facts.section.is_some())
    }
    fn loop_released(&self, button: u32) -> bool {
        self.mapped_button(button, ButtonQuery::Release, 0)
    }
    fn loop_haptic(&self, extended: bool) {
        unsafe { (self.runtime.touch_feedback)(u8::from(extended)) };
    }
    fn loop_expire(&self, notice: fn(&mut State) -> &mut Option<TimedNotice>) {
        let Some(since) = self.model(|state| notice(state).map(|notice| notice.since)) else {
            return;
        };
        if self.loop_now().wrapping_sub(since) >= 2500 {
            self.model(|state| *notice(state) = None);
            self.request_update();
        }
    }
    fn loop_bookmark(&self) {
        self.add_bookmark();
        let since = self.loop_now();
        self.model(|state| {
            let message = if state.bookmark_removed {
                Notice::BookmarkRemoved
            } else {
                Notice::BookmarkAdded
            };
            state.bookmark_notice = Some(TimedNotice { message, since });
        });
        self.request_update();
    }
    fn loop_sync(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return unsafe { (hooks.sync)(hooks.context) != 0 };
        }
        self.open_sync();
        true
    }
    fn loop_footnotes(&self) {
        if self.model(|state| state.history.depth > 0) {
            self.restore_position();
        } else {
            self.open_footnotes(false);
        }
    }
    fn loop_menu(&self, clear_turn: bool) {
        if self.uses_toolbar_menu() && self.loop_has_section() {
            if clear_turn {
                self.model(|state| state.manual_turn = 0);
            }
            self.open_overlay(Overlay::Toolbar);
        } else {
            self.open_reader_menu();
        }
    }

    pub(super) fn loop_reader(&self) {
        if self.try_idle_loop() || self.consume_rejected_input() {
            return;
        }
        let popup = unsafe { (*self.guarded.get()).failure_popup };
        if let Some(popup) =
            popup.filter(|popup| unsafe { (self.leaves.popup_active)(popup.as_ptr()) != 0 })
        {
            let mut input = core::mem::MaybeUninit::<super::abi::XpEpubPopupInput>::uninit();
            unsafe { (self.leaves.popup_input)(popup.as_ptr(), input.as_mut_ptr()) };
            if unsafe { input.assume_init() }.changed != 0 {
                self.request_update();
            }
            return;
        }
        let Some(book) = self.loop_book() else {
            self.finish();
            return;
        };
        self.remember_book();
        self.loop_radio_after();
        self.loop_link_note();
        if self.model(|state| state.orientation) != self.settings(false).orientation {
            self.apply_orientation(self.settings(false).orientation);
            self.request_update();
            return;
        }
        self.loop_prewarm();
        self.loop_partial_build();
        let at_end = self.model(|state| state.current.spine > 0)
            && self.model(|state| state.current.spine)
                >= unsafe { (self.leaves.book_spine_count)(book.as_ptr()) };
        self.clear_end_options();
        if self.settings(false).remove_read_recents != 0 {
            if at_end && !self.model(|state| state.recents_removed) {
                let removed = with_book_text(book, self.leaves.book_path, |path| unsafe {
                    (self.leaves.recents_remove)(path) != 0
                })
                .unwrap_or(false);
                self.model(|state| state.recents_removed = removed);
            } else if !at_end && self.model(|state| state.recents_removed) {
                unsafe { (self.leaves.recents_add)(book.as_ptr()) };
                self.model(|state| state.recents_removed = false);
            }
        }
        let move_to_read = at_end
            && self.settings(false).move_finished != 0
            && !with_book_text(book, self.leaves.book_path, |path| {
                let inside = is_in_read_folder(unsafe { path.bytes() }.unwrap_or_default());
                #[cfg(feature = "app-test")]
                if let Some(hooks) = self.loop_hooks {
                    unsafe { (hooks.read_folder)(hooks.context, u8::from(inside)) };
                }
                inside
            })
            .unwrap_or(false);
        self.model(|state| state.move_to_read = move_to_read);
        let touch = self.book_touch_turn();
        self.loop_expire(|state| &mut state.bookmark_notice);
        self.loop_expire(|state| &mut state.dictionary_notice);
        self.loop_expire(|state| &mut state.font_notice);
        if self.model(|state| state.overlay != Overlay::Page) {
            if self.uses_toolbar_menu() {
                let now = self.loop_now();
                self.model(|state| state.last_turn_ms = now);
                if !self.consume_rejected_input() {
                    self.handle_overlay_input();
                }
                return;
            }
            self.model(|state| state.overlay = Overlay::Page);
            {
                let access = self.lock_resources();
                self.discard_overlay_page(&access);
            }
            self.request_update();
            return;
        }
        if !at_end && self.settings(false).touch_controls != 0 {
            if let Some((x, y)) = self.screen_point(true) {
                self.model(|state| {
                    state.auto_turn = false;
                    state.manual_turn = 0;
                });
                self.start_clip_selection(x, y, SELECTION_CLIP);
                return;
            }
        }
        if matches!(
            self.mapped_query(Query::HomeAction),
            values::HOME_FOOTNOTES
                | values::HOME_SYNC
                | values::HOME_BOOKMARK
                | values::HOME_DICTIONARY
                | values::HOME_READER_MENU
                | values::HOME_CREATE_CLIPPING
        ) {
            self.model(|state| state.auto_turn = false);
        }
        if self.model(|state| state.auto_turn) {
            let touch_stops = self.touch_menu_gesture();
            if self.loop_released(CONFIRM) || self.loop_released(BACK) || touch_stops {
                self.model(|state| state.auto_turn = false);
                if touch_stops {
                    self.loop_haptic(false);
                }
                self.request_update();
                return;
            }
            if !self.loop_has_section() {
                self.request_update();
                return;
            }
            if self.loop_busy() {
                let now = self.loop_now();
                self.model(|state| state.last_turn_ms = now);
                return;
            }
            if self
                .loop_now()
                .wrapping_sub(self.model(|state| state.last_turn_ms))
                >= self.model(|state| state.turn_duration_ms)
            {
                let succeeded = self.page_turn(true);
                self.note_page_turn(true, succeeded);
                self.request_update();
                return;
            }
        }
        self.loop_expire(|state| &mut state.clipping_notice);
        if self.handle_end_menu(false) {
            return;
        }
        let end_menu = self.end_menu_active();
        let threshold = self.confirm_long_press_threshold();
        let long = !end_menu
            && threshold != 0
            && self.mapped_button(CONFIRM, ButtonQuery::Long, threshold);
        let released = self.loop_released(CONFIRM);
        if long {
            match u32::from(self.settings(false).long_menu) {
                values::LONG_MENU_BOOKMARK => self.loop_bookmark(),
                values::LONG_MENU_SYNC => {
                    if self.loop_sync() {
                        return;
                    }
                }
                values::LONG_MENU_DICTIONARY => {
                    self.open_dictionary_word_select();
                    return;
                }
                values::LONG_MENU_CREATE_CLIPPING => {
                    self.start_clip_selection(-1, -1, SELECTION_CLIP);
                    return;
                }
                values::LONG_MENU_FOOTNOTES => {
                    self.loop_footnotes();
                    return;
                }
                _ => {}
            }
        }
        if !end_menu {
            match self.mapped_query(Query::HomeAction) {
                values::HOME_BOOKMARK => {
                    if self.model(|state| state.bookmark_notice.is_none()) {
                        self.loop_bookmark();
                    }
                    return;
                }
                values::HOME_SYNC => {
                    self.loop_sync();
                    return;
                }
                values::HOME_DICTIONARY => {
                    if self.model(|state| state.dictionary_notice.is_none()) {
                        self.open_dictionary_word_select();
                    }
                    return;
                }
                values::HOME_CREATE_CLIPPING => {
                    self.start_clip_selection(-1, -1, SELECTION_CLIP);
                    return;
                }
                values::HOME_READER_MENU => {
                    self.loop_menu(false);
                    return;
                }
                _ => {}
            }
        }
        if !at_end
            && self.settings(false).touch_controls != 0
            && self.mapped_query(Query::HasTouch) != 0
        {
            if let Some((x, y)) = self.screen_point(false) {
                match self.link_href_at_point(x, y) {
                    Ok(Some(href)) => {
                        self.navigate_href(href.bytes(), true);
                        self.loop_haptic(false);
                        return;
                    }
                    Ok(None) => {}
                    Err(()) => {
                        self.request_update();
                        return;
                    }
                }
            }
        }
        let touch_menu = self.touch_menu_gesture();
        if released || touch_menu {
            if touch_menu {
                self.loop_haptic(false);
            }
            self.loop_menu(true);
            return;
        }
        if self.model(|state| state.history.depth > 0)
            && self.loop_released(BACK)
            && self.mapped_query(Query::HeldMs) < GO_HOME_MS
        {
            self.restore_position();
            return;
        }
        if self.handle_back() {
            return;
        }
        if u32::from(self.settings(false).short_power) == values::POWER_CREATE_CLIPPING
            && self.loop_released(POWER)
            && !self.loop_released(DOWN)
        {
            self.start_clip_selection(-1, -1, SELECTION_CLIP);
            return;
        }
        if (!end_menu && self.mapped_query(Query::HomeAction) == values::HOME_FOOTNOTES)
            || (u32::from(self.settings(false).short_power) == values::POWER_FOOTNOTES
                && self.loop_released(POWER)
                && !self.loop_released(DOWN))
        {
            self.loop_footnotes();
            return;
        }
        let guard = self.loop_busy()
            || self
                .loop_now()
                .wrapping_sub(self.model(|state| state.last_turn_ms))
                < 200;
        if self.model(|state| state.manual_turn != 0) && !guard {
            if !self.loop_has_section() {
                self.model(|state| state.manual_turn = 0);
                return;
            }
            let forward = self.model(|state| state.manual_turn > 0);
            self.model(|state| state.manual_turn = 0);
            let succeeded = self.page_turn(forward);
            self.note_page_turn(forward, succeeded);
            if succeeded && self.model(|state| state.manual_touch) {
                self.loop_haptic(false);
            }
            self.model(|state| state.manual_touch = false);
            self.request_update();
            return;
        }
        let buttons = self.button_turn();
        let previous = buttons.prev != 0 || touch.prev != 0;
        let next = buttons.next != 0 || touch.next != 0;
        let touched = touch.prev != 0 || touch.next != 0;
        if !previous && !next {
            self.background_build();
            return;
        }
        if self.handle_end_turn(previous, next) {
            return;
        }
        if self.loop_released(POWER) && self.loop_released(DOWN) {
            return;
        }
        let held = if touched {
            touch.held_ms
        } else {
            self.mapped_query(Query::HeldMs)
        };
        let long = buttons.from_tilt == 0 && held >= SKIP_HOLD_MS;
        if long && u32::from(self.settings(false).long_button) == values::LONG_BUTTON_SKIP {
            let succeeded = self.skip_pages(if next { 1 } else { -1 });
            self.note_page_turn(false, succeeded);
            if succeeded && touched {
                self.loop_haptic(true);
            }
            self.request_update();
            return;
        }
        if long && u32::from(self.settings(false).long_button) == values::LONG_BUTTON_ORIENTATION {
            let old = self.settings(false).orientation;
            self.apply_orientation(if next { (old + 3) % 4 } else { (old + 1) % 4 });
            if touched {
                self.loop_haptic(true);
            }
            self.request_update();
            return;
        }
        if !self.loop_has_section() {
            self.request_update();
            return;
        }
        if guard {
            self.model(|state| {
                state.manual_turn = if previous { -1 } else { 1 };
                state.manual_touch = touched;
            });
            return;
        }
        let succeeded = self.page_turn(!previous);
        self.note_page_turn(!previous, succeeded);
        if succeeded && touched {
            self.loop_haptic(false);
        }
        self.request_update();
    }
}
