use super::{usb, usb_view};
use crate::{
    ffi::{Buffer, SceneView},
    model::Input,
    scene::{Arena, Refresh, MAX_COMMAND_BYTES},
};

/// # Safety
/// Descriptors are aligned, live and disjoint. The state is exclusively borrowed.
/// Callbacks may publish a copy, but must not reenter using the working state.
pub unsafe fn usb_run(
    state: *mut usb::State,
    services: *const usb::Services,
    event: u8,
    input: *const Input,
    home: u8,
) {
    let (Some(s), Some(c)) = (unsafe { state.as_mut() }, unsafe { services.as_ref() }) else {
        return;
    };
    if c.call.is_none() || c.publish.is_none() {
        return;
    }
    let mut leaves = usb::Services {
        context: c.context,
        call: c.call,
        publish: c.publish,
    };
    s.run(
        &mut leaves,
        event,
        unsafe { input.as_ref() }.copied().unwrap_or_default(),
        home != 0,
    );
}
/// # Safety
/// State is aligned and live for this call.
pub unsafe fn usb_prevent_sleep(state: *const usb::State) -> u8 {
    unsafe { state.as_ref() }.map_or(0, |s| u8::from(s.prevent_sleep()))
}
/// # Safety
/// Commands are exclusively writable and disjoint from descriptors. Text
/// callbacks retain no pointers and perform no mutation of the descriptors.
pub unsafe fn usb_view(
    state: *const usb::State,
    layout: *const usb_view::Layout,
    texts: *const usb_view::TextServices,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(l), Some(t), Some(b), Some(o)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { texts.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if b.commands.is_null() || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32 {
        return 0;
    }
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    if usb_view::view(&mut arena, s, l, t).is_err() {
        return 0;
    }
    let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
        return 0;
    };
    *o = SceneView {
        commands: b.commands,
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        refresh: Refresh::Fast as u8,
        reserved: 0,
    };
    1
}

use super::{calibre, calibre_view};

/// # Safety
/// Descriptors are aligned, live and disjoint; callbacks must not reenter state.
pub unsafe fn calibre_run(
    state: *mut calibre::State,
    services: *const calibre::Services,
    event: u8,
    input: *const Input,
    value: u8,
) {
    let (Some(state), Some(services)) = (unsafe { state.as_mut() }, unsafe { services.as_ref() })
    else {
        return;
    };
    if services.call.is_none() || services.publish.is_none() || services.upload.is_none() {
        return;
    }
    let mut leaves = calibre::Services {
        context: services.context,
        call: services.call,
        publish: services.publish,
        upload: services.upload,
    };
    state.run(
        &mut leaves,
        event,
        unsafe { input.as_ref() }.copied().unwrap_or_default(),
        value != 0,
    );
}
/// # Safety
/// Descriptors are aligned and disjoint; command storage is exclusively borrowed.
pub unsafe fn calibre_view(
    state: *const calibre::State,
    layout: *const calibre_view::Layout,
    now: u32,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(state), Some(layout), Some(buffer), Some(out)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if buffer.commands.is_null() || usize::from(buffer.command_capacity) > MAX_COMMAND_BYTES / 32 {
        return 0;
    }
    let commands = unsafe {
        core::slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity))
    };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    if calibre_view::view(&mut arena, state, layout, now).is_err() {
        return 0;
    }
    let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
        return 0;
    };
    *out = SceneView {
        commands: buffer.commands,
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        refresh: Refresh::Fast as u8,
        reserved: 0,
    };
    1
}

use super::{web, web_view};

/// # Safety
/// Descriptors live, aligned, disjoint; callbacks must not reenter run.
pub unsafe fn web_run(
    state: *mut web::State,
    services: *const web::Services,
    event: u8,
    value: u8,
) {
    let (Some(state), Some(services)) = (unsafe { state.as_mut() }, unsafe { services.as_ref() })
    else {
        return;
    };
    if services.call.is_none() || services.publish.is_none() {
        return;
    }
    let mut leaves = web::Services {
        context: services.context,
        call: services.call,
        publish: services.publish,
    };
    state.run(&mut leaves, event, value);
}
/// # Safety
/// Latch and input live, aligned, disjoint. Latch is exclusively writable.
pub unsafe fn web_cancel(latch: *mut u8, input: *const Input, home: u8) -> u8 {
    let (Some(latch), Some(input)) = (unsafe { latch.as_mut() }, unsafe { input.as_ref() }) else {
        return 0;
    };
    u8::from(web::cancel(latch, *input, home != 0))
}
/// # Safety
/// Descriptors live, aligned and disjoint; commands exclusively writable.
pub unsafe fn web_view(
    state: *const web::State,
    layout: *const web_view::Layout,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(state), Some(layout), Some(buffer), Some(out)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if buffer.commands.is_null() || usize::from(buffer.command_capacity) > MAX_COMMAND_BYTES / 32 {
        return 0;
    }
    let commands = unsafe {
        core::slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity))
    };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    let Ok(refresh) = web_view::view(&mut arena, state, layout) else {
        return 0;
    };
    let Ok(scene) = arena.finish(refresh, 1) else {
        return 0;
    };
    *out = SceneView {
        commands: buffer.commands,
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        refresh: refresh as u8,
        reserved: 0,
    };
    1
}
