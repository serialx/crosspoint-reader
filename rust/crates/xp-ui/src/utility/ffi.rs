use super::{frontlight_view, view, Feedback, Layout, Screen, State, Texts, Update};
use crate::{
    ffi::{Buffer, Metrics, SceneView},
    model::{Input, Interest},
    scene::{Arena, Error, Refresh, MAX_COMMAND_BYTES, MAX_HITS},
};
use core::{
    ffi::{c_char, c_void},
    slice,
};
#[repr(C)]
pub struct TextServices {
    pub context: *mut c_void,
    pub text: Option<unsafe extern "C" fn(*mut c_void, u32) -> *const c_char>,
    pub text_height: Option<unsafe extern "C" fn(*mut c_void, u32, i32, i16, u8, u8) -> i16>,
}
impl Texts for TextServices {
    fn present(&self, h: u32) -> Result<bool, Error> {
        let f = self.text.ok_or(Error::Invalid)?;
        let p = unsafe { f(self.context, h) };
        if p.is_null() {
            return Err(Error::Invalid);
        }
        Ok(unsafe { *p } != 0)
    }
    fn height(&self, h: u32, font: i32, width: i16, style: u8, lines: u8) -> Result<i16, Error> {
        self.present(h)?;
        let f = self.text_height.ok_or(Error::Invalid)?;
        let n = unsafe { f(self.context, h, font, width, style, lines) };
        if !(0..=4096).contains(&n) {
            return Err(Error::Invalid);
        }
        Ok(n)
    }
}
/// # Safety
/// State is aligned, exclusively writable and initialized by its owning facade.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_utility_init(state: *mut State) -> u8 {
    let Some(s) = (unsafe { state.as_mut() }) else {
        return 0;
    };
    let mut next = *s;
    if !next.init() {
        return 0;
    }
    *s = next;
    1
}
/// # Safety
/// State is an aligned live descriptor for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_utility_interest(state: *const State, now: u32) -> Interest {
    unsafe { state.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// All descriptors are aligned and disjoint. Published hits remain immutable.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_utility_update(
    state: *mut State,
    input: *const Input,
    published: *const SceneView,
    feedback: *const Feedback,
) -> Update {
    let (Some(s), Some(i), Some(f)) = (
        unsafe { state.as_mut() },
        unsafe { input.as_ref() },
        unsafe { feedback.as_ref() },
    ) else {
        return Update::default();
    };
    let hits = if let Some(p) = unsafe { published.as_ref() } {
        if usize::from(p.interaction_count) > MAX_HITS
            || p.interaction_count != 0 && p.interactions.is_null()
        {
            return Update::default();
        }
        Some(if p.interaction_count == 0 {
            &[][..]
        } else {
            unsafe { slice::from_raw_parts(p.interactions, usize::from(p.interaction_count)) }
        })
    } else {
        None
    };
    if Screen::from_byte(s.kind).is_none()
        || s.minimum > s.maximum
        || s.small_step < 0
        || s.large_step < 0
    {
        return Update::default();
    }
    s.update(i, hits, f)
}
/// # Safety
/// Arrays are exclusively writable, aligned, and disjoint from every descriptor.
/// Text services lend immutable strings for this synchronous view only. Failure
/// may change scratch arrays, but never the scene or feedback descriptors.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_utility_view(
    state: *const State,
    layout: *const Layout,
    metrics: *const Metrics,
    texts: *const TextServices,
    buffer: *const Buffer,
    feedback: *mut Feedback,
    out: *mut SceneView,
) -> u8 {
    unsafe { build_view(state, layout, metrics, texts, buffer, feedback, out, view) }
}

/// # Safety
/// The same descriptor and borrowed-text requirements as `xp_ui_utility_view` apply.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_utility_frontlight_view(
    state: *const State,
    layout: *const Layout,
    metrics: *const Metrics,
    texts: *const TextServices,
    buffer: *const Buffer,
    feedback: *mut Feedback,
    out: *mut SceneView,
) -> u8 {
    unsafe {
        build_view(
            state,
            layout,
            metrics,
            texts,
            buffer,
            feedback,
            out,
            frontlight_view,
        )
    }
}

type PaintView = fn(
    &mut Arena<'_>,
    &State,
    &Layout,
    &mut dyn crate::model::Metrics,
    &dyn Texts,
    Feedback,
) -> Result<(Feedback, Refresh), Error>;

#[allow(clippy::too_many_arguments)]
#[inline(never)]
unsafe fn build_view(
    state: *const State,
    layout: *const Layout,
    metrics: *const Metrics,
    texts: *const TextServices,
    buffer: *const Buffer,
    feedback: *mut Feedback,
    out: *mut SceneView,
    paint: PaintView,
) -> u8 {
    let (Some(s), Some(l), Some(mut m), Some(t), Some(b), Some(f), Some(o)) = (
        unsafe { state.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { metrics.as_ref() },
        unsafe { texts.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { feedback.as_mut() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if b.commands.is_null()
        || b.hits.is_null()
        || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32
        || usize::from(b.hit_capacity) > MAX_HITS
        || m.width.is_none()
        || m.line_height.is_none()
        || t.text.is_none()
        || t.text_height.is_none()
    {
        return 0;
    }
    let commands =
        unsafe { slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let hits = unsafe { slice::from_raw_parts_mut(b.hits, usize::from(b.hit_capacity)) };
    let Ok(mut arena) = Arena::new(commands, &mut [], hits) else {
        return 0;
    };
    let Ok((next, refresh)) = paint(&mut arena, s, l, &mut m, t, *f) else {
        return 0;
    };
    let Ok(scene) = arena.finish(refresh, 1) else {
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
        refresh: refresh as u8,
        reserved: 0,
    };
    *f = next;
    1
}

/// # Safety
/// Both states are live, aligned, and disjoint; the first is exclusively writable.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_utility_publish(state: *mut State, rendered: *const State) {
    if let (Some(s), Some(r)) = (unsafe { state.as_mut() }, unsafe { rendered.as_ref() }) {
        s.publish(r);
    }
}
