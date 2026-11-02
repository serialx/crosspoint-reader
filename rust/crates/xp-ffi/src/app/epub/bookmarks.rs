use super::{
    abi, app_abi, state::Position, text::sanitize_summary, EpubSession, ResourceAccess, Text,
};
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::bookmark::{self, BookmarkPosition, ProgressRange};

struct Match {
    position: Position,
    count: i32,
    range: ProgressRange,
    found: bool,
}
impl Match {
    fn matches(&self, row: &abi::XpEpubBookmark) -> bool {
        bookmark::matches(
            BookmarkPosition {
                percentage: row.percentage,
                spine: i32::from(row.spine),
                page: i32::from(row.chapter_page),
                count: i32::from(row.page_count),
            },
            self.position.spine,
            self.position.page,
            self.count,
            self.range,
        )
    }
}

impl EpubSession {
    // Only the loop installs/destroys this handle or mutates its vector.
    fn loop_bookmarks(&self) -> NonNull<c_void> {
        self.loop_bookmark_handle().expect("reader bookmarks")
    }

    fn bookmark_range(
        &self,
        book: NonNull<c_void>,
        position: Position,
        count: i32,
    ) -> ProgressRange {
        let local = bookmark::range(position.page, count);
        unsafe {
            ProgressRange {
                start: (self.leaves.book_progress)(book.as_ptr(), position.spine, local.start),
                end: (self.leaves.book_progress)(book.as_ptr(), position.spine, local.end),
            }
        }
    }

    fn bookmark_flag(&self, access: Option<&ResourceAccess<'_>>) {
        if !self.model(|state| state.facts.section.is_some()) {
            self.model(|state| state.bookmarked = false);
            return;
        }
        let book = match access {
            Some(access) => self.book(access),
            None => self.loop_book(),
        };
        let Some(book) = book else {
            self.model(|state| state.bookmarked = false);
            return;
        };
        let bookmarks = match access {
            Some(access) => self.bookmarks(access).expect("reader bookmarks"),
            None => self.loop_bookmarks(),
        };
        let length = unsafe { (self.leaves.bookmarks_count)(bookmarks.as_ptr()) };
        if length == 0 {
            self.model(|state| state.bookmarked = false);
            return;
        }
        let count = match access {
            Some(access) => self
                .section(access)
                .map(|section| unsafe { (self.leaves.section_estimated_pages)(section.as_ptr()) }),
            None => self.estimated_pages(),
        }
        .unwrap_or(0);
        let position = self.model(|state| state.current);
        let range = self.bookmark_range(book, position, count);
        let mut search = Match {
            position,
            count,
            range,
            found: false,
        };
        unsafe extern "C" fn row(context: *mut c_void, row: *const abi::XpEpubBookmark) -> u8 {
            let search = &mut *context.cast::<Match>();
            search.found = search.matches(&*row);
            1
        }
        for index in 0..length {
            unsafe {
                (self.leaves.bookmarks_row)(
                    bookmarks.as_ptr(),
                    index,
                    row,
                    (&mut search as *mut Match).cast(),
                )
            };
            if search.found {
                break;
            }
        }
        self.model(|state| state.bookmarked = search.found);
    }

    pub(super) fn update_bookmark_flag(&self, access: &ResourceAccess<'_>) {
        self.bookmark_flag(Some(access));
    }
    pub(super) fn loop_update_bookmark_flag(&self) {
        self.bookmark_flag(None);
    }

    fn load_bookmark_rows(&self, access: &ResourceAccess<'_>) {
        let bookmarks = self.bookmarks(access).expect("reader bookmarks");
        unsafe { (self.leaves.bookmarks_clear)(bookmarks.as_ptr()) };
        if let Some(book) = self.book(access) {
            unsafe { (self.leaves.bookmarks_load)(bookmarks.as_ptr(), book.as_ptr()) };
        }
    }

