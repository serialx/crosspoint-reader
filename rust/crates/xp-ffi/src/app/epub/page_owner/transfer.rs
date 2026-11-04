use super::Page;
use core::{ffi::c_void, mem::ManuallyDrop, ptr::NonNull};

// Promoted to the public ABI only with retirement of the native Page leaves.
#[repr(C)]
pub(crate) struct View {
    arena: *const u8,
    size: usize,
    visible_offset: u32,
}

#[repr(C)]
pub(crate) struct OwnedPage {
    view: View,
    owner: *mut c_void,
    destroy: unsafe extern "C" fn(*mut c_void),
}

#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<View>() == 12);
    assert!(core::mem::size_of::<OwnedPage>() == 20);
};

unsafe extern "C" fn destroy(owner: *mut c_void) {
    let allocation = NonNull::new(owner).expect("transferred Page owner").cast();
    drop(Page { allocation });
}

impl OwnedPage {
    fn empty() -> Self {
        Self {
            view: View {
                arena: core::ptr::null(),
                size: 0,
                visible_offset: 0,
            },
            owner: core::ptr::null_mut(),
            destroy,
        }
    }

    fn new(page: Page) -> Self {
        let page = ManuallyDrop::new(page);
        Self {
            view: View {
                arena: page.arena().as_ptr(),
                size: page.arena().len(),
                visible_offset: page.view().visible_offset(),
            },
            owner: page.allocation.as_ptr().cast(),
            destroy,
        }
    }
}

// Sole owner while a native factory borrows the records. Factories consume all
// owners after successful construction, or leave all owners here for rollback.
pub(in crate::app::epub) struct Pending {
    records: [OwnedPage; 3],
    count: u8,
}

impl Pending {
    pub(in crate::app::epub) fn new(pages: [Option<Page>; 3]) -> Self {
        let count = pages.iter().take_while(|page| page.is_some()).count();
        assert!(count > 0 && pages[count..].iter().all(Option::is_none));
        Self {
            records: pages.map(|page| page.map_or_else(OwnedPage::empty, OwnedPage::new)),
            count: count as u8,
        }
    }

    pub(in crate::app::epub) fn records(&mut self) -> (*mut OwnedPage, u8) {
        (self.records.as_mut_ptr(), self.count)
    }

    pub(in crate::app::epub) fn completed(&self, success: bool) {
        assert!(self.records[..usize::from(self.count)]
            .iter()
            .all(|record| record.owner.is_null() == success));
    }
}

impl Drop for Pending {
    fn drop(&mut self) {
        for record in self.records.iter_mut().rev() {
            if !record.owner.is_null() {
                unsafe { destroy(record.owner) };
            }
        }
    }
}
