use xp_ui::{
    ffi::{Buffer, SceneView},
    model::Input,
    network::{ffi, usb, usb_view},
};

/// # Safety
/// State and services are aligned, live and disjoint. Callbacks must not reenter
/// with the working state; publishing a separate model copy is permitted.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_usb_run(
    state: *mut usb::State,
    services: *const usb::Services,
    event: u8,
    input: *const Input,
    home: u8,
) {
    unsafe { ffi::usb_run(state, services, event, input, home) }
}
/// # Safety
/// State is aligned and live for the call.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_usb_prevent_sleep(state: *const usb::State) -> u8 {
    unsafe { ffi::usb_prevent_sleep(state) }
}
/// # Safety
/// All descriptors are aligned and disjoint; commands are exclusively writable.
/// Text callbacks borrow resources for this call without retaining pointers.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_usb_view(
    state: *const usb::State,
    layout: *const usb_view::Layout,
    texts: *const usb_view::TextServices,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    unsafe { ffi::usb_view(state, layout, texts, buffer, out) }
}

/// # Safety
/// Descriptors are aligned, live and disjoint. Commands are exclusively writable.
/// Resource callbacks retain no pointers and may not mutate the descriptors.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_modes_view(
    state: *const xp_ui::model::ListState,
    layout: *const xp_ui::settings::view::Layout,
    resources: *const xp_ui::settings::settings_ffi::Services,
    buffer: *const Buffer,
    next: *mut xp_ui::model::ListState,
    out: *mut SceneView,
    hits: *mut xp_ui::settings::hits::Layout,
) -> u8 {
    unsafe { ffi::modes_view(state, layout, resources, buffer, next, out, hits) }
}
