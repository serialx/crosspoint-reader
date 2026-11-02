use super::{abi, EpubSession, ResourceAccess};
use core::mem::MaybeUninit;
use xp_ui::{
    reader::render::{Command, PhysicalRenderer, Renderer},
    scene::Refresh,
};

impl EpubSession {
    pub(super) fn render_operation(&self, access: &ResourceAccess<'_>, command: Command) -> u32 {
        let services = unsafe { &*(self.leaves.render_services)(self.native(access)) };
        unsafe { PhysicalRenderer::new(services) }.execute(command)
    }

    pub(super) fn discard_overlay_page(&self, access: &ResourceAccess<'_>) {
        self.guarded(access);
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.settings_overrides {
            unsafe { (hook.discard)(hook.context) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.discard_overlay)(hook.context) };
            self.model(|state| state.page_stored = false);
            return;
        }
        if !self.model(|state| state.page_stored) {
            return;
        }
        unsafe { (self.leaves.render_discard_bw)() };
        self.model(|state| state.page_stored = false);
    }

    pub(super) fn push_overlay_refresh(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_hooks {
            unsafe { (hooks.push)(hooks.context) };
            return;
        }
        let mut geometry = MaybeUninit::<abi::XpEpubGeometry>::uninit();
        unsafe { (self.leaves.render_geometry)(geometry.as_mut_ptr()) };
        let asynchronous = unsafe { geometry.assume_init() }.async_refresh != 0;
        self.render_operation(
            access,
            if asynchronous {
                Command::DisplayAsync(Refresh::Fast)
            } else {
                Command::Display(Refresh::Fast)
            },
        );
        if asynchronous {
            self.model(|state| state.refresh_pending = true);
        }
    }

    pub(super) fn settle_overlay_refresh(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.settle_overlay)(hook.context) };
            return;
        }
        if !self.model(|state| core::mem::take(&mut state.refresh_pending)) {
            return;
        }
        self.render_operation(access, Command::Cleanup);
    }

    pub(super) fn paint_overlay_popup(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            unsafe {
                (hooks.popup)(hooks.context);
            }
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.popup)(hooks.context) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.settings_overrides {
            unsafe { (hook.paint)(hook.context) };
            return;
        }
        let access = self.lock_resources();
        self.settle_overlay_refresh(&access);
        let popup = unsafe {
            (*self.guarded(&access))
                .overlay_popup
                .expect("overlay popup")
        };
        unsafe { (self.leaves.popup_paint)(popup.as_ptr()) };
        self.push_overlay_refresh(&access);
    }

    pub(super) fn disable_fast_initial_refresh(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.disable_fast)(hook.context) };
            return;
        }
        self.set_refresh_counter(0);
    }

    pub(super) fn apply_initial_orientation(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.settings_overrides {
            unsafe { (hook.initial)(hook.context) };
            return;
        }
        let mut settings = MaybeUninit::<abi::XpEpubSettings>::uninit();
        unsafe { (self.leaves.settings_snapshot)(0, settings.as_mut_ptr()) };
        unsafe { (self.leaves.render_orientation)(settings.assume_init().orientation) };
    }
}
