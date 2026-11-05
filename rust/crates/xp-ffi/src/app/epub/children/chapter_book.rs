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
        unsafe extern "C" fn fill(context: *mut c_void, item: *const abi::XpEpubToc) -> u8 {
            let text = unsafe { &mut *context.cast::<Text>() };
            let item = unsafe { &*item };
            let Some(title) = (unsafe { item.title.bytes() }) else {
                return 0;
            };
            let spaces = usize::try_from(item.level.saturating_sub(1).max(0))
                .unwrap_or(0)
                .saturating_mul(2);
            u8::from(text.assign_indented(spaces, title).is_ok())
        }
        unsafe { session_book::toc(book.as_ptr(), index, fill, (text as *mut Text).cast()) != 0 }
    }
}
