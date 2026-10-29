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

use crate::{
    model::ListState,
    scene::Error,
    settings::{hits::Layout as Hits, settings_ffi::Services, view::Layout},
};
/// # Safety
/// Descriptors are aligned, live and disjoint. Commands are exclusively writable.
/// Resource callbacks retain no pointers and may not mutate the descriptors.
pub unsafe fn modes_view(
    state: *const ListState,
    layout: *const Layout,
    services: *const Services,
    buffer: *const Buffer,
    next: *mut ListState,
    out: *mut SceneView,
    hits: *mut Hits,
) -> u8 {
    let (Some(state), Some(layout), Some(services), Some(buffer)) = (
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
        || buffer.commands.is_null()
        || buffer.command_capacity > 256
        || buffer.hit_capacity != 0
        || services.row.is_none()
        || services.width.is_none()
        || services.height.is_none()
        || services.line.is_none()
    {
        return 0;
    }
    let mut working = *state;
    let mut resources = *services;
    for pass in 0..9 {
        let commands = unsafe {
            core::slice::from_raw_parts_mut(buffer.commands, usize::from(buffer.command_capacity))
        };
        let mut routing = Hits::default();
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut routing) else {
            return 0;
        };
        working = match super::modes::view(
            &mut arena,
            working,
            *layout,
            &mut resources,
            services.count,
        ) {
            Ok(next) => next,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        if working.follow != 0 && pass < 8 {
            continue;
        }
        let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
            return 0;
        };
        let command_count = scene.commands.len() as u16;
        unsafe {
            next.write(working);
            hits.write(routing);
            out.write(SceneView {
                commands: buffer.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count,
                text_len: 0,
                interaction_count: 0,
                refresh: Refresh::Fast as u8,
                reserved: 0,
            });
        }
        return 1;
    }
    0
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

use super::{wifi, wifi_view};
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
    let mut working = *s;
    let mut resources = *c;
    if s.phase == wifi::Phase::List as u8 {
        working.list.active = -1;
    }
    // Wi-Fi's original SDK caller paints once and retains follow feedback for
    // the next requested frame. Re-emit that same layout only for a held touch.
    for pass in 0..2 {
        let commands =
            unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
        let mut routing = wifi_view::Hits {
            generation: s.generation,
            count: s.count,
            phase: s.phase,
            ..wifi_view::Hits::default()
        };
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut routing) else {
            return 0;
        };
        let (mut updated, refresh) = match wifi_view::view(&mut arena, &working, l, &mut resources)
        {
            Ok(result) => result,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        let Ok(scene) = arena.finish(refresh, 1) else {
            return 0;
        };
        let count = scene.commands.len() as u16;
        if pass == 0
            && s.phase == wifi::Phase::List as u8
            && s.list.active >= 0
            && routing.rows.count != 0
        {
            working.list.active = s.list.active + routing.rows.first_index;
            continue;
        }
        updated.active = s.list.active;
        unsafe {
            next.write(updated);
            hits.write(routing);
            out.write(SceneView {
                commands: b.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count: count,
                text_len: 0,
                interaction_count: 0,
                refresh: refresh as u8,
                reserved: 0,
            });
        }
        return 1;
    }
    0
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
