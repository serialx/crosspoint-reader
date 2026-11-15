//! Host-only compatibility transport; the live owner calls safe Rust directly.
use super::{wifi, wifi_view};
use crate::{
    ffi::{Buffer, SceneView},
    model::{Input, ListState},
    scene::Error,
};
/// # Safety
/// All descriptors are aligned, live and disjoint. Commands are exclusively
/// writable. Resource callbacks borrow their strings only for this call.
#[allow(clippy::too_many_arguments)]
pub unsafe fn wifi_view(
    state: *const wifi::State,
    layout: *const wifi_view::Layout,
    services: *const wifi_view::Services,
    buffer: *const Buffer,
    next: *mut ListState,
    out: *mut SceneView,
    hits: *mut wifi_view::Hits,
) -> u8 {
    let (Some(s), Some(l), Some(c), Some(b)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { services.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if next.is_null()
        || out.is_null()
        || hits.is_null()
        || b.commands.is_null()
        || b.command_capacity > 256
        || b.hit_capacity != 0
        || c.rows.row.is_none()
        || c.rows.width.is_none()
        || c.rows.height.is_none()
        || c.rows.line.is_none()
        || c.text.is_none()
    {
        return 0;
    }
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let mut resources = *c;
    let mut updated = ListState::default();
    let mut routing = wifi_view::Hits::default();
    let (count, refresh) =
        match wifi_view::build(commands, s, l, &mut resources, &mut updated, &mut routing) {
            Ok(result) => result,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
    unsafe {
        next.write(updated);
        hits.write(routing);
        out.write(SceneView {
            commands: b.commands,
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: 1,
            command_count: count as u16,
            text_len: 0,
            interaction_count: 0,
            refresh: refresh as u8,
            reserved: 0,
        });
    }
    1
}
/// # Safety
/// State and services are aligned, live and disjoint. Publishing may update the
/// working state with viewport feedback; callbacks must not reenter the engine.
pub unsafe fn wifi_run(
    state: *mut wifi::State,
    services: *const wifi::Services,
    event: u8,
    value: u32,
    input: *const Input,
) {
    let (Some(s), Some(c)) = (unsafe { state.as_mut() }, unsafe { services.as_ref() }) else {
        return;
    };
    if c.call.is_none() || c.publish.is_none() {
        return;
    }
    let mut leaves = wifi::Services {
        context: c.context,
        call: c.call,
        publish: c.publish,
    };
    s.run(
        &mut leaves,
        event,
        value,
        unsafe { input.as_ref() }.copied().unwrap_or_default(),
    );
}
/// # Safety
/// Both states are live, exclusively writable and disjoint.
pub unsafe fn wifi_publish(model: *mut wifi::State, next: *mut wifi::State) {
    if let (Some(m), Some(n)) = (unsafe { model.as_mut() }, unsafe { next.as_mut() }) {
        m.publish_model(n)
    }
}
/// # Safety
/// Descriptors are live and disjoint. Model is exclusively writable.
pub unsafe fn wifi_view_publish(
    model: *mut wifi::State,
    source: *const wifi::State,
    next: *const ListState,
) {
    if let (Some(m), Some(s), Some(n)) = (
        unsafe { model.as_mut() },
        unsafe { source.as_ref() },
        unsafe { next.as_ref() },
    ) {
        m.publish_view(s, n)
    }
}
/// # Safety
/// Descriptors are live and disjoint. Input is exclusively writable.
pub unsafe fn wifi_hit(
    hits: *const wifi_view::Hits,
    state: *const wifi::State,
    input: *mut Input,
) -> u8 {
    let (Some(h), Some(s), Some(i)) = (
        unsafe { hits.as_ref() },
        unsafe { state.as_ref() },
        unsafe { input.as_mut() },
    ) else {
        return 0;
    };
    let Some(hit) = h.hit(s, i.touch_x, i.touch_y) else {
        return 0;
    };
    i.action = hit.action;
    i.value = if hit.action == 1
        && i.touch_flags & crate::model::TOUCH_PRESSED != 0
        && i.touch_flags & crate::model::TOUCH_RELEASED == 0
    {
        hit.value - h.rows.first_index
    } else {
        hit.value
    };
    1
}
