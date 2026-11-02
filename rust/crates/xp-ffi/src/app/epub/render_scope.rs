use super::{
    state::{Overlay, RenderFrame, SessionAccounting},
    EpubSession, ResourceAccess,
};
use core::{cell::UnsafeCell, ptr::NonNull};
use xp_ui::reader::base::{REFRESH_COUNTER_ONLY, REFRESH_MANUAL, REFRESH_REQUEST_MASK};

struct Publication<'a> {
    owner: &'a EpubSession,
    access: &'a ResourceAccess<'a>,
    frame: NonNull<RenderFrame>,
}

impl Drop for Publication<'_> {
    fn drop(&mut self) {
        self.owner.model(|state| {
            let frame = unsafe { &self.frame.as_ref().base };
            debug_assert!(state.base.refresh_requests & !REFRESH_REQUEST_MASK == 0);
            debug_assert!(frame.refresh_requests & !REFRESH_REQUEST_MASK == 0);
            let pending = state.base.session.pending_forward;
            state.base.session = frame.session;
            state.base.session.pending_forward = pending;
            if state.base.refresh_requests == 0 {
                state.base.refresh_counter = frame.refresh_counter;
            }
            state.base.end_ready = frame.end_ready;
            state.base.rendered = frame.rendered;
        });
        unsafe { (*self.owner.guarded(self.access)).active_frame = None };
    }
}

impl EpubSession {
    // The application retains RenderLock until this method returns.
    // Keep the EPUB frame out of the shared App render dispatch.
    #[inline(never)]
    pub(in crate::app) unsafe fn render(&self) -> bool {
        if !self.model(|state| state.ready) {
            return false;
        }
        let access = self.borrowed_resources();
        if !self.render_load_failure(&access) {
            self.render_with(|access| self.render_base(access, || self.render_book(access)));
        }
        self.complete_render();
        true
    }

    pub(super) fn complete_render(&self) {
        self.model(|state| {
            if matches!(state.overlay, Overlay::Page) {
                state.scene_gate.failed = false;
            }
        });
    }

    // The application render caller holds RenderLock for this entire invocation.
    pub(super) unsafe fn render_with(&self, paint: impl FnOnce(&ResourceAccess<'_>)) -> bool {
        if !self.model(|state| state.ready) {
            return false;
        }
        let access = self.borrowed_resources();
        assert!(
            (*self.guarded(&access)).active_frame.is_none(),
            "nested render frame"
        );
        let frame_storage = UnsafeCell::new(RenderFrame {
            base: self.model(|state| {
                debug_assert!(state.base.refresh_requests & !REFRESH_REQUEST_MASK == 0);
                let frame = state.base;
                state.base.session.pending_forward = false;
                state.base.refresh_requests = 0;
                frame
            }),
        });
        let frame = NonNull::new(frame_storage.get()).expect("render frame storage");
        (*self.guarded(&access)).active_frame = Some(frame);
        let _publication = Publication {
            owner: self,
            access: &access,
            frame,
        };
        paint(&access);
        true
    }

    fn frame(&self, access: &ResourceAccess<'_>) -> NonNull<RenderFrame> {
        unsafe {
            (*self.guarded(access))
                .active_frame
                .expect("active render frame")
        }
    }

    pub(super) fn render_refresh_counter(&self, access: &ResourceAccess<'_>) -> i32 {
        unsafe { self.frame(access).as_ref().base.refresh_counter }
    }

    pub(super) fn set_render_refresh_counter(&self, access: &ResourceAccess<'_>, value: i32) {
        unsafe { (*self.frame(access).as_ptr()).base.refresh_counter = value };
    }

    pub(super) fn take_forced_refresh(&self, access: &ResourceAccess<'_>) -> bool {
        let frame = self.frame(access);
        unsafe {
            let requests = &mut (*frame.as_ptr()).base.refresh_requests;
            let forced = *requests & REFRESH_MANUAL != 0;
            *requests &= !REFRESH_MANUAL;
            forced
        }
    }

    pub(super) fn queue_forced_refresh(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.force)(hook.context) };
        }
        self.model(|state| {
            state.base.refresh_counter = 1;
            state.base.refresh_requests |= REFRESH_MANUAL;
        });
    }

    pub(super) fn set_refresh_counter(&self, value: i32) {
        self.model(|state| {
            state.base.refresh_counter = value;
            state.base.refresh_requests |= REFRESH_COUNTER_ONLY;
        });
    }

    pub(super) fn mark_page_rendered(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.mark)(hook.context) };
        }
        let frame = self.frame(access);
        self.model(|state| {
            state.base.rendered = true;
            unsafe { (*frame.as_ptr()).base.rendered = true };
        });
    }

    pub(super) fn mark_end_ready(&self, access: &ResourceAccess<'_>) {
        let frame = self.frame(access);
        self.model(|state| {
            state.base.end_ready = true;
            unsafe { (*frame.as_ptr()).base.end_ready = true };
        });
    }

    pub(super) fn accepts_remote_input(&self) -> bool {
        self.model(|state| state.base.rendered)
    }

    pub(super) fn note_page_turn(&self, forward: bool, succeeded: bool) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.note)(hook.context, u8::from(forward), u8::from(succeeded)) };
        }
        self.model(|state| state.base.session.note(forward, succeeded));
    }

    pub(super) fn snapshot_session(&self) -> SessionAccounting {
        self.model(|state| state.base.session)
    }

    pub(super) fn reset_session(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.reset)(hook.context) };
        }
        self.model(|state| state.base.session = SessionAccounting::default());
    }

    pub(super) fn complete_session(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.complete)(hook.context) };
            return;
        }
        let now = unsafe { (self.runtime.now_ms)() };
        let epoch = unsafe { (self.leaves.trusted_now)() };
        let progress = self.progress_basis_points();
        unsafe {
            (*self.frame(access).as_ptr())
                .base
                .session
                .render(now, epoch, progress)
        };
    }
}