    pub(super) fn load_cached_bookmarks(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.load_bookmarks)(hook.context) };
            return;
        }
        self.load_bookmark_rows(access);
        self.update_bookmark_flag(access);
    }

    pub(super) fn loop_load_cached_bookmarks(&self) {
        {
            let access = self.lock_resources();
            self.load_bookmark_rows(&access);
        }
        self.loop_update_bookmark_flag();
    }

    fn section_text(&self, access: &ResourceAccess<'_>, page: i32) -> Option<Text> {
        let Some(section) = self.section(access) else {
            return Some(Text::default());
        };
        Text::receive(|sink, context| unsafe {
            (self.leaves.section_page_text)(section.as_ptr(), page, sink, context)
        })
    }

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
            let count = unsafe { (self.leaves.section_estimated_pages)(section.as_ptr()) };
            (self.model(|state| state.current.page), count)
        };
        let position = self.loop_current_position();
        struct Context<'a> {
            owner: &'a EpubSession,
            book: NonNull<c_void>,
            page: i32,
            count: i32,
        }
        unsafe extern "C" fn saved(
            context: *mut c_void,
            saved: *const abi::XpEpubSavedProgress,
        ) -> u8 {
            let context = &*context.cast::<Context<'_>>();
            u8::from(context.owner.commit_bookmark(
                context.book,
                context.page,
                context.count,
                &*saved,
            ))
        }
        let mut context = Context {
            owner: self,
            book,
            page,
            count,
        };
        unsafe {
            (self.leaves.progress_to_saved)(
                book.as_ptr(),
                &position,
                saved,
                (&mut context as *mut Context<'_>).cast(),
            )
        };
    }

    fn commit_bookmark(
        &self,
        book: NonNull<c_void>,
        page: i32,
        count: i32,
        saved: &abi::XpEpubSavedProgress,
    ) -> bool {
        let position = Position {
            spine: self.model(|state| state.current.spine),
            page,
        };
        let range = self.bookmark_range(book, position, count);
        let bookmarks = self.loop_bookmarks();
        let removed = {
            let _access = self.lock_resources();
            let mut predicate = Match {
                position,
                count,
                range,
                found: false,
            };
            unsafe extern "C" fn keep(context: *mut c_void, row: *const abi::XpEpubBookmark) -> u8 {
                let predicate = &*context.cast::<Match>();
                u8::from(!predicate.matches(&*row))
            }
            (unsafe {
                (self.leaves.bookmarks_retain)(
                    bookmarks.as_ptr(),
                    keep,
                    (&mut predicate as *mut Match).cast(),
                )
            }) != 0
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
            let mut summary = [0; 72];
            #[cfg(feature = "app-test")]
            if let Some(hook) = self.bookmark_overrides {
                unsafe { (hook.sanitize)(hook.context, text.wire()) };
            }
            let length = sanitize_summary(text.bytes(), &mut summary);
            let mut entry = abi::XpEpubBookmark {
                xpath: saved.xpath,
                summary: app_abi::Text::from_bytes(&summary[..length]),
                name: app_abi::Text::from_bytes(b""),
                percentage: saved.percentage,
                spine: self.model(|state| state.current.spine) as u16,
                page_count: count as u16,
                chapter_page: page as u16,
                has_offset: 0,
                offset: 0,
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
            if let Some(offset) = offset {
                entry.has_offset = 1;
                entry.offset = offset;
            }
            {
                let _access = self.lock_resources();
                if unsafe { (self.leaves.bookmarks_insert_front)(bookmarks.as_ptr(), &entry) } == 0
                {
                    return false;
                }
            }
            self.model(|state| {
                state.bookmark_removed = false;
                state.bookmarked = true;
            });
        }
        let saved = unsafe { (self.leaves.bookmarks_save)(bookmarks.as_ptr(), book.as_ptr()) };
        if saved == 0 {
            self.loop_log_parts(1, &[b"Failed to save bookmarks"]);
        }
        unsafe { (self.runtime.notify_render)() };
        true
    }
}
