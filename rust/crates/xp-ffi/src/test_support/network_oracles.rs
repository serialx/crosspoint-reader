//! Test-only bridges to the safe Wi-Fi policy and canonical scene builder.
use xp_ui::network::{legacy_wifi as ffi, wifi, wifi_view};
use xp_ui::{
    ffi::{Buffer, SceneView},
    model::Input,
};
/// # Safety
/// All descriptors are aligned, live and disjoint. Resource callbacks borrow
/// their strings for this call. Commands are exclusively writable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_test_network_wifi_view(
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
pub unsafe extern "C" fn xp_test_network_wifi_run(
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
pub unsafe extern "C" fn xp_test_network_wifi_publish(
    model: *mut wifi::State,
    next: *mut wifi::State,
) {
    unsafe { ffi::wifi_publish(model, next) }
}

/// # Safety
/// Descriptors are aligned, live and disjoint; mutable pointers are exclusive.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_test_network_wifi_view_publish(
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
pub unsafe extern "C" fn xp_test_network_wifi_hit(
    hits: *const wifi_view::Hits,
    state: *const wifi::State,
    input: *mut Input,
) -> u8 {
    unsafe { ffi::wifi_hit(hits, state, input) }
}

#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_test_network_wifi_row_before(a: u8, ar: i32, b: u8, br: i32) -> u8 {
    u8::from(wifi::row_before(a != 0, ar, b != 0, br))
}

/// # Safety
/// State is aligned, live and exclusively writable.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_test_network_wifi_prepare_view(state: *mut wifi::State) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.prepare_view();
    }
}

/// # Safety
/// Descriptors are aligned and live; output is exclusively writable and disjoint.
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_test_network_wifi_budget(
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
pub extern "C" fn xp_test_network_wifi_signal_strength(rssi: i32) -> u8 {
    wifi::signal_strength(rssi)
}
