use super::{
    state::{Effect, Event, State},
    view, Feedback, Layout, Services,
};
use crate::{
    ffi::{Buffer, SceneView},
    model::{Input, Interest},
    scene::{Arena, Error},
    settings::hits,
};

/// # Safety
/// The aligned output is exclusively writable and live for this call.
pub unsafe fn init(state: *mut State, kind: u8, recovery: u8) {
    if kind <= 3 {
        if let Some(s) = unsafe { state.as_mut() } {
            *s = State::new(kind, recovery);
        }
    }
}
/// # Safety
/// State is aligned, exclusively writable, and live for this call.
pub unsafe fn service(state: *mut State, event: u8, a: u32, b: u32) -> u8 {
    match (unsafe { state.as_mut() }, Event::from_byte(event)) {
        (Some(s), Some(e)) if s.valid() => s.service(e, a, b),
        _ => 0,
    }
}
/// # Safety
/// State is aligned and live for this call.
pub unsafe fn interest(state: *const State, now: u32) -> Interest {
    unsafe { state.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// Descriptors are aligned, live, and disjoint. State is exclusively writable.
pub unsafe fn input(
    state: *mut State,
    input: *const Input,
    feedback: *const Feedback,
    published: u8,
    row_count: i32,
    family_count: i32,
) -> Effect {
    match (
        unsafe { state.as_mut() },
        unsafe { input.as_ref() },
        unsafe { feedback.as_ref() },
    ) {
        (Some(s), Some(i), Some(f)) if s.valid() && row_count >= 0 && family_count >= 0 => {
            s.input(*i, f, published != 0, row_count, family_count)
        }
        _ => Effect::default(),
    }
}
/// # Safety
/// State is aligned and live for this call.
pub unsafe fn sleep(state: *const State) -> u8 {
    unsafe { state.as_ref() }.map_or(0, |s| u8::from(s.sleep()))
}
/// # Safety
/// State is aligned and live for this call.
pub unsafe fn delay(state: *const State) -> u8 {
    unsafe { state.as_ref() }.map_or(0, |s| u8::from(s.delay()))
}
/// # Safety
/// Descriptors and scratch arrays are aligned, live, and disjoint; scratch/output
/// storage is exclusive. Callbacks cannot reenter or retain pointers. Strings
/// remain immutable until synchronous painting completes. Failure preserves output.
pub unsafe fn build(
    state: *const State,
    layout: *const Layout,
    services: *const Services,
    buffer: *const Buffer,
    feedback: *mut Feedback,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(l), Some(r), Some(b), Some(f), Some(o)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { services.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { feedback.as_mut() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if b.commands.is_null()
        || b.command_capacity > 256
        || b.hit_capacity != 0
        || r.rows.line.is_none()
        || r.rows.width.is_none()
        || r.text.is_none()
    {
        return 0;
    }
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let mut rows = hits::Layout::default();
    let Ok(mut arena) = Arena::with_hit_sink(commands, &mut rows) else {
        return 0;
    };
    let mut next = Feedback::default();
    let refresh = match view::view(&mut arena, s, l, r, &mut next) {
        Ok(v) => v,
        Err(Error::Capacity) => return 2,
        Err(_) => return 0,
    };
    let Ok(scene) = arena.finish(refresh, 1) else {
        return 0;
    };
    let count = scene.commands.len() as u16;
    next.rows = rows;
    *f = next;
    *o = SceneView {
        commands: b.commands,
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: count,
        text_len: 0,
        interaction_count: 0,
        refresh: refresh as u8,
        reserved: 0,
    };
    1
}
/// # Safety
/// Descriptors are aligned, live and disjoint; state is exclusively writable.
pub unsafe fn publish(state: *mut State, snapshot: *const State, feedback: *const Feedback) {
    if let (Some(s), Some(old), Some(f)) = (
        unsafe { state.as_mut() },
        unsafe { snapshot.as_ref() },
        unsafe { feedback.as_ref() },
    ) {
        if s.valid() && old.valid() && s.kind == old.kind && s.phase == old.phase {
            s.scene_failed = 0;
            if old.is_list() {
                let nav = s.nav_mut();
                if nav.navigation_revision == old.nav().navigation_revision
                    && nav.selected == old.nav().selected
                {
                    nav.selected = f.list.selected;
                }
                nav.publish(&f.list, old.nav().pending_scroll);
            }
            s.last_percent = f.last_percent;
        }
    }
}

/// # Safety
/// State is aligned and live for this call.
pub unsafe fn percent(state: *const State) -> u32 {
    unsafe { state.as_ref() }.map_or(0, |s| s.percent())
}
