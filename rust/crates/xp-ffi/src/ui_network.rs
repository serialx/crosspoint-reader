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

use xp_ui::network::{calibre, calibre_view};
/// # Safety
/// Descriptors live and disjoint; callbacks do not reenter state.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_calibre_run(
    state: *mut calibre::State,
    services: *const calibre::Services,
    event: u8,
    input: *const Input,
    value: u8,
) {
    unsafe { ffi::calibre_run(state, services, event, input, value) }
}
/// # Safety
/// Descriptors live and disjoint; command storage exclusively borrowed.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_calibre_view(
    state: *const calibre::State,
    layout: *const calibre_view::Layout,
    now: u32,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    unsafe { ffi::calibre_view(state, layout, now, buffer, out) }
}

use xp_ui::network::{web, web_view};
/// # Safety
/// Descriptors aligned, live and disjoint; callbacks do not reenter run.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_web_run(
    state: *mut web::State,
    services: *const web::Services,
    event: u8,
    value: u8,
) {
    unsafe { ffi::web_run(state, services, event, value) }
}
/// # Safety
/// Descriptors aligned, live and disjoint; latch exclusively borrowed.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_web_cancel(
    latch: *mut u8,
    input: *const Input,
    home: u8,
) -> u8 {
    unsafe { ffi::web_cancel(latch, input, home) }
}
/// # Safety
/// Descriptors aligned, live and disjoint; commands exclusively borrowed.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_web_view(
    state: *const web::State,
    layout: *const web_view::Layout,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    unsafe { ffi::web_view(state, layout, buffer, out) }
}

use xp_ui::network::{wifi, wifi_view};
/// # Safety
/// All descriptors are aligned, live and disjoint. Resource callbacks borrow
/// their strings for this call. Commands are exclusively writable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_view(
    state: *const wifi::State,
    layout: *const wifi_view::Layout,
    services: *const wifi_view::Services,
    buffer: *const Buffer,
    next: *mut xp_ui::model::ListState,
    out: *mut SceneView,
    hits: *mut wifi_view::Hits,
) -> u8 {
    unsafe { ffi::wifi_view(state, layout, services, buffer, next, out, hits) }
}

/// # Safety
/// Descriptors are aligned, live and disjoint; mutable pointers are exclusive.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_run(
    state: *mut wifi::State,
    services: *const wifi::Services,
    event: u8,
    value: u32,
    input: *const Input,
) {
    unsafe { ffi::wifi_run(state, services, event, value, input) }
}

/// # Safety
/// Descriptors are aligned, live and disjoint; mutable pointers are exclusive.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_publish(
    model: *mut wifi::State,
    next: *mut wifi::State,
) {
    unsafe { ffi::wifi_publish(model, next) }
}

/// # Safety
/// Descriptors are aligned, live and disjoint; mutable pointers are exclusive.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_view_publish(
    model: *mut wifi::State,
    source: *const wifi::State,
    next: *const xp_ui::model::ListState,
) {
    unsafe { ffi::wifi_view_publish(model, source, next) }
}

/// # Safety
/// Descriptors are aligned, live and disjoint; mutable pointers are exclusive.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_hit(
    hits: *const wifi_view::Hits,
    state: *const wifi::State,
    input: *mut Input,
) -> u8 {
    unsafe { ffi::wifi_hit(hits, state, input) }
}

#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_network_wifi_row_before(a: u8, ar: i32, b: u8, br: i32) -> u8 {
    u8::from(wifi::row_before(a != 0, ar, b != 0, br))
}

/// # Safety
/// State is aligned, live and exclusively writable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_prepare_view(state: *mut wifi::State) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.prepare_view();
    }
}

/// # Safety
/// Descriptors are aligned and live; output is exclusively writable and disjoint.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_network_wifi_budget(
    state: *const wifi::State,
    layout: *const wifi_view::Layout,
    line: i32,
    out: *mut wifi_view::Budget,
) -> u8 {
    let (Some(s), Some(l), Some(o)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    match wifi_view::budget(s, l, line) {
        Ok(b) => {
            *o = b;
            1
        }
        Err(_) => 0,
    }
}

#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_network_wifi_signal_strength(rssi: i32) -> u8 {
    wifi::signal_strength(rssi)
}
