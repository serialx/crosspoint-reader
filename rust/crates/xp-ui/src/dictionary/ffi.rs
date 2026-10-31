//! Checked private wire entries. Owners serialize borrowed records and output storage.
use super::{definition, paint, popup, selection, view};
use crate::{
    ffi::{Buffer, Metrics, SceneView},
    model::{Effect, Input},
    scene::{Arena, Error, Rect, Refresh, MAX_COMMAND_BYTES},
};
use core::{ffi::c_char, slice};
use xp_core::glyph::GlyphTarget;
unsafe fn words<'a>(ptr: *const selection::Word, count: u32) -> Option<&'a [selection::Word]> {
    if count > u16::MAX as u32 || (count != 0 && ptr.is_null()) {
        return None;
    }
    Some(if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(ptr, count as usize) }
    })
}
unsafe fn build(
    buffer: *const Buffer,
    out: *mut SceneView,
    f: impl FnOnce(&mut Arena<'_>) -> Result<Refresh, Error>,
) -> u8 {
    let (Some(buffer), Some(out)) = (unsafe { buffer.as_ref() }, unsafe { out.as_mut() }) else {
        return 0;
    };
    if buffer.commands.is_null() || buffer.command_capacity as usize > MAX_COMMAND_BYTES / 32 {
        return 0;
    }
    let commands =
        unsafe { slice::from_raw_parts_mut(buffer.commands, buffer.command_capacity as usize) };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    let refresh = match f(&mut arena) {
        Ok(v) => v,
        Err(Error::Capacity) => return 2,
        Err(_) => return 0,
    };
    let scene = match arena.finish(refresh, 1) {
        Ok(v) => v,
        Err(Error::Capacity) => return 2,
        Err(_) => return 0,
    };
    *out = SceneView {
        commands: scene.commands.as_ptr(),
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
/// # Safety
/// State is writable; word records are aligned, readable, and disjoint.
pub unsafe fn selection_init(
    state: *mut selection::State,
    mode: u8,
    ptr: *const selection::Word,
    count: u32,
    width: i32,
    first: i32,
    last: i32,
) {
    if let (Some(state), Some(words)) = (unsafe { state.as_mut() }, unsafe { words(ptr, count) }) {
        *state = selection::State::init(words, mode, width, first, last);
    }
}
/// # Safety
/// Descriptors are valid synchronous loans; mutable state is disjoint.
pub unsafe fn selection_update(
    state: *mut selection::State,
    ptr: *const selection::Word,
    count: u32,
    layout: *const selection::Layout,
    input: *const selection::Input,
) -> selection::Update {
    let (Some(state), Some(words), Some(layout), Some(input)) = (
        unsafe { state.as_mut() },
        unsafe { words(ptr, count) },
        unsafe { layout.as_ref() },
        unsafe { input.as_ref() },
    ) else {
        return selection::Update::default();
    };
    state.update(words, layout, *input)
}
/// # Safety
/// State and input are aligned, live and disjoint.
pub unsafe fn selection_poll(state: *mut selection::State, input: *const Input) -> u8 {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(s), Some(i)) => u8::from(s.poll(*i)),
        _ => 0,
    }
}
/// # Safety
/// A non-null state is aligned and writable.
pub unsafe fn selection_dequeue(state: *mut selection::State) -> u8 {
    unsafe { state.as_mut() }.map_or(0, selection::State::dequeue)
}
/// # Safety
/// State is writable and disjoint from readable word records.
pub unsafe fn selection_buttons(
    state: *mut selection::State,
    ptr: *const selection::Word,
    count: u32,
    buttons: u8,
) -> selection::Update {
    match (unsafe { state.as_mut() }, unsafe { words(ptr, count) }) {
        (Some(s), Some(w)) => s.buttons(w, buttons),
        _ => selection::Update::default(),
    }
}
/// # Safety
/// Descriptors borrow live disjoint storage. State/records remain immutable until painting finishes.
#[allow(clippy::too_many_arguments)]
pub unsafe fn selection_view(
    state: *const selection::State,
    ptr: *const selection::Word,
    count: u32,
    layout: *const selection::Layout,
    old: *const selection::Painted,
    buffer: *const Buffer,
    next: *mut selection::Painted,
    out: *mut SceneView,
) -> u8 {
    let (Some(state), Some(words), Some(layout), Some(old), Some(next)) = (
        unsafe { state.as_ref() },
        unsafe { words(ptr, count) },
        unsafe { layout.as_ref() },
        unsafe { old.as_ref() },
        unsafe { next.as_mut() },
    ) else {
        return 0;
    };
    let mut painted = *old;
    let status = unsafe {
        build(buffer, out, |arena| {
            let (p, mode) = view::selection(state, words, layout, old, arena)?;
            painted = p;
            Ok(if mode == 4 {
                Refresh::None
            } else {
                Refresh::Fast
            })
        })
    };
    if status == 1 {
        *next = painted;
    }
    status
}
/// # Safety
/// Target storage is exclusively writable. Records and callbacks obey the private header contract.
#[allow(clippy::too_many_arguments)]
pub unsafe fn selection_paint(
    state: *const selection::State,
    ptr: *const selection::Word,
    count: u32,
    layout: *const selection::Layout,
    old: *const selection::Painted,
    target: *const GlyphTarget,
    fonts: *const paint::Fonts,
    mode: u8,
) -> u8 {
    let (Some(state), Some(words), Some(layout), Some(old), Some(target), Some(fonts)) = (
        unsafe { state.as_ref() },
        unsafe { words(ptr, count) },
        unsafe { layout.as_ref() },
        unsafe { old.as_ref() },
        unsafe { target.as_ref() },
        unsafe { fonts.as_ref() },
    ) else {
        return 0;
    };
    u8::from(unsafe { paint::paint(state, words, layout, old, target, *fonts, mode) }.is_ok())
}
/// # Safety
/// State and input are valid disjoint synchronous loans.
pub unsafe fn definition_update(
    state: *mut definition::State,
    input: *const Input,
    tap: u8,
    long: u8,
    header: u8,
    width: i32,
    touch: u8,
) -> Effect {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(s), Some(i)) => s.update(*i, tap != 0, long != 0, header != 0, width, touch != 0),
        _ => Effect::default(),
    }
}
/// # Safety
/// Non-null layout is aligned and readable.
pub unsafe fn definition_body(layout: *const definition::Layout) -> Rect {
    unsafe { layout.as_ref() }.map_or(Rect::default(), definition::body)
}
/// # Safety
/// Readable descriptors and writable output buffers are disjoint and live.
pub unsafe fn definition_view(
    state: *const definition::State,
    layout: *const definition::Layout,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(state), Some(layout)) = (unsafe { state.as_ref() }, unsafe { layout.as_ref() })
    else {
        return 0;
    };
    unsafe {
        build(buffer, out, |arena| {
            definition::view(layout, arena)?;
            Ok(if state.message_visible != 0 {
                Refresh::None
            } else {
                Refresh::Fast
            })
        })
    }
}
/// # Safety
/// Non-null state is aligned and writable.
pub unsafe fn lookup_init(state: *mut popup::Lookup) {
    if let Some(s) = unsafe { state.as_mut() } {
        *s = popup::Lookup::new();
    }
}
/// # Safety
/// Non-null state is aligned and writable.
pub unsafe fn lookup_result(
    state: *mut popup::Lookup,
    opened: u8,
    index: u8,
    result: u8,
    found: u8,
    now: u32,
) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.result(opened != 0, index, result, found != 0, now);
    }
}
/// # Safety
/// Non-null state is aligned and readable.
pub unsafe fn lookup_expired(state: *const popup::Lookup, now: u32) -> u8 {
    u8::from(unsafe { state.as_ref() }.is_some_and(|s| s.expired(now)))
}
/// # Safety
/// Message is NUL terminated; callbacks borrow all descriptors synchronously and cannot mutate them.
pub unsafe fn dictionary_popup(
    layout: *const popup::Layout,
    message: *const c_char,
    metrics: *const Metrics,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(layout), Some(metrics)) = (unsafe { layout.as_ref() }, unsafe { metrics.as_ref() })
    else {
        return 0;
    };
    if message.is_null() || metrics.width.is_none() || metrics.line_height.is_none() {
        return 0;
    }
    let text = unsafe { core::ffi::CStr::from_ptr(message) }.to_bytes();
    if text.len() > u16::MAX as usize {
        return 0;
    }
    unsafe {
        build(buffer, out, |arena| {
            popup::view(layout, text, &mut &*metrics, arena)?;
            Ok(Refresh::Fast)
        })
    }
}

/// # Safety
/// State is readable and the output scene/storage are writable and disjoint.
pub unsafe fn lookup_view(
    state: *const popup::Lookup,
    screen: Rect,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let Some(state) = (unsafe { state.as_ref() }) else {
        return 0;
    };
    unsafe {
        build(buffer, out, |arena| {
            arena.push(crate::scene::Command {
                kind: crate::scene::Kind::Clear as u8,
                ..Default::default()
            })?;
            arena.push(crate::scene::Command {
                kind: crate::scene::Kind::Page as u8,
                resource: view::PAGE,
                generation: 1,
                rect: screen,
                ..Default::default()
            })?;
            Ok(if state.popup != 0 {
                Refresh::None
            } else {
                Refresh::Fast
            })
        })
    }
}
