use super::{
    app_abi,
    book::with_book_text,
    state::{Overlay, Position},
    EpubSession, ResourceAccess,
};
use xp_ui::reader::PageMove;

pub(super) enum RemoteShortcut {
    Selection,
    Toolbar,
    Menu,
}

#[derive(Clone, Copy)]
pub(super) struct ChapterPosition {
    pub(super) page: i32,
    pub(super) count: i32,
}

// Match the ESP32-C3 ROM conversion, including signed NaNs.
fn progress_integer(value: f32) -> i32 {
    if value.is_nan() {
        if value.is_sign_negative() {
            i32::MIN
        } else {
            i32::MAX
        }
    } else {
        value as i32
    }
}

impl EpubSession {
    fn section_position(&self) -> Option<(Position, i32)> {
        let position = self.model(|state| {
            state
                .facts
                .section
                .map(|section| (state.current, section.estimated_pages))
        });
        #[cfg(feature = "app-test")]
        if let (Some((_, count)), Some(observer)) = (position, self.estimate_observer) {
            unsafe { (observer.read)(observer.context, count) };
        }
        position
    }

    pub(super) fn chapter_position(&self) -> ChapterPosition {
        if let Some((position, count)) = self.section_position() {
            ChapterPosition {
                page: position.page,
                count,
            }
        } else {
            self.model(|state| ChapterPosition {
                page: state.next_page,
                count: state.cached.pages,
            })
        }
    }

    pub(super) fn book_percent_for(&self, position: ChapterPosition) -> i32 {
        let Some(book) = self.loop_book() else {
            return 0;
        };
        if unsafe { (self.leaves.book_size)(book.as_ptr()) } == 0 || position.count <= 0 {
            return 0;
        }
        let spine = self.model(|state| state.current.spine);
        let fraction = position.page as f32 / position.count as f32;
        let progress = unsafe { (self.leaves.book_progress)(book.as_ptr(), spine, fraction) };
        progress_integer(progress.clamp(0.0, 1.0) * 100.0 + 0.5)
    }

    pub(super) fn current_toc_index(&self, access: &ResourceAccess<'_>) -> i32 {
        let Some(book) = self.book(access) else {
            return 0;
        };
        let section = self.section(access);
        let (spine, page) = self.model(|state| {
            (
                state.current.spine,
                if section.is_some() {
                    state.current.page
                } else {
                    state.next_page
                },
            )
        });
        unsafe {
            (self.leaves.section_toc_for_page)(
                section.map_or(core::ptr::null_mut(), |section| section.as_ptr()),
                book.as_ptr(),
                spine,
                page,
            )
            .max(0)
        }
    }

    pub(super) fn screenshot_info(&self) -> app_abi::Screenshot {
        let mut result = app_abi::Screenshot {
            reader_type: 1,
            title: [0; 64],
            spine: -1,
            current_page: 0,
            total_pages: 0,
            progress_percent: 0,
        };
        let book = self.loop_book();
        if let Some(book) = book {
            with_book_text(book, self.leaves.book_title, |text| {
                if let Some(bytes) = unsafe { text.bytes() } {
                    for (output, &byte) in result.title[..63].iter_mut().zip(bytes) {
                        if byte == 0 {
                            break;
                        }
                        *output = byte as core::ffi::c_char;
                    }
                }
            });
            result.spine = self.model(|state| state.current.spine);
        }
        if let Some((position, count)) = self.section_position() {
            result.current_page = position.page.wrapping_add(1);
            result.total_pages = count;
            if let Some(book) = book {
                if unsafe { (self.leaves.book_size)(book.as_ptr()) } > 0 && result.total_pages > 0 {
                    let progress = unsafe {
                        (self.leaves.book_progress)(
                            book.as_ptr(),
                            position.spine,
                            position.page as f32 / count as f32,
                        )
                    };
                    result.progress_percent =
                        progress_integer(progress * 100.0 + 0.5).clamp(0, 100);
                }
            }
        }
        result
    }

