use super::{abi, state::Overlay, EpubSession, Text};
use core::{ffi::c_void, mem::MaybeUninit, ptr::NonNull};
use xp_ui::{
    model::{BACK, CONFIRM, DOWN, LEFT, RIGHT, SWIPE_DOWN, SWIPE_UP, UP},
    reader::{
        epub_overlay::{Event, HOLD_MS, HOLD_ROWS, TEXT_ROWS},
        input::{ButtonQuery, Query},
    },
};

// Indices for the physical input_pressed leaf, not shared button masks.
enum HeldButton {
    Left = 2,
    Right = 3,
    Up = 4,
    Down = 5,
}

impl EpubSession {
    fn held_button(&self, button: HeldButton) -> bool {
        unsafe { (self.leaves.input_pressed)(button as u32) != 0 }
    }

    pub(super) fn mapped_button(&self, button: u32, query: ButtonQuery, threshold: u32) -> bool {
        let services = unsafe { &*(self.leaves.input_services)() };
        unsafe {
            services.button.expect("mapped button query")(
                services.context,
                button,
                query as u8,
                threshold,
            ) != 0
        }
    }

    fn overlay_width(&self) -> i32 {
        let width = self.overlay_geometry().width;
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            unsafe {
                (hooks.width)(hooks.context, width);
            }
        }
        width
    }

    fn overlay_height(&self) -> i32 {
        let height = self.overlay_geometry().height;
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            unsafe {
                (hooks.height)(hooks.context, height);
            }
        }
        height
    }

    fn tool_overlay(tool: i32) -> Overlay {
        match tool {
            0 => Overlay::Contents,
            1 => Overlay::Text,
            _ => Overlay::More,
        }
    }

    fn overlay_goto_spine(&self, target: i32) {
        let Some(book) = self.loop_book() else { return };
        let count = unsafe { (self.leaves.book_spine_count)(book.as_ptr()) };
        if count <= 0 {
            self.request_update();
            return;
        }
        let target = target.clamp(0, count - 1);
        if target != self.model(|state| state.current.spine) {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            self.clear_deferred();
            self.model(|state| {
                state.next_page = 0;
                state.current.spine = target;
            });
            self.drop_section(&access);
        }
        self.request_update();
    }

    fn activate_contents_row(&self, index: i32) {
        let Some(book) = self.loop_book() else { return };
        struct Capture<'a> {
            owner: &'a EpubSession,
            delivered: bool,
        }
        unsafe extern "C" fn item(context: *mut c_void, row: *const abi::XpEpubToc) -> u8 {
            let capture = &mut *context.cast::<Capture<'_>>();
            let Some(row) = row.as_ref() else { return 0 };
            let owner = capture.owner;
            let anchor = if row.spine != -1 {
                let Some(bytes) = row.anchor.bytes() else {
                    return 0;
                };
                let Ok(anchor) = Text::copy(bytes) else {
                    return 0;
                };
                Some(anchor)
            } else {
                None
            };
            {
                let access = owner.lock_resources();
                if owner.model(|state| state.scene_gate.failed) {
                    return 0;
                }
                if let Some(anchor) = anchor {
                    owner.clear_deferred();
                    owner.model(|state| {
                        state.current.spine = row.spine;
                        state.next_page = 0;
                    });
                    (*owner.guarded(&access)).anchor = anchor;
                    owner.drop_section(&access);
                }
                owner.model(|state| state.overlay = Overlay::Page);
                owner.discard_overlay_page(&access);
            }
            owner.request_update();
            capture.delivered = true;
            1
        }
        let mut capture = Capture {
            owner: self,
            delivered: false,
        };
        unsafe {
            (self.leaves.book_toc)(
                book.as_ptr(),
                index,
                item,
                (&mut capture as *mut Capture<'_>).cast(),
            );
        }
        if !capture.delivered {
            self.request_update();
        }
    }

    fn activate_overlay_row(&self, count: i32) {
        let (index, overlay) = self.model(|state| (state.panel_index, state.overlay));
        if index < 0 || index >= count {
            return;
        }
        match overlay {
            Overlay::Text => match index {
                0 => {
                    {
                        let access = self.lock_resources();
                        if self.model(|state| state.scene_gate.failed) {
                            drop(access);
                            self.request_update();
                            return;
                        }
                        self.model(|state| state.overlay = Overlay::Page);
                        self.dismiss_overlay_popup();
                        self.discard_overlay_page(&access);
                    }
                    {
                        let access = self.lock_resources();
                        self.settle_overlay_refresh(&access);
                        if self.model(|state| state.scene_gate.failed) {
                            self.model(|state| state.overlay = overlay);
                            drop(access);
                            self.request_update();
                            return;
                        }
                    }
                    self.open_text_settings(true);
                }
                4 => {
                    let enabled = self.settings(false).focus_reading == 0;
                    unsafe {
                        (self.leaves.settings_focus_reading)(u8::from(enabled));
                    }
                    self.apply_text_setting_live();
                }
                _ => self.show_text_row_popup(index),
            },
            Overlay::Contents => self.activate_contents_row(index),
            Overlay::More => self.activate_more_row(index),
            Overlay::Page | Overlay::Toolbar => {}
        }
    }

    fn page_overlay_rows(&self, toolbar: NonNull<c_void>, direction: i32, count: i32, rows: i32) {
        if count <= 0 {
            return;
        }
        let moved =
            unsafe { (self.leaves.toolbar_scroll)(toolbar.as_ptr(), direction * rows, count) != 0 };
        if self.model(|state| state.panel_cursor) {
            self.model(|state| {
                state.panel_index = (state.panel_index + direction * rows).clamp(0, count - 1)
            });
            self.redraw_overlay();
        } else if moved {
            self.redraw_overlay();
        }
    }

    pub(super) fn handle_overlay_input(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            unsafe { (hooks.overlay_input)(hooks.context) };
            return;
        }
        let Some(toolbar) = self.loop_toolbar() else {
            return;
        };
        if let Some(popup) = self
            .overlay_popup()
            .filter(|popup| unsafe { (self.leaves.popup_active)(popup.as_ptr()) != 0 })
        {
            let mut input = MaybeUninit::<abi::XpEpubPopupInput>::uninit();
            unsafe {
                (self.leaves.popup_input)(popup.as_ptr(), input.as_mut_ptr());
            }
            if unsafe { input.assume_init() }.changed != 0 {
                self.overlay_popup_changed();
            }
            return;
        }
        let mut routed = MaybeUninit::<abi::XpEpubRouted>::uninit();
        unsafe {
            (self.leaves.toolbar_route)(toolbar.as_ptr(), routed.as_mut_ptr());
        }
        let routed = unsafe { routed.assume_init() };
        if matches!(self.model(|state| state.overlay), Overlay::Toolbar) {
            match routed.event {
                x if x == Event::Dismiss as u8 => {
                    self.close_overlay_to_page();
                    return;
                }
                x if x == Event::Tool as u8 => {
                    self.model(|state| state.focused_tool = routed.value);
                    self.open_overlay(Self::tool_overlay(routed.value));
                    return;
                }
                x if x == Event::PrevChapter as u8 => {
                    self.overlay_goto_spine(self.model(|state| state.current.spine) - 1);
                    return;
                }
                x if x == Event::NextChapter as u8 => {
                    self.overlay_goto_spine(self.model(|state| state.current.spine) + 1);
                    return;
                }
                x if x == Event::Scrub as u8 => {
                    let Some(book) = self.loop_book() else { return };
                    let count = unsafe { (self.leaves.book_spine_count)(book.as_ptr()) };
                    let target =
                        ((routed.permille as f32 / 1000.0) * (count - 1) as f32 + 0.5) as i32;
                    self.overlay_goto_spine(target);
                    return;
                }
                _ => {}
            }
            if routed.routed != 0 {
                return;
            }
            if self.mapped_button(BACK, ButtonQuery::Release, 0) {
                self.close_overlay_to_page();
                return;
            }
            if self.mapped_button(LEFT, ButtonQuery::Release, 0) {
                self.model(|state| {
                    state.focused_tool = (state.focused_tool + 2) % 3;
                    state.panel_cursor = true;
                });
                self.redraw_overlay();
                return;
            }
            if self.mapped_button(RIGHT, ButtonQuery::Release, 0) {
                self.model(|state| {
                    state.focused_tool = (state.focused_tool + 1) % 3;
                    state.panel_cursor = true;
                });
                self.redraw_overlay();
                return;
            }
            if self.mapped_button(CONFIRM, ButtonQuery::Release, 0) {
                self.open_overlay(Self::tool_overlay(self.model(|state| state.focused_tool)));
                return;
            }
            let previous = self.mapped_button(UP, ButtonQuery::Release, 0);
            let next = self.mapped_button(DOWN, ButtonQuery::Release, 0);
            if previous || next {
                self.overlay_goto_spine(
                    self.model(|state| state.current.spine) + if next { 1 } else { -1 },
                );
            }
            return;
        }
        let overlay = self.model(|state| state.overlay);
        let count = match overlay {
            Overlay::Contents => self.loop_book().map_or(0, |book| unsafe {
                (self.leaves.book_toc_count)(book.as_ptr())
            }),
            Overlay::Text => TEXT_ROWS,
            _ => i32::from(unsafe { (*self.guarded.get()).more_count }),
        };
        let rows = unsafe { (self.leaves.toolbar_visible_rows)(toolbar.as_ptr()) }.max(1);
        match routed.event {
            x if x == Event::Dismiss as u8 => {
                self.dismiss_overlay_panel();
                return;
            }
            x if x == Event::Tool as u8 => {
                let target = Self::tool_overlay(routed.value);
                if target != overlay {
                    self.model(|state| state.focused_tool = routed.value);
                    self.open_overlay(target);
                }
                return;
            }
            x if x == Event::Row as u8 => {
                if routed.x >= self.overlay_width() - 44 {
                    let next =
                        routed.y >= self.overlay_height() - (self.overlay_height() * 62) / 200;
                    self.page_overlay_rows(toolbar, if next { 1 } else { -1 }, count, rows);
                    return;
                }
                self.model(|state| {
                    state.panel_index = routed.value;
                    state.panel_cursor = false;
                });
                self.activate_overlay_row(count);
                return;
            }
            _ => {}
        }
        let swipe = self.mapped_query(Query::Swipe);
        if swipe == SWIPE_UP as u32 || swipe == SWIPE_DOWN as u32 {
            self.page_overlay_rows(
                toolbar,
                if swipe == SWIPE_UP as u32 { 1 } else { -1 },
                count,
                rows,
            );
            return;
        }
        if routed.routed != 0 {
            return;
        }
        if self.mapped_button(BACK, ButtonQuery::Release, 0) {
            self.dismiss_overlay_panel();
            return;
        }
        if self.mapped_button(CONFIRM, ButtonQuery::Release, 0) {
            self.activate_overlay_row(count);
            return;
        }
        if count <= 0 {
            return;
        }
        let up = self.held_button(HeldButton::Up) || self.held_button(HeldButton::Left);
        let down = self.held_button(HeldButton::Down) || self.held_button(HeldButton::Right);
        if !self.model(|state| state.panel_hold_jumped)
            && (up || down)
            && self.mapped_query(Query::HeldMs) >= HOLD_MS
        {
            self.model(|state| {
                state.panel_index = (state.panel_index + if down { HOLD_ROWS } else { -HOLD_ROWS })
                    .clamp(0, count - 1);
                state.panel_hold_jumped = true;
                state.panel_cursor = true;
            });
            self.redraw_overlay();
            return;
        }
        let released_up = self.mapped_button(UP, ButtonQuery::Release, 0)
            || self.mapped_button(LEFT, ButtonQuery::Release, 0);
        let released_down = self.mapped_button(DOWN, ButtonQuery::Release, 0)
            || self.mapped_button(RIGHT, ButtonQuery::Release, 0);
        if released_up || released_down {
            if !self.model(|state| state.panel_hold_jumped) {
                self.model(|state| {
                    state.panel_index = if released_up {
                        (state.panel_index + count - 1) % count
                    } else {
                        (state.panel_index + 1) % count
                    };
                    state.panel_cursor = true;
                });
                self.redraw_overlay();
            }
            self.model(|state| state.panel_hold_jumped = false);
        }
    }
}
