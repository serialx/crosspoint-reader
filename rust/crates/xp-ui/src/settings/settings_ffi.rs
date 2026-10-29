use super::{
    hits::Layout as Hits,
    state::{LayoutSelection, QuickResume},
    view::{self, Layout, Resources, Row},
};
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
    pub icon: u32,
    pub toggle: u8,
    pub checked: u8,
    pub enabled: u8,
    pub reserved: u8,
}
impl Default for DataRow {
    fn default() -> Self {
        Self {
            label: core::ptr::null(),
            subtitle: core::ptr::null(),
            value: core::ptr::null(),
            icon: 0,
            toggle: 0,
            checked: 0,
            enabled: 1,
            reserved: 0,
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub row: Option<unsafe extern "C" fn(*mut c_void, u16, *mut DataRow) -> u8>,
    pub width: Option<unsafe extern "C" fn(*mut c_void, *const c_char, i32, u8) -> i32>,
    pub height: Option<unsafe extern "C" fn(*mut c_void, *const c_char, i32, u8, u8, i16) -> i32>,
    pub line: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
    pub count: u16,
    pub reserved: u16,
}
impl Services {
    fn data(&self, index: u16) -> Result<DataRow, Error> {
        if index >= self.count {
            return Err(Error::Invalid);
        }
        let mut row = DataRow::default();
        let f = self.row.ok_or(Error::Invalid)?;
        if unsafe { f(self.context, index, &mut row) } == 0 || row.label.is_null() {
            return Err(Error::Invalid);
        }
        Ok(row)
    }
    fn text(&self, handle: u32) -> Result<*const c_char, Error> {
        let h = handle.checked_sub(1).ok_or(Error::Invalid)?;
        let row = self.data(u16::try_from(h / 4).map_err(|_| Error::Invalid)?)?;
        let p = match h % 4 {
            0 => row.label,
            1 => row.subtitle,
            2 => row.value,
            _ => return Err(Error::Invalid),
        };
        if p.is_null() {
            Err(Error::Invalid)
        } else {
            Ok(p)
        }
    }
}
fn metric(v: i32) -> Result<i32, Error> {
    if (0..=32767).contains(&v) {
        Ok(v)
    } else {
        Err(Error::Invalid)
    }
}
impl Resources for Services {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<Row, Error> {
        let d = self.data(index)?;
        let h = u32::from(index) * 4 + 1;
        Ok(Row {
            label: h,
            subtitle: if d.subtitle.is_null() { 0 } else { h + 1 },
            value: if d.value.is_null() { 0 } else { h + 2 },
            icon: if d.icon == 0 { 0 } else { h + 3 },
            toggle: d.toggle,
            checked: d.checked,
            enabled: d.enabled,
            reserved: 0,
        })
    }
    fn width(&mut self, h: u32, font: i32, bold: u8) -> Result<i32, Error> {
        let f = self.width.ok_or(Error::Invalid)?;
        metric(unsafe { f(self.context, self.text(h)?, font, bold) })
    }
    fn height(&mut self, h: u32, font: i32, bold: u8, lines: u8, width: i16) -> Result<i32, Error> {
        let f = self.height.ok_or(Error::Invalid)?;
        metric(unsafe { f(self.context, self.text(h)?, font, bold, lines, width) })
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        let f = self.line.ok_or(Error::Invalid)?;
        metric(unsafe { f(self.context, font) })
    }
}

/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn init(state: *mut ListState, selected: i32) {
    if let Some(s) = unsafe { state.as_mut() } {
        *s = ListState::new(selected);
    }
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn select(state: *mut ListState, selected: i32, count: u16) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.selected = selected.clamp(0, (i32::from(count) - 1).max(0));
        s.follow = 1;
        s.navigation_revision = s.navigation_revision.wrapping_add(1);
    }
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn interest(state: *const ListState, now: u32) -> Interest {
    unsafe { state.as_ref() }.map_or(Interest::default(), |s| s.interest(now))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn update(state: *mut ListState, input: *const Input, count: u16) -> Effect {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(s), Some(i)) => {
            if i.action == 2 {
                if i.value < 0 || i.value >= i32::from(count) {
                    return Effect::default();
                }
                s.selected = i.value;
                s.active = -1;
                Effect {
                    kind: 4,
                    value: i.value,
                    ..Effect::default()
                }
            } else {
                s.update(*i, i32::from(count), true)
            }
        }
        _ => Effect::default(),
    }
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn publish(state: *mut ListState, next: *const ListState, scroll: i32) {
    if let (Some(s), Some(n)) = (unsafe { state.as_mut() }, unsafe { next.as_ref() }) {
        s.publish(n, scroll);
    }
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn hit(layout: *const Hits, x: i16, y: i16, out: *mut Interaction) -> u8 {
    if let (Some(l), Some(o)) = (unsafe { layout.as_ref() }, unsafe { out.as_mut() }) {
        if let Some(h) = l.hit(x, y) {
            *o = h;
            return 1;
        }
    }
    0
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
/// The command buffer has its declared capacity and is writable scratch storage.
/// Callbacks and their borrowed text resources remain valid throughout the call.
pub unsafe fn view(
    state: *const ListState,
    layout: *const Layout,
    services: *const Services,
    buffer: *const Buffer,
    next: *mut ListState,
    out: *mut SceneView,
    hits: *mut Hits,
) -> u8 {
    let (Some(state), Some(l), Some(s), Some(b)) = (
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
        || s.row.is_none()
        || s.width.is_none()
        || s.height.is_none()
        || s.line.is_none()
    {
        return 0;
    }
    let mut next_state = *state;
    let mut services = *s;
    for pass in 0..9 {
        let commands =
            unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
        let mut routing = Hits::default();
        let Ok(mut arena) = Arena::with_hit_sink(commands, &mut routing) else {
            return 0;
        };
        let (n, disabled) = match view::view(&mut arena, next_state, *l, &mut services, s.count) {
            Ok(v) => v,
            Err(Error::Capacity) => return 2,
            Err(_) => return 0,
        };
        next_state = n;
        if n.follow != 0 && pass < 8 {
            continue;
        }
        let Ok(scene) = arena.finish(Refresh::Fast, 1) else {
            return 0;
        };
        let command_count = scene.commands.len() as u16;
        routing.disabled = disabled;
        unsafe {
            next.write(next_state);
            hits.write(routing);
            out.write(SceneView {
                commands: b.commands,
                text: core::ptr::null(),
                interactions: core::ptr::null(),
                generation: 1,
                command_count,
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
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn quick_resume(
    state: *mut QuickResume,
    timeout: u8,
    quick: u8,
    screen_changed: u8,
    timeout_changed: u8,
) -> u8 {
    unsafe { state.as_mut() }.map_or(timeout, |s| {
        s.sync(
            timeout,
            quick != 0,
            screen_changed != 0,
            timeout_changed != 0,
        )
    })
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn layout_locked(state: *const LayoutSelection, index: u8) -> u8 {
    unsafe { state.as_ref() }.map_or(0, |s| u8::from(s.locked(index)))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn layout_toggle(state: *mut LayoutSelection, index: u8) {
    if let Some(s) = unsafe { state.as_mut() } {
        s.toggle(index);
    }
}

/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn remap_init(state: *mut super::state::Remap) {
    if let Some(s) = unsafe { state.as_mut() } {
        *s = super::state::Remap::default();
    }
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn remap_update(
    state: *mut super::state::Remap,
    input: *const Input,
    front: i32,
) -> Effect {
    match (unsafe { state.as_mut() }, unsafe { input.as_ref() }) {
        (Some(s), Some(i)) => s.update(*i, front),
        _ => Effect::default(),
    }
}

/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn sync_update(state: u8, input: *const Input) -> Effect {
    let Some(i) = (unsafe { input.as_ref() }) else {
        return Effect::default();
    };
    let kind = if state == 0 {
        4
    } else if i.pressed & crate::model::BACK != 0
        || i.touch_flags & crate::model::TOUCH_RELEASED != 0 && i.touch_x >= 0 && i.touch_y >= 0
    {
        1
    } else {
        0
    };
    Effect {
        kind,
        ..Effect::default()
    }
}
pub fn sync_result(wifi: u8, success: u8) -> u8 {
    if wifi == 0 {
        2
    } else if success == 0 {
        3
    } else {
        1
    }
}

pub fn menu_edit(kind: u8, index: i32, value: u8) -> Effect {
    super::state::menu_edit(kind, index, value)
}
pub fn status_value(index: u8, value: u8) -> u8 {
    super::state::status_value(index, value)
}
pub fn section(category: u8, field: u8) -> u8 {
    super::state::section(category, field)
}
pub fn visible(category: u8, field: u8, text: u8, caps: u8, footnotes: u8) -> u8 {
    u8::from(super::state::visible(
        category, field, text, caps, footnotes,
    ))
}
pub fn extra(section: u8, caps: u8, index: u8) -> u8 {
    super::state::extra(section, caps, index)
}

/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_show(s: *mut super::ble::State, v: u8) {
    if let Some(s) = unsafe { s.as_mut() } {
        s.show(v);
    }
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_poll(s: *mut super::ble::State, n: u32) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| u8::from(s.poll(n)))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_back(s: *mut super::ble::State) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| s.back())
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_begin(s: *mut super::ble::State, i: i32, c: u8, n: u32) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| u8::from(s.begin(i, c != 0, n)))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_step(s: *mut super::ble::State, k: u8, c: u32, a: u32, n: u32) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| s.step(k, c, a, n))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_clear(s: *mut super::ble::State, i: i32) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| s.clear(i))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_activate(s: *const super::ble::State, c: i16, e: u8, a: u8) -> Effect {
    unsafe { s.as_ref() }.map_or(Effect::default(), |s| s.activate(c, e != 0, a != 0))
}
/// # Safety
/// Non-null pointers must be aligned and live for the call; mutable descriptors
/// must be exclusively borrowed and disjoint from the other arguments.
pub unsafe fn ble_row(
    s: *const super::ble::State,
    f: u8,
    n: u8,
    b: u8,
    i: u16,
    o: *mut super::ble::Row,
) -> u8 {
    if let (Some(s), Some(o)) = (unsafe { s.as_ref() }, unsafe { o.as_mut() }) {
        if let Some(row) = super::ble::row(s, f, n, b, i) {
            *o = row;
            return 1;
        }
    }
    0
}
