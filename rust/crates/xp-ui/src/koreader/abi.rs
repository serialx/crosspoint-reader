use super::{run, state, view, Layout, Options, Services, State};
use crate::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest},
    scene::{Arena, Refresh, MAX_COMMAND_BYTES, MAX_HITS},
};
use core::{ffi::c_char, slice};
/// # Safety
/// Aligned live descriptors follow the private KOReader family contract.
pub unsafe fn init(p: *mut State, kind: u8, smart: u8, plugin: u8) {
    if let Some(s) = unsafe { p.as_mut() } {
        *s = State {
            kind,
            smart,
            plugin,
            ..State::default()
        };
    }
}
/// # Safety
/// The model is exclusively writable for this call.
pub unsafe fn publish(p: *mut State, phase: u8, selected: u8) {
    if let Some(s) = unsafe { p.as_mut() } {
        s.publish(phase, selected);
    }
}
/// # Safety
/// The model is exclusively writable for this call.
pub unsafe fn mark_return(p: *mut State, now: u32) {
    if let Some(s) = unsafe { p.as_mut() } {
        s.mark_return(now);
    }
}
/// # Safety
/// The model is a live descriptor for this call.
pub unsafe fn busy(p: *const State) -> u8 {
    unsafe { p.as_ref() }.map_or(0, |s| u8::from(s.busy()))
}
/// # Safety
/// The model is a live descriptor for this call.
pub unsafe fn interest(p: *const State, now: u32) -> Interest {
    unsafe { p.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// Descriptors are aligned and disjoint; the model is exclusively writable.
pub unsafe fn update(p: *mut State, i: *const Input) -> Effect {
    match (unsafe { p.as_mut() }, unsafe { i.as_ref() }) {
        (Some(s), Some(i)) => s.update(i),
        _ => Effect::default(),
    }
}
/// # Safety
/// Options and services remain live for the synchronous call and its callbacks.
pub unsafe fn sync(o: *const Options, s: *const Services) {
    if let (Some(o), Some(s)) = (unsafe { o.as_ref() }, unsafe { s.as_ref() }) {
        run::sync(*o, *s);
    }
}
/// # Safety
/// Options and services remain live for the synchronous call and its callbacks.
pub unsafe fn upload(o: *const Options, s: *const Services) {
    if let (Some(o), Some(s)) = (unsafe { o.as_ref() }, unsafe { s.as_ref() }) {
        run::upload(*o, *s);
    }
}
/// # Safety
/// Services remain live for the synchronous call and its callbacks.
pub unsafe fn auth(signup: u8, s: *const Services) {
    if let Some(s) = unsafe { s.as_ref() } {
        run::auth(signup, *s);
    }
}
/// # Safety
/// Services remain live for the synchronous call and its callbacks.
pub unsafe fn save(s: *const Services) {
    if let Some(s) = unsafe { s.as_ref() } {
        run::save(*s);
    }
}
/// # Safety
/// The owner lends disjoint command/hit arrays and live aligned descriptors.
/// State/output are unchanged on failure; array contents are scratch until success.
pub unsafe fn view(s: *const State, l: *const Layout, b: *const Buffer, o: *mut SceneView) -> u8 {
    let (Some(s), Some(l), Some(b), Some(o)) = (
        unsafe { s.as_ref() },
        unsafe { l.as_ref() },
        unsafe { b.as_ref() },
        unsafe { o.as_mut() },
    ) else {
        return 0;
    };
    if b.command_capacity == 0
        || usize::from(b.command_capacity)
            > MAX_COMMAND_BYTES / core::mem::size_of::<crate::scene::Command>()
        || b.commands.is_null()
        || usize::from(b.hit_capacity) > MAX_HITS
        || b.hit_capacity != 0 && b.hits.is_null()
    {
        return 0;
    }
    let commands =
        unsafe { slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let hits = if b.hit_capacity == 0 {
        &mut [][..]
    } else {
        unsafe { slice::from_raw_parts_mut(b.hits, usize::from(b.hit_capacity)) }
    };
    let Ok(mut arena) = Arena::new(commands, &mut [], hits) else {
        return 0;
    };
    if view::view(&mut arena, *s, *l).is_err() {
        return 0;
    }
    let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
        return 0;
    };
    *o = SceneView {
        commands: b.commands,
        text: core::ptr::null(),
        interactions: b.hits,
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: scene.interactions.len() as u16,
        refresh: 1,
        reserved: 0,
    };
    1
}
pub fn text(language: u8, handle: u32) -> *const c_char {
    xp_core::i18n::get(language, u16::try_from(handle).unwrap_or(u16::MAX))
}
pub fn edit(row: u8, value: u8, credentials: u8) -> Effect {
    state::edit(row, value, credentials)
}
pub fn label(row: u8) -> u16 {
    if row < 12 {
        u16::from(row) + 32
    } else {
        0
    }
}
/// # Safety
/// Nonempty text is readable for len bytes.
pub unsafe fn url_empty(p: *const u8, len: u32) -> u8 {
    if p.is_null() || len > 8 {
        return 0;
    }
    let value = unsafe { slice::from_raw_parts(p, len as usize) };
    u8::from(value == b"https://" || value == b"http://")
}

const _: () = {
    assert!(core::mem::size_of::<State>() == 12);
    assert!(core::mem::size_of::<Options>() == 12);
    assert!(core::mem::offset_of!(Options, local_percentage) == 0);
    assert!(core::mem::offset_of!(Options, smart) == 4);
};
