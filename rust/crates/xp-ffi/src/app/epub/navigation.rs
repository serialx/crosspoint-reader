use super::{abi, app_abi, state::Position, EpubSession, ResourceAccess, Text};

impl EpubSession {
    pub(super) fn navigate_href(&self, href: &[u8], save: bool) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            unsafe {
                (hooks.navigate)(
                    hooks.context,
                    app_abi::Text::from_bytes(href),
                    u8::from(save),
                )
            };
            return;
        }
        let Some(book) = self.loop_book() else { return };
        let anchor = href
            .iter()
            .position(|byte| *byte == b'#')
            .map(|index| &href[index + 1..])
            .unwrap_or(&[]);
        // This replaces the native temporary anchor, which is moved into the
        // session only after successful resolution. There is one retained copy.
        let Ok(anchor) = Text::copy(anchor) else {
            return;
        };
        let target = if href.first() == Some(&b'#') {
            self.model(|state| state.current.spine)
        } else {
            unsafe {
                (super::session_book::resolve_href)(book.as_ptr(), app_abi::Text::from_bytes(href))
            }
        };
        if target < 0 {
            self.loop_log(format_args!("Could not resolve href: "), href);
            return;
        }
        if save {
            let (depth, spine, page) = self.model(|state| {
                let history = &mut state.history;
                if history.depth == 3 {
                    history.positions.copy_within(1..3, 0);
                    history.depth -= 1;
                }
                let page = if state.facts.section.is_some() {
                    state.current.page
                } else {
                    state.next_page
                };
                history.positions[usize::from(history.depth)] = Position {
                    spine: state.current.spine,
                    page,
                };
                history.depth += 1;
                (history.depth, state.current.spine, page)
            });
            self.loop_log(
                format_args!("Saved position [{depth}]: spine {spine}, page {page}"),
                &[],
            );
        }
        {
            let access = self.lock_resources();
            self.clear_deferred();
            self.set_anchor(&access, anchor);
            self.model(|state| {
                state.current.spine = target;
                state.next_page = 0;
            });
            self.drop_section(&access);
        }
        unsafe { (self.runtime.notify_render)() };
        self.loop_log(format_args!("Navigated to spine {target} for href: "), href);
    }

    pub(super) fn restore_position(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            unsafe { (hooks.restore)(hooks.context) };
            return;
        }
        let saved = self.model(|state| {
            if state.history.depth == 0 {
                return None;
            }
            state.history.depth -= 1;
            Some((
                state.history.depth,
                state.history.positions[usize::from(state.history.depth)],
            ))
        });
        let Some((depth, position)) = saved else {
            return;
        };
        self.loop_log(
            format_args!(
                "Restoring position [{depth}]: spine {}, page {}",
                position.spine, position.page
            ),
            &[],
        );
        {
            let access = self.lock_resources();
            self.clear_deferred();
            self.model(|state| {
                state.current.spine = position.spine;
                state.next_page = position.page;
            });
            self.drop_section(&access);
        }
        unsafe { (self.runtime.notify_render)() };
    }

    pub(super) fn clear_deferred(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            unsafe {
                (hooks.clear)(hooks.context);
            }
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.clear)(hooks.context) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.projection_hooks {
            unsafe { (hook.clear)(hook.context) };
        }
        self.model(|state| {
            state.cached.pages = 0;
            state.cached.offset = None;
        });
    }

    pub(super) fn jump_to_percent(&self, percent: i32) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.jump)(hooks.context, percent) };
            return;
        }
        let Some(book) = self.loop_book() else { return };
        let book = book.as_ptr();
        let size = unsafe { (super::session_book::size)(book) };
        if size == 0 {
            return;
        }
        let percent = percent.clamp(0, 100) as u32;
        let target = if percent == 100 {
            size - 1
        } else {
            (size / 100) * percent + (size % 100) * percent / 100
        };
        let count = unsafe { (super::session_book::spine_count)(book) };
        if count == 0 {
            return;
        }
        let found = unsafe { (super::session_book::spine_for_size)(book, target) };
        let spine = if found < 0 { count - 1 } else { found };
        let previous = if found > 0 {
            unsafe { (super::session_book::cumulative_size)(book, found - 1) }
        } else {
            0
        };
        let end = unsafe { (super::session_book::cumulative_size)(book, spine) };
        let span = end.saturating_sub(previous);
        let progress = if span == 0 {
            0.0
        } else {
            target.wrapping_sub(previous) as f32 / span as f32
        };
        self.model(|state| state.deferred.spine_progress = progress.clamp(0.0, 1.0));
        {
            let access = self.lock_resources();
            self.clear_deferred();
            self.model(|state| {
                state.current.spine = spine;
                state.next_page = 0;
                state.deferred.percent = true;
            });
            self.drop_section(&access);
        }
        unsafe { (self.runtime.notify_render)() };
    }

    pub(super) fn apply_progress(&self, sync: &app_abi::XpAppProgressResult) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.progress)(hooks.context, sync) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            unsafe { (hooks.progress)(hooks.context, sync) };
            return;
        }
        let Some(book) = self.loop_book() else { return };
        let book = book.as_ptr();
        if sync.has_offset != 0
            && sync.spine >= 0
            && sync.spine < unsafe { (super::session_book::spine_count)(book) }
        {
            let access = self.lock_resources();
            self.clear_deferred();
            let page = self.section(&access).and_then(|section| {
                if self.model(|state| state.current.spine) != sync.spine {
                    return None;
                }
                let mut page = 0;
                (unsafe {
                    (self.leaves.section_page_for_offset)(
                        section.as_ptr(),
                        sync.offset,
                        0,
                        &mut page,
                    )
                } != 0)
                    .then_some(page)
            });
            if let Some(page) = page {
                self.model(|state| state.current.page = i32::from(page));
            } else {
                self.model(|state| {
                    state.current.spine = sync.spine;
                    state.deferred.offset = Some(sync.offset);
                    state.next_page = sync.page.max(0);
                });
                self.drop_section(&access);
            }
            unsafe { (self.runtime.notify_render)() };
            return;
        }
        let mut target_spine = sync.spine;
        let mut target_page = sync.page;
        let active_total = self.estimated_pages().unwrap_or(0);
        let matches = self.model(|state| {
            state.facts.section.is_some()
                && sync.pages > 0
                && state.current.spine == sync.spine
                && sync.page >= 0
                && sync.page < sync.pages
                && active_total == sync.pages
        });
        if !matches && sync.has_saved_progress != 0 {
            let count = self
                .estimated_pages()
                .unwrap_or_else(|| self.model(|state| state.cached.pages));
            let spine = self.model(|state| state.current.spine);
            let saved = abi::XpEpubSavedProgress {
                xpath: sync.xpath,
                percentage: sync.percentage,
            };
            let mut position = core::mem::MaybeUninit::<abi::XpEpubPosition>::uninit();
            unsafe {
                (self.leaves.progress_to_reader)(
                    super::session_book::facade(book),
                    &saved,
                    spine,
                    count,
                    position.as_mut_ptr(),
                );
                target_spine = (*position.as_ptr()).spine;
                target_page = (*position.as_ptr()).page;
            }
        }
        let access = self.lock_resources();
        self.clear_deferred();
        if self.model(|state| state.current.spine) != target_spine {
            self.model(|state| {
                state.current.spine = target_spine;
                state.next_page = target_page;
            });
            self.drop_section(&access);
        } else if self.section(&access).is_some() {
            self.model(|state| {
                if state.current.page != target_page {
                    state.current.page = target_page.max(0);
                }
            });
        } else {
            self.model(|state| state.next_page = target_page);
        }
        unsafe { (self.runtime.notify_render)() };
    }

    pub(super) fn estimated_pages(&self) -> Option<i32> {
        let pages = self.model(|state| state.facts.section.map(|facts| facts.estimated_pages));
        #[cfg(feature = "app-test")]
        if let (Some(pages), Some(observer)) = (pages, self.estimate_observer) {
            unsafe { (observer.read)(observer.context, pages) };
        }
        pages
    }

    pub(super) fn apply_deferred(&self, access: &ResourceAccess<'_>) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(intercept) = self.deferred_override {
            return unsafe { (intercept.apply)(intercept.context) } != 0;
        }
        let cached = self.model(|state| state.cached);
        if cached.offset.is_none() && cached.pages == 0 {
            return false;
        }
        let Some(section) = self.section(access) else {
            return false;
        };
        if unsafe { (self.leaves.section_building)(section.as_ptr()) } != 0 {
            return false;
        }
        let mut changed = false;
        if self.model(|state| state.current.spine) == cached.spine {
            let active_page = self.model(|state| state.current.page);
            let mut page = active_page;
            let mut mapped = false;
            if let Some(offset) = cached.offset {
                let mut result = 0;
                mapped = unsafe {
                    (self.leaves.section_page_for_offset)(section.as_ptr(), offset, 0, &mut result)
                } != 0;
                if mapped {
                    page = i32::from(result);
                }
            }
            let count = i32::from(unsafe { (self.leaves.section_page_count)(section.as_ptr()) });
            if !mapped && cached.pages > 0 && count != cached.pages {
                let progress = active_page as f32 / cached.pages as f32;
                page = (progress * count as f32) as i32;
            }
            page = page.max(0);
            if count > 0 && page >= count {
                page = count - 1;
            }
            if page != active_page {
                self.model(|state| state.current.page = page);
                changed = true;
            }
        }
        self.clear_deferred();
        changed
    }

    pub(super) fn save_progress(
        &self,
        access: &ResourceAccess<'_>,
        spine: i32,
        page: i32,
        count: i32,
    ) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            return unsafe { (hooks.save)(hooks.context, spine, page, count) != 0 };
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            return unsafe { (hooks.save_progress)(hooks.context, spine, page, count) != 0 };
        }
        let Some(book) = self.book(access) else {
            return false;
        };
        let mut offset = None;
        if let Some(section) = self.section(access) {
            if spine == self.model(|state| state.current.spine)
                && page >= 0
                && page < i32::from(unsafe { (self.leaves.section_page_count)(section.as_ptr()) })
            {
                offset = self.model(|state| {
                    (page == state.current.page)
                        .then_some(state.visible_offset)
                        .flatten()
                });
                if offset.is_none() {
                    let mut value = 0;
                    if unsafe {
                        (self.leaves.section_offset_for_page)(
                            section.as_ptr(),
                            page as u16,
                            &mut value,
                        )
                    } != 0
                    {
                        offset = Some(value);
                    }
                }
            }
        }
        let position = abi::XpEpubPosition {
            spine,
            page,
            count,
            visible_offset: offset.unwrap_or(0),
            has_visible_offset: u8::from(offset.is_some()),
            paragraph: 0,
            list_item: 0,
            has_paragraph: 0,
            has_list_item: 0,
            resolved_spine: 0,
            mapped_page: 0,
            anchor: [0; 64],
        };
        unsafe {
            (self.leaves.progress_save)(super::session_book::facade(book.as_ptr()), &position) != 0
        }
    }

    pub(super) fn remember_offset(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.remember)(hooks.context) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.settings_overrides {
            unsafe { (hook.remember)(hook.context) };
            return;
        }
        self.model(|state| state.cached.offset = None);
        let Some(section) = self.section(access) else {
            return;
        };
        let page = self.model(|state| state.current.page);
        if page >= 0
            && page < i32::from(unsafe { (self.leaves.section_page_count)(section.as_ptr()) })
        {
            let mut offset = 0;
            if unsafe {
                (self.leaves.section_offset_for_page)(section.as_ptr(), page as u16, &mut offset)
            } != 0
            {
                self.model(|state| state.cached.offset = Some(offset));
            }
        }
    }
}
