extern crate alloc;

use super::{
    app_abi, selection_metadata::SelectionMetadata, state::RenderFrame, EpubSession,
    ResourceAccess, Text,
};
use alloc::vec::Vec;
use core::{cell::UnsafeCell, ffi::c_void, ptr::NonNull};

#[derive(Default)]
pub(super) struct Guarded {
    pub(super) native: Option<NonNull<c_void>>,
    pub(super) book: Option<NonNull<c_void>>,
    pub(super) section: Option<NonNull<c_void>>,
    pub(super) toolbar: Option<NonNull<c_void>>,
    pub(super) overlay_popup: Option<NonNull<c_void>>,
    pub(super) failure_popup: Option<NonNull<c_void>>,
    pub(super) end_options: Option<NonNull<c_void>>,
    // Loop is the sole writer; mutations and render reads hold ResourceAccess.
    pub(super) bookmarks: Option<NonNull<c_void>>,
    pub(super) active_frame: Option<NonNull<RenderFrame>>,
    pub(super) anchor: Text,
    pub(super) more_actions: [u8; 19],
    pub(super) more_count: u8,
    // Loop-only retained popup payload; render never reads these choices.
    pub(super) font_sizes: Vec<u8>,
    pub(super) selection_metadata: SelectionMetadata,
}

#[derive(Default)]
#[repr(transparent)]
pub(super) struct GuardedCell(UnsafeCell<Guarded>);

impl GuardedCell {
    #[cfg(feature = "app-test")]
    pub(super) fn get_mut(&mut self) -> &mut Guarded {
        self.0.get_mut()
    }

    // Fixtures call only between operations or while holding ResourceAccess.
    #[cfg(feature = "app-test")]
    pub(super) unsafe fn fixture(&self) -> &Guarded {
        &*self.0.get()
    }
}

impl EpubSession {
    #[cfg(feature = "app-test")]
    fn fixture_book_pointer(&self, book: Option<NonNull<c_void>>) -> Option<NonNull<c_void>> {
        // Box reclamation may retag the fixture allocation. Derive the inline
        // pointer from this owner borrow rather than reusing its published tag.
        let slot = self.legacy_fixture.get().cast::<c_void>();
        book.map(|book| {
            if book.as_ptr() == slot {
                NonNull::new(slot).expect("inline fixture storage")
            } else {
                book
            }
        })
    }

    // Keep raw storage private to this module. Every field loan ends before a
    // caller can invoke a native callback or update another resource.
    fn guarded_ptr(&self, access: &ResourceAccess<'_>) -> *mut Guarded {
        self.check_resources(access);
        self.guarded.0.get()
    }

    pub(super) fn set_native(&self, access: &ResourceAccess<'_>, value: Option<NonNull<c_void>>) {
        unsafe { (*self.guarded_ptr(access)).native = value };
    }

