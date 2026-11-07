use super::EpubSession;
use crate::app::bookmarks;
use core::mem::MaybeUninit;

impl EpubSession {
    #[inline(never)]
    pub(super) fn export_clipping(
        &self,
        title: &[u8],
        author: &[u8],
        chapter: &[u8],
        page: i32,
        text: &[u8],
    ) -> bool {
        let Some(physical) = (unsafe { self.leaves.clipping_physical.as_ref() }) else {
            return false;
        };
        let io = crate::app::clippings_native::Native {
            leaves: self.leaves,
            resources: self.resources,
            physical,
        };
        bookmarks::export::write(
            &io,
            || {
                let mut value = MaybeUninit::uninit();
                (unsafe { (self.leaves.local_calendar)(value.as_mut_ptr()) } != 0)
                    .then(|| unsafe { value.assume_init() })
            },
            bookmarks::export::Clipping {
                title,
                author,
                chapter,
                page,
                text,
            },
        )
    }
}