    pub(super) fn is_at_end(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.at_end)(hook.context) != 0 };
        }
        let Some(book) = self.loop_book() else {
            return false;
        };
        let spine = self.model(|state| state.current.spine);
        spine >= unsafe { (self.leaves.book_spine_count)(book.as_ptr()) }
    }

    pub(super) fn progress_basis_points(&self) -> i32 {
        if self.is_at_end() {
            return 10000;
        }
        if let Some(book) = self.loop_book() {
            if self.model(|state| state.facts.section.is_some())
                && unsafe { (self.leaves.book_size)(book.as_ptr()) } != 0
            {
                if let Some((position, count)) =
                    self.section_position().filter(|(_, count)| *count > 0)
                {
                    let progress = unsafe {
                        (self.leaves.book_progress)(
                            book.as_ptr(),
                            position.spine,
                            position.page as f32 / count as f32,
                        )
                    };
                    return progress_integer(progress * 10000.0 + 0.5).clamp(0, 10000);
                }
            }
        }
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.projection_hooks {
            unsafe { (hook.fallback)(hook.context) };
        }
        self.screenshot_info().progress_percent * 100
    }

    pub(super) fn return_from_end(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.return_end)(hook.context) };
            return;
        }
        let Some(book) = self.loop_book() else { return };
        if unsafe { (self.leaves.book_spine_count)(book.as_ptr()) } > 0 {
            let count = unsafe { (self.leaves.book_spine_count)(book.as_ptr()) };
            self.model(|state| {
                state.current.spine = count.wrapping_sub(1);
                state.next_page = 0;
                state.deferred.page = Some(u16::MAX);
            });
        }
    }

    pub(super) fn end_rendered(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.end_rendered)(hook.context) };
            return;
        }
        self.model(|state| state.auto_turn = false);
        if self.model(|state| core::mem::take(&mut state.sync_save_error)) {
            unsafe {
                (self.resources.popup_draw)((self.resources.translate)(
                    xp_core::i18n::ids::STR_SAVE_PROGRESS_FAILED,
                ));
            }
        }
    }

    pub(super) fn confirm_long_press_threshold(&self) -> u32 {
        let mut input = core::mem::MaybeUninit::uninit();
        unsafe { (self.leaves.input_facts)(input.as_mut_ptr()) };
        if unsafe { input.assume_init() }.board_touch != 0 {
            return 0;
        }
        match self.settings(false).long_menu {
            2 | 3 | 5 | 6 => 400,
            0 if unsafe { (self.leaves.has_sync_credentials)() } != 0 => 1000,
            _ => 0,
        }
    }

    pub(super) fn uses_toolbar_menu(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.projection_hooks {
            return unsafe { (hook.toolbar_menu)(hook.context) != 0 };
        }
        self.settings(false).menu_style == 1
    }

    pub(super) fn remote_shortcut(&self, reader_menu: bool) -> Option<RemoteShortcut> {
        if !self.model(|state| state.facts.section.is_some()) {
            return None;
        }
        self.model(|state| state.auto_turn = false);
        Some(if !reader_menu {
            RemoteShortcut::Selection
        } else if self.uses_toolbar_menu() {
            RemoteShortcut::Toolbar
        } else {
            RemoteShortcut::Menu
        })
    }

    pub(super) fn end_menu_active(&self) -> bool {
        if !self.is_at_end() || !self.model(|state| state.base.end_ready) {
            return false;
        }
        // Render publishes EndReady after installing/loading the widget. Only the
        // loop drops it; its own menuActive() uses the widget publication gate.
        let end = unsafe {
            (*self.guarded.get())
                .end_options
                .expect("published end menu")
        };
        unsafe { (self.leaves.end_menu_active)(end.as_ptr()) != 0 }
    }

    fn base_input_over_page(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.projection_hooks {
            return unsafe { (hook.base_input)(hook.context) != 0 };
        }
        self.end_menu_active()
    }

    pub(super) fn input_over_page(&self) -> bool {
        self.model(|state| !matches!(state.overlay, Overlay::Page)) || self.base_input_over_page()
    }

    fn acknowledge_page_turn(&self) {
        #[cfg(feature = "app-test")]
        {
            let hook = self.projection_hooks.expect("fixture BLE acknowledgment");
            unsafe { (hook.acknowledge)(hook.context) };
        }
        #[cfg(not(feature = "app-test"))]
        unsafe {
            crate::ble_runtime::xp_ble_acknowledge()
        };
    }

    pub(super) fn apply_page_move(&self, movement: PageMove, record: bool) -> bool {
        match movement.kind {
            1 => self.model(|state| state.current.page = movement.value),
            2 => {
                let access = self.lock_resources();
                self.model(|state| {
                    state.next_page = 0;
                    if movement.last_page != 0 {
                        state.deferred.page = Some(u16::MAX);
                    }
                    state.current.spine = movement.value;
                });
                self.drop_section(&access);
                if record {
                    let now = unsafe { (self.runtime.now_ms)() };
                    self.model(|state| state.last_turn_ms = now);
                }
                return true;
            }
            3 => self.model(|state| state.current.spine = movement.value),
            _ => return false,
        }
        if record {
            let now = unsafe { (self.runtime.now_ms)() };
            self.model(|state| state.last_turn_ms = now);
        }
        true
    }

    pub(super) fn page_turn(&self, forward: bool) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.turn)(hook.context, u8::from(forward)) != 0 };
        }
        if !self.model(|state| state.facts.section.is_some()) {
            return false;
        }
        self.acknowledge_page_turn();
        {
            let _access = self.lock_resources();
            self.clear_deferred();
        }
        let Some(book) = self.loop_book() else {
            return false;
        };
        let spine_count = unsafe { (self.leaves.book_spine_count)(book.as_ptr()) };
        let Some((position, facts)) =
            self.model(|state| state.facts.section.map(|facts| (state.current, facts)))
        else {
            return false;
        };
        let movement = xp_ui::reader::turn(
            position.spine,
            spine_count,
            position.page,
            facts.pages,
            facts.building,
            forward,
        );
        self.apply_page_move(movement, true)
    }

    pub(super) fn skip_pages(&self, amount: i32) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.skip)(hook.context, amount) != 0 };
        }
        if !self.model(|state| state.facts.section.is_some()) {
            return false;
        }
        self.acknowledge_page_turn();
        let position = self.model(|state| state.current);
        self.apply_page_move(
            xp_ui::reader::skip(position.spine, position.page, amount),
            false,
        )
    }
}
