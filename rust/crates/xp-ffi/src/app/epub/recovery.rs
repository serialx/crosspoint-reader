use super::{
    app_abi,
    settings::{label_at, LabelSource, Labels},
    state::{ChildIntent, LoadFailure, PopupIntent},
    EpubSession, ResourceAccess,
};
use core::{ffi::c_void, ptr::NonNull};
use xp_core::i18n::ids;
use xp_ui::{app::Transition, reader::render::Command};

unsafe extern "C" fn failure_choice(context: *mut c_void, index: i32) {
    (&*context.cast::<EpubSession>()).choose_failure(index);
}

impl EpubSession {
    pub(super) fn request_update(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.navigation_hooks {
            unsafe { (hooks.update)(hooks.context) };
            return;
        }
        super::super::request_update();
    }

    pub(super) fn finish(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.navigation_hooks {
            unsafe { (hooks.finish)(hooks.context) };
            return;
        }
        let app = super::super::app().expect("application initialized");
        app.input.borrow_mut().reset();
        app.navigation.queue(Transition::Pop);
    }

    pub(super) fn push_child(&self, child: NonNull<c_void>, intent: ChildIntent) {
        self.model(|state| state.child = intent);
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.navigation_hooks {
            unsafe { (hooks.push)(hooks.context, child.as_ptr()) };
            return;
        }
        let app = super::super::app().expect("application initialized");
        let owner = app.adopt(child.as_ptr()).expect("non-null child");
        app.input.borrow_mut().reset();
        app.navigation.queue(Transition::Push(owner));
    }

    pub(super) fn replace_child(&self, child: NonNull<c_void>) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            unsafe { (hooks.replace)(hooks.context, child.as_ptr()) };
            return;
        }
        let app = super::super::app().expect("application initialized");
        let owner = app.adopt(child.as_ptr()).expect("non-null child");
        app.replace(owner);
    }

    fn reopen_reader(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.navigation_hooks {
            unsafe { (hooks.reopen)(hooks.context, self.path().wire()) };
            return;
        }
        let app = super::super::app().expect("application initialized");
        app.navigate(app_abi::Launch::new(
            app_abi::READER,
            app_abi::LaunchPayload::Reader(app_abi::ReaderLaunch {
                path: self.path().wire(),
                fast_initial: 0,
            }),
        ));
    }

    fn show_failure_choices(
        &self,
        access: &ResourceAccess<'_>,
        message: app_abi::Text,
        labels: &[app_abi::Text],
    ) {
        let mut source = LabelSource {
            owner: self,
            labels: Labels::Borrowed(labels),
        };
        let popup = self.failure_popup(access).expect("failure popup");
        unsafe {
            (self.leaves.popup_show)(
                popup.as_ptr(),
                app_abi::Text::from_bytes(&b"\0"[..0]),
                message,
                labels.len() as u32,
                0,
                label_at,
                (&mut source as *mut LabelSource<'_>).cast(),
                failure_choice,
                (self as *const Self).cast_mut().cast(),
            );
        }
    }

    pub(super) fn handle_load_failure(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.failure)(hook.context) != 0 };
        }
        let (message, offer_sync) = match self.model(|state| state.failure) {
            LoadFailure::None => return false,
            LoadFailure::Protected => (ids::STR_DRM_PROTECTED_FILE, false),
            LoadFailure::Expired => (ids::STR_LOAN_EXPIRED, false),
            LoadFailure::Unverified => (ids::STR_LOAN_TIME_UNVERIFIED, true),
        };
        self.model(|state| state.popup = PopupIntent::LoadFailure { offer_sync });
        unsafe {
            let labels = [
                (self.resources.translate)(if offer_sync {
                    ids::STR_CLOCK_SYNC_NOW
                } else {
                    ids::STR_OK_BUTTON
                }),
                (self.resources.translate)(ids::STR_OK_BUTTON),
            ];
            let message = (self.resources.translate)(message);
            let access = self.lock_resources();
            self.show_failure_choices(&access, message, &labels[..if offer_sync { 2 } else { 1 }]);
        }
        self.request_update();
        true
    }

    pub(super) fn begin_loan_time_sync(&self) {
        let child = NonNull::new(unsafe { (self.leaves.child_wifi)() });
        if let Some(child) = child {
            self.push_child(child, ChildIntent::Wifi);
        } else {
            unsafe {
                (self.leaves.log)(
                    1,
                    app_abi::Text::from_bytes(b"ERS"),
                    app_abi::Text::from_bytes(b"OOM: Wi-Fi selection for loan time sync"),
                )
            };
            self.finish();
        }
    }

    pub(super) fn choose_failure(&self, index: i32) {
        let offer_sync = match self.model(|state| state.popup) {
            PopupIntent::LoadFailure { offer_sync } => offer_sync,
            PopupIntent::RetryTime => true,
            _ => return,
        };
        if offer_sync && index == 0 {
            self.begin_loan_time_sync();
        } else {
            self.finish();
        }
    }

    pub(super) fn loan_wifi_result(&self, cancelled: bool) {
        if cancelled || unsafe { (self.leaves.wifi_connected)() } == 0 {
            self.finish();
            return;
        }
        let synced = unsafe {
            let message = (self.resources.translate)(ids::STR_SYNCING_TIME);
            (self.resources.popup_draw)(message);
            let synced = (self.leaves.trusted_sync)(5000) != 0;
            (self.leaves.wifi_disconnect)(0);
            (self.runtime.delay_ms)(30);
            synced
        };
        if !synced {
            self.model(|state| state.popup = PopupIntent::RetryTime);
            unsafe {
                let labels = [
                    (self.resources.translate)(ids::STR_RETRY),
                    (self.resources.translate)(ids::STR_OK_BUTTON),
                ];
                let message = (self.resources.translate)(ids::STR_CLOCK_SYNC_FAIL);
                let access = self.lock_resources();
                self.show_failure_choices(&access, message, &labels);
            }
            self.request_update();
            return;
        }
        unsafe {
            (self.leaves.state_set_open_path)(self.path().wire());
            (self.leaves.state_save)();
            (self.leaves.restart_reader)();
        }
        self.reopen_reader();
    }

    pub(super) fn render_load_failure(&self, access: &ResourceAccess<'_>) -> bool {
        let popup = self.failure_popup(access).expect("failure popup");
        if unsafe { (self.leaves.popup_active)(popup.as_ptr()) } == 0 {
            return false;
        }
        self.render_operation(access, Command::Clear(255));
        unsafe { (self.leaves.popup_process_render)(popup.as_ptr()) };
        true
    }
}
