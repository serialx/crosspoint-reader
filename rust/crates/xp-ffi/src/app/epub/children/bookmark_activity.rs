use super::{
    bookmark_owned::Bookmarks, bookmark_view::Details, bookmark_window::Toc, physical::Physical,
};
use crate::app::{
    abi,
    epub::{abi::XpEpubToc, session_book, EpubSession, ResourceAccess},
    resources::Text,
};
use core::ffi::c_void;
use xp_core::i18n::ids::STR_UNNAMED;

struct GuardedToc<'a, 'b>(&'a ResourceAccess<'b>);
impl Toc for GuardedToc<'_, '_> {
    fn subtitle(&mut self, spine: u16, prefix: &[u8], output: &mut Text) -> bool {
        let parent = self.0.owner;
        let Some(book) = parent.book(self.0) else {
            return false;
        };
        let Some(index) = (unsafe { session_book::toc_for_spine(book.as_ptr(), i32::from(spine)) })
        else {
            return false;
        };
        if index < 0 {
            let title = unsafe { parent.resources.translate(STR_UNNAMED).bytes() };
            return title.is_some_and(|title| output.assign_parts(&[prefix, title]).is_ok());
        }
        struct Fill<'a> {
            prefix: &'a [u8],
            output: &'a mut Text,
        }
        unsafe extern "C" fn fill(context: *mut c_void, item: *const XpEpubToc) -> u8 {
            let target = unsafe { &mut *context.cast::<Fill<'_>>() };
            let Some(title) = (unsafe { (*item).title.bytes() }) else {
                return 0;
            };
            u8::from(target.output.assign_parts(&[target.prefix, title]).is_ok())
        }
        let mut target = Fill { prefix, output };
        unsafe {
            session_book::toc(
                book.as_ptr(),
                index,
                fill,
                (&mut target as *mut Fill<'_>).cast(),
            ) != 0
        }
    }
}

pub(super) fn error(parent: &EpubSession, message: &[u8]) {
    unsafe {
        (parent.leaves.log)(
            1,
            abi::Text::from_bytes(b"EPB"),
            abi::Text::from_bytes(message),
        )
    };
}
impl Bookmarks {
    #[inline(never)]
    unsafe fn command_allocation_error(&self, parent: &EpubSession) {
        if let Some(bytes) = unsafe { &*self.frame.get() }
            .list
            .commands
            .failed_allocation_bytes()
        {
            super::super::book_owner::log::with_tail(
                parent.leaves,
                1,
                b"RUI",
                format_args!("Cannot allocate {bytes} command bytes"),
                &[],
            );
        }
    }

    #[inline(never)]
    pub(super) unsafe fn enter_bookmarks(
        &self,
        parent: &EpubSession,
        access: &ResourceAccess<'_>,
    ) -> bool {
        if !unsafe { self.enter(parent.resources) } {
            unsafe { self.command_allocation_error(parent) };
            return false;
        }
        if parent.book(access).is_none() {
            return true;
        }
        parent.read_bookmarks(access);
        let count = parent.with_bookmarks(access, |rows| rows.len());
        super::super::book_owner::log::with_tail(
            parent.leaves,
            0,
            b"EPB",
            format_args!("Loaded {count} bookmarks for book: "),
            &[parent.path().bytes()],
        );
        unsafe { self.invalidate() };
        true
    }

    #[inline(never)]
    pub(super) unsafe fn render_bookmarks(
        &self,
        parent: &EpubSession,
        access: &ResourceAccess<'_>,
        physical: &Physical,
    ) -> bool {
        let painted = parent.with_bookmarks(access, |rows| unsafe {
            self.paint(
                parent.resources,
                physical,
                &Details {
                    help: parent.leaves.child_list_help,
                    icon: parent.leaves.child_list_icon,
                },
                rows,
                &mut GuardedToc(access),
            )
        });
        let Ok(prepared) = painted else {
            if matches!(painted, Err(xp_ui::scene::Error::Capacity)) {
                unsafe { self.command_allocation_error(parent) };
            }
            return false;
        };
        if let Some(handle) = unsafe { (*self.popup.get()).handle } {
            if unsafe { (parent.leaves.popup_active)(handle.as_ptr()) } != 0 {
                unsafe { (parent.leaves.popup_process_render)(handle.as_ptr()) };
                return true;
            }
        }
        unsafe { (parent.resources.refresh)(prepared.refresh as u8) };
        true
    }
}
