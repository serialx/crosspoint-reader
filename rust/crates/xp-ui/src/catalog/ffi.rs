use super::{
    hits::Hits,
    state::State,
    view::{self, Layout},
};
use crate::settings::view::{Resources, Row};
use crate::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest, ListState},
    scene::{Arena, Error, Interaction, Refresh},
};
use core::ffi::{c_char, c_void};
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DataRow {
    pub label: *const c_char,
    pub subtitle: *const c_char,
    pub value: *const c_char,
    pub heading: *const c_char,
}
impl Default for DataRow {
    fn default() -> Self {
        Self {
            label: core::ptr::null(),
            subtitle: core::ptr::null(),
            value: core::ptr::null(),
            heading: core::ptr::null(),
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub row: Option<unsafe extern "C" fn(*mut c_void, u16, *mut DataRow) -> u8>,
    pub text: Option<unsafe extern "C" fn(*mut c_void, u32) -> *const c_char>,
    pub width: Option<unsafe extern "C" fn(*mut c_void, *const c_char, i32, u8) -> i32>,
    pub height: Option<unsafe extern "C" fn(*mut c_void, *const c_char, i32, u8, u8, i16) -> i32>,
    pub line: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
    pub count: u16,
    pub reserved: u16,
}
fn metric(v: i32) -> Result<i32, Error> {
    if (0..=32767).contains(&v) {
        Ok(v)
    } else {
        Err(Error::Invalid)
    }
}
impl Services {
    pub fn data(&self, index: u16) -> Result<DataRow, Error> {
        if index >= self.count {
            return Err(Error::Invalid);
        }
        let mut row = DataRow::default();
        if unsafe { self.row.ok_or(Error::Invalid)?(self.context, index, &mut row) } == 0
            || row.label.is_null()
        {
            return Err(Error::Invalid);
        }
        Ok(row)
    }
    fn text(&self, handle: u32) -> Result<*const c_char, Error> {
        let p = if handle >= 0x8000_0000 {
            unsafe { self.text.ok_or(Error::Invalid)?(self.context, handle) }
        } else {
            let h = handle.checked_sub(1).ok_or(Error::Invalid)?;
            let d = self.data(u16::try_from(h / 4).map_err(|_| Error::Invalid)?)?;
            match h % 4 {
                0 => d.label,
                1 => d.subtitle,
                2 => d.value,
                _ => d.heading,
            }
        };
        if p.is_null() {
            Err(Error::Invalid)
        } else {
            Ok(p)
        }
    }
}
impl Resources for Services {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, i: u16) -> Result<Row, Error> {
        let d = self.data(i)?;
        let h = u32::from(i) * 4 + 1;
        Ok(Row {
            label: h,
            subtitle: if d.subtitle.is_null() { 0 } else { h + 1 },
            value: if d.value.is_null() { 0 } else { h + 2 },
            enabled: 1,
            ..Row::default()
        })
    }
    fn width(&mut self, h: u32, f: i32, b: u8) -> Result<i32, Error> {
        metric(unsafe { self.width.ok_or(Error::Invalid)?(self.context, self.text(h)?, f, b) })
    }
    fn height(&mut self, h: u32, f: i32, b: u8, l: u8, w: i16) -> Result<i32, Error> {
        metric(unsafe {
            self.height.ok_or(Error::Invalid)?(self.context, self.text(h)?, f, b, l, w)
        })
    }
    fn line(&mut self, f: i32) -> Result<i32, Error> {
        metric(unsafe { self.line.ok_or(Error::Invalid)?(self.context, f) })
    }
}
/// # Safety
/// Pointers follow the catalog ABI's aligned, disjoint borrowed-descriptor contract.
pub unsafe fn init(s: *mut State, k: u8, i: i32, p: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        *s = State::new(k, i, p);
    }
}
/// # Safety
/// Pointers follow the catalog ABI borrowing contract.
pub unsafe fn interest(s: *const State, t: u32) -> Interest {
    unsafe { s.as_ref() }.map_or(Interest::default(), |s| s.interest(t))
}
/// # Safety
/// Pointers follow the catalog ABI borrowing contract.
pub unsafe fn update(s: *mut State, i: *const Input, n: u16, q: u8, p: u8) -> Effect {
    match (unsafe { s.as_mut() }, unsafe { i.as_ref() }) {
        (Some(s), Some(i)) => s.update(*i, n, q != 0, p),
        _ => Effect::default(),
    }
}
/// # Safety
/// Pointers follow the catalog ABI borrowing contract.
pub unsafe fn publish(s: *mut State, n: *const ListState, v: i32) {
    if let (Some(s), Some(n)) = (unsafe { s.as_mut() }, unsafe { n.as_ref() }) {
        s.list.publish(n, v);
    }
}
/// # Safety
/// Pointers follow the catalog ABI borrowing contract.
pub unsafe fn hit(h: *const Hits, x: i16, y: i16, o: *mut Interaction) -> u8 {
    if let (Some(h), Some(o)) = (unsafe { h.as_ref() }, unsafe { o.as_mut() }) {
        if let Some(v) = h.hit(x, y) {
            *o = v;
            return 1;
        }
    }
    0
}
/// # Safety
/// Descriptors and strings are aligned and valid through the call. Mutable output
/// and scratch descriptors are exclusive and disjoint; callbacks cannot reenter.
#[allow(clippy::too_many_arguments)]
#[inline(never)]
pub unsafe fn view(
    s: *const State,
    l: *const Layout,
    r: *const Services,
    b: *const Buffer,
    n: *mut ListState,
    o: *mut SceneView,
    h: *mut Hits,
) -> u8 {
    let (Some(s), Some(l), Some(r), Some(b)) = (
        unsafe { s.as_ref() },
        unsafe { l.as_ref() },
        unsafe { r.as_ref() },
        unsafe { b.as_ref() },
    ) else {
        return 0;
    };
    if n.is_null()
        || o.is_null()
        || h.is_null()
        || b.commands.is_null()
        || b.command_capacity > 256
        || b.hit_capacity != 0
        || r.row.is_none()
        || r.text.is_none()
        || r.width.is_none()
        || r.height.is_none()
        || r.line.is_none()
    {
        return 0;
    }
    let mut state = *s;
    for pass in 0..9 {
        let commands =
            unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
        let mut routing = Hits::default();
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut routing) else {
            return 0;
        };
        let next = match view::view(&mut arena, state, *l, *r) {
            Ok(n) => n,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        state.list = next;
        if next.follow != 0 && pass < 8 {
            continue;
        }
        let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
            return 0;
        };
        let count = scene.commands.len() as u16;
        unsafe {
            n.write(next);
            h.write(routing);
            o.write(SceneView {
                commands: b.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count: count,
                text_len: 0,
                interaction_count: 0,
                refresh: 1,
                reserved: 0,
            });
        }
        return 1;
    }
    0
}
/// # Safety
/// Pointers follow the private catalog ABI borrowing contract.
pub unsafe fn select(s: *mut State, i: i32, r: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.select(i, r != 0);
    }
}
/// # Safety
/// Pointers follow the private catalog ABI borrowing contract.
pub unsafe fn mode(s: *mut State, m: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.set_mode(m);
    }
}
/// # Safety
/// Pointers follow the private catalog ABI borrowing contract.
pub unsafe fn awake(s: *const State) -> u8 {
    unsafe { s.as_ref() }.map_or(0, |s| u8::from(s.awake()))
}
/// # Safety
/// Pointers follow the private catalog ABI borrowing contract.
pub unsafe fn progress(s: *mut State, d: u32, t: u32, n: u32) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| u8::from(s.progress(d, t, n)))
}
