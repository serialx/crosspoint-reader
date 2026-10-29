//! Private family list resources and borrowed command boundary.
use super::{
    file, file_view, hits,
    rows::{self, Resources},
};
use crate::{
    ffi::{Buffer, SceneView},
    scene::{Arena, Error, Refresh, MAX_COMMAND_BYTES},
};
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Rows {
    pub context: *mut c_void,
    pub window: Option<unsafe extern "C" fn(*mut c_void, u16, u16) -> u8>,
    pub row: Option<unsafe extern "C" fn(*mut c_void, u16, *mut rows::Row) -> u8>,
    pub width: Option<unsafe extern "C" fn(*mut c_void, u32, i32, u8) -> i32>,
    pub height: Option<unsafe extern "C" fn(*mut c_void, u32, i32, u8, u8, i16) -> i32>,
    pub line: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
    pub path: *const u8,
    pub path_len: u32,
    pub position: Option<unsafe extern "C" fn(*mut c_void, i32, u16, u8) -> u8>,
}
impl Rows {
    fn measured(v: i32) -> Result<i32, Error> {
        if (0..=32767).contains(&v) {
            Ok(v)
        } else {
            Err(Error::Invalid)
        }
    }
}
impl Resources for Rows {
    fn position(&mut self, index: i32, count: u16, groups: bool) -> Result<(), Error> {
        if unsafe {
            self.position.ok_or(Error::Invalid)?(self.context, index, count, u8::from(groups))
        } != 0
        {
            Ok(())
        } else {
            Err(Error::Invalid)
        }
    }
    fn window(&mut self, f: u16, n: u16) -> Result<(), Error> {
        if n == 0 || n > 24 {
            return Err(Error::Invalid);
        }
        if unsafe { self.window.ok_or(Error::Invalid)?(self.context, f, n) } != 0 {
            Ok(())
        } else {
            Err(Error::Invalid)
        }
    }
    fn row(&mut self, i: u16) -> Result<rows::Row, Error> {
        let mut out = rows::Row::default();
        if unsafe { self.row.ok_or(Error::Invalid)?(self.context, i, &mut out) } == 0
            || out.label == 0
            || out.icon_size < 0
            || out.icon_size > 128
        {
            return Err(Error::Invalid);
        }
        Ok(out)
    }
    fn width(&mut self, h: u32, f: i32, b: u8) -> Result<i32, Error> {
        Self::measured(unsafe { self.width.ok_or(Error::Invalid)?(self.context, h, f, b) })
    }
    fn height(&mut self, h: u32, f: i32, b: u8, n: u8, w: i16) -> Result<i32, Error> {
        Self::measured(unsafe { self.height.ok_or(Error::Invalid)?(self.context, h, f, b, n, w) })
    }
    fn line(&mut self, f: i32) -> Result<i32, Error> {
        let v = Self::measured(unsafe { self.line.ok_or(Error::Invalid)?(self.context, f) })?;
        if v == 0 || v > 8192 {
            Err(Error::Invalid)
        } else {
            Ok(v)
        }
    }
}
/// # Safety
/// Aligned disjoint descriptors are borrowed for this call. Callback strings stay
/// immutable through painting. Commands are exclusive; output changes only on success.
#[allow(clippy::too_many_arguments)]
#[inline(never)]
pub unsafe fn file_view(
    state: *const file::State,
    facts: *const file::Facts,
    layout: *const file_view::Layout,
    rows: *const Rows,
    buffer: *const Buffer,
    feedback: *mut file_view::Feedback,
    geometry: *mut hits::Layout,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(f), Some(l), Some(rs), Some(b)) = (
        unsafe { state.as_ref() },
        unsafe { facts.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { rows.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if feedback.is_null()
        || geometry.is_null()
        || out.is_null()
        || b.commands.is_null()
        || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32
        || b.hit_capacity != 0
        || rs.path_len > 0x0fff_ffff
        || (rs.path.is_null() && rs.path_len != 0)
    {
        return 0;
    }
    let mut resources = *rs;
    let path = if rs.path_len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(rs.path, rs.path_len as usize) }
    };
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let mut state = *s;
    for pass in 0..=8 {
        let mut hit = hits::Layout::default();
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut hit) else {
            return 0;
        };
        let next = match file_view::view(&mut arena, &state, *f, l, path, &mut resources) {
            Ok(n) => n,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
            return 0;
        };
        if next.list.follow != 0 && pass < 8 {
            state.list = next.list;
            continue;
        }
        let count = scene.commands.len() as u16;
        unsafe {
            feedback.write(next);
            geometry.write(hit);
            out.write(SceneView {
                commands: b.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count: count,
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

/// # Safety
/// Descriptors and callbacks follow the same borrowing contract as file_view.
#[allow(clippy::too_many_arguments)]
#[inline(never)]
pub unsafe fn library_view(
    state: *const super::library::State,
    facts: *const super::library::Facts,
    layout: *const super::library_view::Layout,
    rows: *const Rows,
    buffer: *const Buffer,
    feedback: *mut super::library_view::Feedback,
    geometry: *mut hits::Layout,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(f), Some(l), Some(rs), Some(b)) = (
        unsafe { state.as_ref() },
        unsafe { facts.as_ref() },
        unsafe { layout.as_ref() },
        unsafe { rows.as_ref() },
        unsafe { buffer.as_ref() },
    ) else {
        return 0;
    };
    if feedback.is_null()
        || geometry.is_null()
        || out.is_null()
        || b.commands.is_null()
        || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32
        || b.hit_capacity != 0
        || s.active_tab >= 3
    {
        return 0;
    }
    let mut resources = *rs;
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let mut state = *s;
    for pass in 0..=8 {
        let mut hit = hits::Layout::default();
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut hit) else {
            return 0;
        };
        let next = match super::library_view::view(&mut arena, &state, *f, l, &mut resources) {
            Ok(n) => n,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
            return 0;
        };
        if next.list.follow != 0 && pass < 8 {
            *state.nav_mut() = next.list;
            continue;
        }
        let count = scene.commands.len() as u16;
        unsafe {
            feedback.write(next);
            geometry.write(hit);
            out.write(SceneView {
                commands: b.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count: count,
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