    pub(super) fn take_native(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).native.take() }
    }

    pub(super) fn book(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        let book = unsafe { (*self.guarded_ptr(access)).book };
        #[cfg(feature = "app-test")]
        let book = self.fixture_book_pointer(book);
        book
    }

    pub(super) fn set_book(&self, access: &ResourceAccess<'_>, value: Option<NonNull<c_void>>) {
        unsafe { (*self.guarded_ptr(access)).book = value };
    }

    pub(super) fn take_book(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        let book = unsafe { (*self.guarded_ptr(access)).book.take() };
        #[cfg(feature = "app-test")]
        let book = self.fixture_book_pointer(book);
        book
    }

    pub(super) fn section(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).section }
    }

    pub(super) fn set_section(&self, access: &ResourceAccess<'_>, value: Option<NonNull<c_void>>) {
        unsafe { (*self.guarded_ptr(access)).section = value };
    }

    pub(super) fn take_section(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).section.take() }
    }

    pub(super) fn toolbar(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).toolbar }
    }

    pub(super) fn set_toolbar(&self, access: &ResourceAccess<'_>, value: Option<NonNull<c_void>>) {
        unsafe { (*self.guarded_ptr(access)).toolbar = value };
    }

    pub(super) fn take_toolbar(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).toolbar.take() }
    }

    pub(super) fn overlay_popup(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).overlay_popup }
    }

    pub(super) fn set_overlay_popup(
        &self,
        access: &ResourceAccess<'_>,
        value: Option<NonNull<c_void>>,
    ) {
        unsafe { (*self.guarded_ptr(access)).overlay_popup = value };
    }

    pub(super) fn take_overlay_popup(
        &self,
        access: &ResourceAccess<'_>,
    ) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).overlay_popup.take() }
    }

    pub(super) fn failure_popup(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).failure_popup }
    }

    pub(super) fn set_failure_popup(
        &self,
        access: &ResourceAccess<'_>,
        value: Option<NonNull<c_void>>,
    ) {
        unsafe { (*self.guarded_ptr(access)).failure_popup = value };
    }

    pub(super) fn take_failure_popup(
        &self,
        access: &ResourceAccess<'_>,
    ) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).failure_popup.take() }
    }

    pub(super) fn end_options(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).end_options }
    }

    pub(super) fn set_end_options(
        &self,
        access: &ResourceAccess<'_>,
        value: Option<NonNull<c_void>>,
    ) {
        unsafe { (*self.guarded_ptr(access)).end_options = value };
    }

    pub(super) fn take_end_options(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).end_options.take() }
    }

    pub(super) fn bookmarks(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).bookmarks }
    }

    pub(super) fn set_bookmarks(
        &self,
        access: &ResourceAccess<'_>,
        value: Option<NonNull<c_void>>,
    ) {
        unsafe { (*self.guarded_ptr(access)).bookmarks = value };
    }

    pub(super) fn take_bookmarks(&self, access: &ResourceAccess<'_>) -> Option<NonNull<c_void>> {
        unsafe { (*self.guarded_ptr(access)).bookmarks.take() }
    }

    pub(super) fn native(&self, access: &ResourceAccess<'_>) -> *mut c_void {
        unsafe {
            (*self.guarded_ptr(access))
                .native
                .expect("reader resources")
                .as_ptr()
        }
    }

    pub(super) fn active_frame(&self, access: &ResourceAccess<'_>) -> Option<NonNull<RenderFrame>> {
        unsafe { (*self.guarded_ptr(access)).active_frame }
    }

    pub(super) fn set_active_frame(
        &self,
        access: &ResourceAccess<'_>,
        frame: Option<NonNull<RenderFrame>>,
    ) {
        unsafe { (*self.guarded_ptr(access)).active_frame = frame };
    }

    // The wire borrow lasts until the next anchor mutation under this resource lock.
    pub(super) fn anchor(&self, access: &ResourceAccess<'_>) -> app_abi::Text {
        unsafe { (*self.guarded_ptr(access)).anchor.wire() }
    }

    pub(super) fn anchor_is_empty(&self, access: &ResourceAccess<'_>) -> bool {
        unsafe { (*self.guarded_ptr(access)).anchor.bytes().is_empty() }
    }

    pub(super) fn set_anchor(&self, access: &ResourceAccess<'_>, anchor: Text) {
        unsafe { (*self.guarded_ptr(access)).anchor = anchor };
    }

    pub(super) fn clear_anchor(&self, access: &ResourceAccess<'_>) {
        unsafe { (*self.guarded_ptr(access)).anchor.clear() };
    }

    pub(super) fn set_more_actions(
        &self,
        access: &ResourceAccess<'_>,
        actions: [u8; 19],
        count: u8,
    ) {
        let data = self.guarded_ptr(access);
        unsafe {
            (*data).more_actions = actions;
            (*data).more_count = count;
        }
    }

    pub(super) fn more_count(&self, access: &ResourceAccess<'_>) -> u8 {
        unsafe { (*self.guarded_ptr(access)).more_count }
    }

    pub(super) fn more_action(&self, access: &ResourceAccess<'_>, row: i32) -> Option<u8> {
        self.check_resources(access);
        self.more_action_at(row)
    }

    pub(super) fn set_font_sizes(&self, access: &ResourceAccess<'_>, sizes: Vec<u8>) {
        unsafe { (*self.guarded_ptr(access)).font_sizes = sizes };
    }

    pub(super) fn clear_selection_metadata(&self, access: &ResourceAccess<'_>) {
        unsafe { (*self.guarded_ptr(access)).selection_metadata = SelectionMetadata::default() };
    }

    // Loop is the only writer of these slots. Render borrows their values under
    // ResourceAccess; these field-only reads never borrow the whole storage.

    pub(super) fn loop_book(&self) -> Option<NonNull<c_void>> {
        let book = unsafe { core::ptr::addr_of!((*self.guarded.0.get()).book).read() };
        #[cfg(feature = "app-test")]
        let book = self.fixture_book_pointer(book);
        book
    }

    pub(super) fn loop_bookmark_handle(&self) -> Option<NonNull<c_void>> {
        unsafe { core::ptr::addr_of!((*self.guarded.0.get()).bookmarks).read() }
    }

    pub(super) fn loop_toolbar(&self) -> Option<NonNull<c_void>> {
        unsafe { core::ptr::addr_of!((*self.guarded.0.get()).toolbar).read() }
    }

    pub(super) fn loop_overlay_popup(&self) -> Option<NonNull<c_void>> {
        unsafe { core::ptr::addr_of!((*self.guarded.0.get()).overlay_popup).read() }
    }

    pub(super) fn loop_failure_popup(&self) -> Option<NonNull<c_void>> {
        unsafe { core::ptr::addr_of!((*self.guarded.0.get()).failure_popup).read() }
    }

    // Render installs this slot before publishing end_ready; only loop removes it.
    pub(super) fn published_end_options(&self) -> Option<NonNull<c_void>> {
        unsafe { core::ptr::addr_of!((*self.guarded.0.get()).end_options).read() }
    }

    pub(super) fn loop_more_count(&self) -> u8 {
        unsafe { core::ptr::addr_of!((*self.guarded.0.get()).more_count).read() }
    }

    pub(super) fn loop_more_action(&self, row: i32) -> Option<u8> {
        self.more_action_at(row)
    }

    fn more_action_at(&self, row: i32) -> Option<u8> {
        if row < 0 || row >= i32::from(self.loop_more_count()) {
            return None;
        }
        unsafe { Some((*self.guarded.0.get()).more_actions[row as usize]) }
    }

    // Font choices and selection payload are loop-owned; render never reads them.
    pub(super) fn loop_font_size(&self, index: usize) -> Option<u8> {
        let sizes = unsafe { &(*self.guarded.0.get()).font_sizes };
        sizes.get(index).copied()
    }

    pub(super) fn set_selection_metadata(&self, metadata: SelectionMetadata) {
        unsafe { (*self.guarded.0.get()).selection_metadata = metadata };
    }

    pub(super) fn take_selection_metadata(&self) -> SelectionMetadata {
        unsafe { core::mem::take(&mut (*self.guarded.0.get()).selection_metadata) }
    }
}
