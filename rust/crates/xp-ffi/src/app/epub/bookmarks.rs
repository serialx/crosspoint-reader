use super::{
    bookmark_storage::{Name, Row},
    session_section::Section as ReaderSection,
    state::Position,
    text::sanitize_summary,
    EpubSession, ResourceAccess, Text,
};
use crate::app::bookmarks::mutations;
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::bookmark::{self, BookmarkPosition, ProgressRange};

fn matches(row: &Row, position: Position, count: i32, range: ProgressRange) -> bool {
    bookmark::matches(
        BookmarkPosition {
            percentage: row.percentage,
            spine: i32::from(row.spine),
            page: i32::from(row.page),
            count: i32::from(row.page_count),
        },
        position.spine,
        position.page,
        count,
        range,
    )
}
impl EpubSession {
    fn bookmark_range(
        &self,
        book: NonNull<c_void>,
        position: Position,
        count: i32,
    ) -> Option<ProgressRange> {
        let local = bookmark::range(position.page, count);
        unsafe {
            Some(ProgressRange {
                start: super::session_book::progress(book.as_ptr(), position.spine, local.start)?,
                end: super::session_book::progress(book.as_ptr(), position.spine, local.end)?,
            })
        }
    }
    pub(super) fn update_bookmark_flag(&self, access: &ResourceAccess<'_>) {
        if self.with_bookmarks(access, |rows| rows.is_empty()) {
            self.model(|state| state.bookmarked = false);
            return;
        }
        let found = self.book(access).map_or(Some(false), |book| {
            let Some(section) = self.section(access) else {
                return Some(false);
            };
            let count = unsafe { ReaderSection::estimated_pages(section) };
            let position = self.model(|state| state.current);
            let range = self.bookmark_range(book, position, count)?;
            Some(self.with_bookmarks(access, |rows| {
                rows.iter().any(|row| matches(row, position, count, range))
            }))
        });
        let Some(found) = found else {
            self.reject_reader_scene();
            return;
        };
        self.model(|state| state.bookmarked = found);
    }
    #[cfg(feature = "app-test")]
    pub(super) fn loop_update_bookmark_flag(&self) {
        self.update_bookmark_flag(&self.lock_resources());
    }
    pub(super) fn load_cached_bookmarks(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.load_bookmarks)(hook.context) };
            return;
        }
        self.read_bookmarks(access);
        self.update_bookmark_flag(access);
    }
    #[cfg(feature = "app-test")]
    pub(super) fn loop_load_cached_bookmarks(&self) {
        self.load_cached_bookmarks(&self.lock_resources());
    }
    fn section_text(&self, access: &ResourceAccess<'_>, page: i32) -> Option<Text> {
        match self.section(access) {
            Some(section) => unsafe { self.page_text(section, page) },
            None => Some(Text::default()),
        }
    }
    #[inline(never)]
    pub(super) fn add_bookmark(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.bookmark)(hooks.context) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            unsafe { (hooks.bookmark)(hooks.context) };
            return;
        }
        if !self.model(|state| state.facts.section.is_some()) {
            return;
        }
        let Some(book) = self.loop_book() else {
            return;
        };
        let position = self.model(|state| state.current);
        self.loop_log(
            format_args!(
                "Toggle bookmark at spine {}, page {}",
                position.spine, position.page
            ),
            &[],
        );
        let (page, count) = {
            let access = self.lock_resources();
            let Some(section) = self.section(&access) else {
                return;
            };
            (self.model(|state| state.current.page), unsafe {
                ReaderSection::estimated_pages(section)
            })
        };
        let position = self.loop_current_position();
        #[cfg(feature = "app-test")]
        {
            let mut saved = None;
            if unsafe {
                super::test_support::legacy_progress::saved(book, &position, |row| {
                    let Some(bytes) = row.xpath.bytes() else {
                        return 0;
                    };
                    let Ok(xpath) = Text::copy(bytes) else {
                        return 0;
                    };
                    saved = Some(super::progress_mapping::Saved {
                        xpath,
                        percentage: row.percentage,
                    });
                    1
                })
            }
            .is_some()
            {
                if let Some(saved) = saved {
                    self.commit_bookmark(book, page, count, saved);
                }
                return;
            }
        }
        let saved = {
            let access = self.lock_resources();
            let Some(reader) = (unsafe { super::session_book::reader_pointer(book) }) else {
                return;
            };
            unsafe { self.saved_progress(&access, reader, &position) }
        };
        if let Some(saved) = saved {
            self.commit_bookmark(book, page, count, saved);
        }
    }

    #[inline(never)]
    fn commit_bookmark(
        &self,
        book: NonNull<c_void>,
        page: i32,
        count: i32,
        saved: super::progress_mapping::Saved,
    ) -> bool {
        let position = Position {
            spine: self.model(|state| state.current.spine),
            page,
        };
        let Some(range) = self.bookmark_range(book, position, count) else {
            self.loop_log_parts(1, &[b"Cannot save bookmarks: unreadable size window"]);
            return false;
        };
        let removed = {
            let access = self.lock_resources();
            self.mutate_bookmarks(&access, |rows| {
                mutations::retain(rows, |row| !matches(row, position, count, range)) != 0
            })
        };
        if removed {
            self.model(|state| {
                state.bookmark_removed = true;
                state.bookmarked = false;
            });
        } else {
            let text = if page >= 0 && page < count {
                let access = self.lock_resources();
                let Some(text) = self.section_text(&access, page) else {
                    return false;
                };
                text
            } else {
                Text::default()
            };
            #[cfg(feature = "app-test")]
            if let Some(hook) = self.bookmark_overrides {
                unsafe { (hook.sanitize)(hook.context, text.wire()) };
            }
            let mut summary = [0; 72];
            let length = sanitize_summary(text.bytes(), &mut summary);
            let Ok(summary) = Text::copy(&summary[..length]) else {
                return false;
            };
            let mut offset = self.model(|state| state.visible_offset);
            if offset.is_none()
                && self.model(|state| {
                    state
                        .facts
                        .section
                        .is_some_and(|section| page >= 0 && page < i32::from(section.pages))
                })
            {
                let access = self.lock_resources();
                offset = self.section_offset(&access, page as u16);
            }
            let entry = Row {
                xpath: saved.xpath,
                summary,
                name: Name::default(),
                percentage: saved.percentage,
                spine: position.spine as u16,
                page_count: count as u16,
                page: page as u16,
                visible_offset: offset,
            };
            {
                let access = self.lock_resources();
                if self
                    .mutate_bookmarks(&access, |rows| mutations::insert_front(rows, entry))
                    .is_err()
                {
                    return false;
                }
            }
            self.model(|state| {
                state.bookmark_removed = false;
                state.bookmarked = true;
            });
        }
        if !self.save_bookmarks() {
            self.loop_log_parts(1, &[b"Failed to save bookmarks"]);
        }
        unsafe { (self.runtime.notify_render)() };
        true
    }
}

#[cfg(all(feature = "chapter-test", not(feature = "app-test")))]
pub(super) fn test_failed_commit(owner: &EpubSession, book: NonNull<c_void>) {
    let access = unsafe { owner.borrowed_resources() };
    owner.mutate_bookmarks(&access, |rows| {
        rows.push(super::bookmark_storage::Row {
            xpath: super::Text::default(),
            summary: super::Text::default(),
            name: Default::default(),
            percentage: 0.645,
            spine: 64,
            page_count: 2,
            page: 1,
            visible_offset: Some(123),
        })
    });
    assert!(!owner.commit_bookmark(
        book,
        1,
        2,
        super::progress_mapping::Saved {
            xpath: super::Text::default(),
            percentage: 0.645,
        }
    ));
    owner.with_bookmarks(&access, |rows| {
        assert_eq!(rows.len(), 1);
        let row = &rows[0];
        assert_eq!(
            (row.spine, row.page_count, row.page, row.visible_offset),
            (64, 2, 1, Some(123))
        );
        assert_eq!(row.percentage, 0.645);
        assert!(row.xpath.is_empty() && row.summary.is_empty());
    });
}
