use super::{
    labels,
    preview::{self, Geometry, Key},
    view::{self, Layout},
    Edit, State, Values,
};
use crate::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest},
    reader_chrome::{
        hits::Layout as Hits,
        list::{Resources, Row},
    },
    reader_chrome_ffi::Rows,
    scene::{Arena, Error, Rect, Refresh, MAX_COMMAND_BYTES},
};
use core::{ffi::c_char, ptr, slice};
use xp_core::para::paragraph::Paragraph;
struct Adapter(Rows);
impl Adapter {
    fn measured(v: i32) -> Result<i32, Error> {
        if (0..=32767).contains(&v) {
            Ok(v)
        } else {
            Err(Error::Invalid)
        }
    }
}
impl Resources for Adapter {
    fn window(&mut self, first: u16, requested: u16) -> Result<(), Error> {
        let f = self.0.window.ok_or(Error::Invalid)?;
        if requested == 0
            || requested > self.0.window_capacity
            || u32::from(first) + u32::from(requested) > u32::from(self.0.count)
            || unsafe { f(self.0.context, first, requested) } == 0
        {
            return Err(Error::Invalid);
        }
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<Row, Error> {
        let f = self.0.row.ok_or(Error::Invalid)?;
        let mut out = Row::default();
        if index >= self.0.count
            || unsafe { f(self.0.context, index, &mut out) } == 0
            || out.label == 0
            || out.toggle > 1
            || out.checked > 1
        {
            return Err(Error::Invalid);
        }
        Ok(out)
    }
    fn width(&mut self, h: u32, f: i32, b: u8) -> Result<i32, Error> {
        let cb = self.0.text_width.ok_or(Error::Invalid)?;
        Self::measured(unsafe { cb(self.0.context, h, f, b) })
    }
    fn height(&mut self, h: u32, f: i32, b: u8, l: u8, w: i16) -> Result<i32, Error> {
        let cb = self.0.text_height.ok_or(Error::Invalid)?;
        Self::measured(unsafe { cb(self.0.context, h, f, b, l, w) })
    }
    fn line(&mut self, f: i32) -> Result<i32, Error> {
        let cb = self.0.line_height.ok_or(Error::Invalid)?;
        let n = Self::measured(unsafe { cb(self.0.context, f) })?;
        if n > 0 && n <= 8192 {
            Ok(n)
        } else {
            Err(Error::Invalid)
        }
    }
}
/// # Safety
/// All non-null descriptors are aligned, live and disjoint for this call.
/// Writable descriptors are exclusive. Resource callbacks cannot retain or reenter.
pub unsafe fn view(
    s: *const State,
    l: *const Layout,
    r: *const Rows,
    b: *const Buffer,
    next: *mut State,
    out: *mut SceneView,
    hits: *mut Hits,
) -> u8 {
    let (Some(s), Some(l), Some(r), Some(b)) = (
        unsafe { s.as_ref() },
        unsafe { l.as_ref() },
        unsafe { r.as_ref() },
        unsafe { b.as_ref() },
    ) else {
        return 0;
    };
    if next.is_null()
        || out.is_null()
        || hits.is_null()
        || b.commands.is_null()
        || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32
        || b.hit_capacity != 0
        || !s.valid()
        || r.window_capacity == 0
        || r.window_capacity > 64
    {
        return 0;
    }
    let mut snapshot = *s;
    for pass in 0..9 {
        let mut h = Hits::new(1, r.generation);
        let commands =
            unsafe { slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut h) else {
            return 0;
        };
        let mut resources = Adapter(*r);
        snapshot = match view::view(
            &mut arena,
            snapshot,
            *l,
            &mut resources,
            r.count,
            r.window_capacity,
            r.generation,
        ) {
            Ok(s) => s,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        if snapshot.tabs[usize::from(snapshot.tab)].follow != 0 && pass < 8 {
            continue;
        }
        let Ok(scene) = arena.finish(Refresh::Fast, r.generation) else {
            return 0;
        };
        unsafe {
            next.write(snapshot);
            out.write(SceneView {
                commands: b.commands,
                text: ptr::null(),
                interactions: ptr::null(),
                generation: r.generation,
                command_count: scene.commands.len() as u16,
                text_len: 0,
                interaction_count: 0,
                refresh: 1,
                reserved: 0,
            });
            hits.write(h);
        }
        return 1;
    }
    0
}
/// # Safety
/// Writable state is aligned and exclusive, or null.
pub unsafe fn init(
    s: *mut State,
    tab: u8,
    family: i32,
    size: i32,
    fonts: u16,
    sizes: u16,
    language: u8,
) {
    if tab >= 4
        || fonts == 0
        || sizes == 0
        || !(0..i32::from(fonts)).contains(&family)
        || !(0..i32::from(sizes)).contains(&size)
    {
        return;
    }
    if let Some(s) = unsafe { s.as_mut() } {
        *s = State::new(tab, family, size, fonts, sizes, language);
    }
}
/// # Safety
/// State is aligned and readable, or null.
pub unsafe fn interest(s: *const State, now: u32) -> Interest {
    unsafe { s.as_ref() }.map_or_else(Interest::default, |s| s.interest(now))
}
/// # Safety
/// Input is readable; writable state is exclusive and disjoint, or null.
pub unsafe fn update(s: *mut State, i: *const Input) -> Effect {
    match (unsafe { s.as_mut() }, unsafe { i.as_ref() }) {
        (Some(s), Some(i)) => s.update(*i),
        _ => Effect::default(),
    }
}
/// # Safety
/// States are aligned and disjoint; destination is exclusive, or null.
pub unsafe fn publish(s: *mut State, n: *const State, scroll: i32) {
    if let (Some(s), Some(n)) = (unsafe { s.as_mut() }, unsafe { n.as_ref() }) {
        s.publish(n, scroll);
    }
}
/// # Safety
/// Descriptors are readable and aligned, or null.
pub unsafe fn edit(s: *const State, row: i32, v: *const Values) -> Edit {
    match (unsafe { s.as_ref() }, unsafe { v.as_ref() }) {
        (Some(s), Some(v)) => s.edit(row, *v),
        _ => Edit::default(),
    }
}
/// # Safety
/// Scratch is exclusive for capacity bytes, or null with zero capacity.
pub unsafe fn label(
    language: u8,
    kind: u8,
    index: u8,
    value: u8,
    scratch: *mut c_char,
    capacity: u16,
) -> *const c_char {
    if scratch.is_null() && capacity != 0 {
        return ptr::null();
    }
    let bytes = if capacity == 0 {
        &mut [][..]
    } else {
        unsafe { slice::from_raw_parts_mut(scratch.cast(), usize::from(capacity)) }
    };
    labels::label(language, kind, index, value, bytes)
}
/// # Safety
/// Key is readable and output exclusive/disjoint, or null.
#[allow(clippy::too_many_arguments)]
pub unsafe fn geometry(
    bounds: Rect,
    padding: i16,
    gap: i16,
    label_height: i32,
    line_height: i32,
    advance: i32,
    key: *const Key,
    out: *mut Geometry,
) {
    if let (Some(key), Some(out)) = (unsafe { key.as_ref() }, unsafe { out.as_mut() }) {
        if let Some(g) = preview::geometry(
            bounds,
            padding,
            gap,
            label_height,
            line_height,
            advance,
            *key,
        ) {
            *out = g;
        }
    }
}
/// # Safety
/// Keys are aligned and readable, or null.
pub unsafe fn same(a: *const Key, b: *const Key) -> u8 {
    match (unsafe { a.as_ref() }, unsafe { b.as_ref() }) {
        (Some(a), Some(b)) => u8::from(a == b),
        _ => 0,
    }
}
/// # Safety
/// Paragraph is live, aligned and exclusive. Text is terminated and readable.
pub unsafe fn feed(p: *mut Paragraph, text: *const c_char) {
    if let Some(p) = unsafe { p.as_mut() } {
        unsafe { preview::feed(p, text) };
    }
}
/// # Safety
/// Paragraph is live and aligned, or null.
pub unsafe fn capacity(p: *const Paragraph, h: i32, line: i32, advance: i32) -> usize {
    unsafe { p.as_ref() }.map_or(usize::MAX, |p| preview::capacity(p, h, line, advance))
}
/// # Safety
/// Output contains two exclusive aligned i16 slots, or null.
pub unsafe fn line(
    row: i32,
    after: i32,
    h: i32,
    line: i32,
    advance: i32,
    gap: i32,
    y: *mut i16,
) -> u8 {
    if y.is_null() {
        return 0;
    }
    let Some((ys, mask)) = preview::line(row, after, h, line, advance, gap) else {
        return 0;
    };
    unsafe {
        ptr::copy_nonoverlapping(ys.as_ptr(), y, 2);
    }
    mask
}
