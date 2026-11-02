extern crate alloc;
use super::{abi, app_abi, EpubSession, ResourceAccess};
use alloc::boxed::Box;
use core::ptr::NonNull;

struct Pending(Option<Box<EpubSession>>);
impl Drop for Pending {
    fn drop(&mut self) {
        if let Some(owner) = &self.0 {
            let access = owner.entry_resources();
            owner.close_owned_components(&access);
        }
    }
}

impl EpubSession {
    pub(in crate::app) fn try_owned(
        path: &[u8],
        fast_initial: bool,
        leaves: &'static abi::XpEpubLeaves,
        runtime: &'static app_abi::Runtime,
        resources: &'static app_abi::Resources,
    ) -> Option<Box<Self>> {
        let mut pending = Pending(Some(Self::try_new(
            path, false, 0, leaves, runtime, resources,
        )?));
        let owner = pending.0.as_ref().expect("pending owner");
        let access = owner.entry_resources();
        let native = NonNull::new(unsafe { (leaves.resources_open)() })?;
        unsafe { (*owner.guarded(&access)).native = Some(native) };
        if fast_initial {
            let render = unsafe { (leaves.render_services)(native.as_ptr()).as_ref() }
                .expect("physical root render services");
            let frequency = unsafe {
                render.read.expect("physical root query")(
                    render.context,
                    xp_ui::reader::render::Query::Frequency as u8,
                )
            } as i32;
            // Entry owns the unpublished model; no render can observe it yet.
            unsafe { (*owner.state.get()).base.refresh_counter = frequency.max(2) };
        }
        let bookmarks = NonNull::new(unsafe { (leaves.bookmarks_open)() })?;
        unsafe { (*owner.guarded(&access)).bookmarks = Some(bookmarks) };
        let overlay = NonNull::new(unsafe { (leaves.popup_open)() })?;
        unsafe { (*owner.guarded(&access)).overlay_popup = Some(overlay) };
        let failure = NonNull::new(unsafe { (leaves.popup_open)() })?;
        unsafe { (*owner.guarded(&access)).failure_popup = Some(failure) };
        drop(access);
        pending.0.take()
    }

    fn close_owned_components(&self, access: &ResourceAccess<'_>) {
        assert!(unsafe { (*self.guarded(access)).active_frame.is_none() });
        if let Some(toolbar) = unsafe { (*self.guarded(access)).toolbar.take() } {
            unsafe { (self.leaves.toolbar_close)(toolbar.as_ptr()) };
        }
        if let Some(popup) = unsafe { (*self.guarded(access)).failure_popup.take() } {
            unsafe { (self.leaves.popup_close)(popup.as_ptr()) };
        }
        if let Some(popup) = unsafe { (*self.guarded(access)).overlay_popup.take() } {
            unsafe { (self.leaves.popup_close)(popup.as_ptr()) };
        }
        if let Some(bookmarks) = unsafe { (*self.guarded(access)).bookmarks.take() } {
            unsafe { (self.leaves.bookmarks_close)(bookmarks.as_ptr()) };
        }
        if let Some(native) = unsafe { (*self.guarded(access)).native.take() } {
            unsafe { (self.leaves.resources_close)(native.as_ptr()) };
        }
    }

    // Called by App under its lifetime lock, after publication excludes render.
    #[inline(never)]
    pub(in crate::app) unsafe fn exit_owned(&self) {
        let access = self.borrowed_resources();
        self.exit(&access);
        self.teardown_book(&access);
        self.model(|state| state.ready = false);
    }

    // Only App-owned sessions enter here; standalone fixtures borrow their handles.
    pub(in crate::app) fn destroy_owned(&self) {
        // Pending owners never entered; entered owners were exited under the App lock.
        let access = self.entry_resources();
        assert!(self.book(&access).is_none());
        assert!(self.section(&access).is_none());
        self.close_owned_components(&access);
    }

    // Application calls this while failed stack reservation still holds its lock.
    pub(in crate::app) unsafe fn child_install_failed(&self) {
        let access = self.borrowed_resources();
        self.model(|state| state.child = super::state::ChildIntent::None);
        (*self.guarded(&access)).selection_metadata = Default::default();
        self.request_update();
    }
}

impl EpubSession {
    pub(in crate::app) fn app_facts(&self, input: bool) -> app_abi::LegacyFacts {
        app_abi::LegacyFacts {
            // Native reader identity uses the unclassified activity kind.
            kind: 255,
            reader: 1,
            remote_input: u8::from(self.accepts_remote_input()),
            prevent_sleep: 0,
            exclusive_storage: 0,
            skip_delay: 0,
            input_over_page: u8::from(input && self.input_over_page()),
        }
    }

    pub(in crate::app) fn app_remote_turn(&self, forward: bool, chapter: bool) -> bool {
        let mut input = core::mem::MaybeUninit::<abi::XpEpubInputFacts>::uninit();
        unsafe { (self.leaves.input_facts)(input.as_mut_ptr()) };
        if unsafe { input.assume_init() }.remote_click != 0 {
            return false;
        }
        if chapter {
            self.skip_pages(if forward { 1 } else { -1 })
        } else {
            self.page_turn(forward)
        }
    }
}
