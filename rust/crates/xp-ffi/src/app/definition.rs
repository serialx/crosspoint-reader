use super::{
    abi, ActivityResult, App, ChildResultSlot, Definition, DefinitionSession, Owner,
    ReaderChildResult,
};
use crate::app::epub::{abi::XpEpubSettings, children::definition_physical::NativeTransfer};
use core::{mem::MaybeUninit, ptr::NonNull};
use xp_ui::app::Transition;

pub(super) fn facts() -> abi::LegacyFacts {
    abi::LegacyFacts {
        kind: u8::MAX,
        reader: 0,
        remote_input: 0,
        prevent_sleep: 0,
        exclusive_storage: 0,
        skip_delay: 0,
        input_over_page: 0,
    }
}
impl App {
    #[inline(never)]
    pub(super) fn definition_enter(&self, target: NonNull<DefinitionSession>) {
        unsafe { target.as_ref().enter(self.runtime, self.epub) };
    }
    #[inline(never)]
    pub(super) fn definition_exit(&self, target: NonNull<DefinitionSession>) {
        unsafe { target.as_ref().exit(self.epub, self.resources.font_owner) };
    }
    #[inline(never)]
    pub(super) fn definition_input(
        &self,
        target: NonNull<DefinitionSession>,
        home_gesture: bool,
        light_panel_gesture: bool,
    ) {
        if home_gesture {
            self.go_home(0, false);
            return;
        }
        if light_panel_gesture {
            self.open_control_center(true, false, Some(facts().kind));
            return;
        }
        let mut settings = MaybeUninit::<XpEpubSettings>::uninit();
        unsafe { (self.epub.settings_snapshot)(0, settings.as_mut_ptr()) };
        let touch = unsafe { settings.assume_init() }.touch_controls != 0;
        let child = unsafe {
            target
                .as_ref()
                .input(self.runtime, self.epub, touch, self.resources.font_owner)
        };
        if !child.is_null() {
            let owner = self.adopt(child).expect("native selection owner");
            self.input.borrow_mut().reset();
            self.navigation.queue(Transition::Push(owner));
        } else if unsafe { *target.as_ref().result.get() }.is_some() {
            self.input.borrow_mut().reset();
            self.navigation.queue(Transition::Pop);
        }
        if self.navigation.drain(self) {
            self.flush_update();
        }
    }
    #[inline(never)]
    pub(super) fn definition_render(&self, target: NonNull<DefinitionSession>) -> bool {
        unsafe { target.as_ref().render(self.resources, self.epub) }.is_ok()
    }
    #[inline(never)]
    pub(super) fn definition_result(
        &self,
        target: NonNull<DefinitionSession>,
        result: *const abi::XpAppResultView,
    ) {
        if let Some(result) =
            unsafe { result.as_ref() }.filter(|result| result.kind == abi::RESULT_CLIPPING)
        {
            unsafe {
                target
                    .as_ref()
                    .selection_result(self.runtime, self.epub, result)
            };
        }
    }
    #[inline(never)]
    pub(super) fn definition_selection_failed(&self, target: NonNull<DefinitionSession>) {
        let _guard = DefinitionSession::lock(self.runtime);
        unsafe { target.as_ref().selection_failed(self.runtime) };
    }
}
#[inline(never)]
pub(super) fn with_result(
    target: NonNull<DefinitionSession>,
    consume: impl FnOnce(ActivityResult),
) {
    // Copy the small result before the continuation destroys the owner; no owner reference escapes.
    let cancelled = unsafe { (*target.as_ref().result.get()).take() }.unwrap_or(false);
    let mut result = Some(ReaderChildResult::DefinitionClosed { cancelled });
    consume(ActivityResult::Child(ChildResultSlot(NonNull::from(
        &mut result,
    ))));
}

/// # Safety
/// Transfer points to live native strings; title/text and the transfer are borrowed synchronously.
#[no_mangle]
pub unsafe extern "C" fn xp_app_definition_open_native(transfer: *mut NativeTransfer) -> u8 {
    let Some(app) = super::app() else {
        return 0;
    };
    if transfer.is_null() {
        return 0;
    }
    let Some(owner) = (unsafe {
        Definition::try_native(
            app.runtime,
            &app.epub.child_selection_physical.definition,
            transfer,
        )
    }) else {
        return 0;
    };
    app.input.borrow_mut().reset();
    app.navigation
        .queue(Transition::Push(Owner::Definition(owner)));
    1
}

#[cfg(feature = "app-test")]
#[no_mangle]
pub extern "C" fn xp_app_test_current_definition() -> *mut core::ffi::c_void {
    match super::app().and_then(|app| app.navigation.current()) {
        Some(super::Target::Definition(target)) => target.as_ptr().cast(),
        _ => core::ptr::null_mut(),
    }
}
#[cfg(feature = "app-test")]
#[no_mangle]
pub extern "C" fn xp_app_test_definition_result(cancelled: u8) -> u8 {
    match super::app().and_then(|app| app.navigation.current()) {
        Some(super::Target::Definition(target)) => {
            unsafe { *target.as_ref().result.get() = Some(cancelled != 0) };
            1
        }
        _ => 0,
    }
}

#[cfg(feature = "app-test")]
#[no_mangle]
pub extern "C" fn xp_app_test_published_definition() -> *mut core::ffi::c_void {
    super::app().map_or(core::ptr::null_mut(), |app| {
        app.signals.with(|signals| match signals.target {
            Some(super::Target::Definition(target)) => target.as_ptr().cast(),
            _ => core::ptr::null_mut(),
        })
    })
}
#[cfg(feature = "app-test")]
#[no_mangle]
/// # Safety
/// Lookup storage must remain live until the installed Definition is destroyed.
pub unsafe extern "C" fn xp_app_test_definition_install_lookup(
    lookup: *mut core::ffi::c_void,
) -> u8 {
    let Some(app) = super::app() else {
        return 0;
    };
    let Some(owner) = (unsafe {
        Definition::try_lookup(
            app.runtime,
            &app.epub.child_selection_physical.definition,
            lookup.cast(),
        )
    }) else {
        return 0;
    };
    app.navigation
        .queue(Transition::Push(Owner::Definition(owner)));
    1
}
