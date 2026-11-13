use super::chapter_view::ChapterBook;
use crate::app::{
    epub::{abi, session_book, ResourceAccess},
    resources::Text,
};
use core::ffi::c_void;

pub(super) struct GuardedBook<'a, 'b>(pub(super) &'a ResourceAccess<'b>);
impl ChapterBook for GuardedBook<'_, '_> {
    fn count(&self) -> Option<i32> {
        self.0
            .owner
            .book(self.0)
            .map(|book| unsafe { session_book::toc_count(book.as_ptr()) })
    }
    fn label(&mut self, index: i32, text: &mut Text) -> bool {
        let Some(book) = self.0.owner.book(self.0) else {
            return false;
        };
        struct Label<'a> {
            text: &'a mut Text,
            called: bool,
        }
        unsafe extern "C" fn fill(context: *mut c_void, item: *const abi::XpEpubToc) -> u8 {
            let label = unsafe { &mut *context.cast::<Label<'_>>() };
            label.called = true;
            let text = &mut label.text;
            let item = unsafe { &*item };
            let Some(title) = (unsafe { item.title.bytes() }) else {
                return 0;
            };
            let spaces = usize::try_from(item.level.saturating_sub(1).max(0))
                .unwrap_or(0)
                .saturating_mul(2);
            u8::from(text.assign_indented(spaces, title).is_ok())
        }
        let mut label = Label {
            text,
            called: false,
        };
        let ok = unsafe {
            session_book::toc(
                book.as_ptr(),
                index,
                fill,
                (&mut label as *mut Label<'_>).cast(),
            ) != 0
        };
        if !label.called {
            // An unavailable label must not reject the list or trigger an idle retry.
            label.text.clear();
            return true;
        }
        ok
    }
}
