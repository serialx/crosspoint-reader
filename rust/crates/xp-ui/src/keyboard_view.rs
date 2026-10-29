//! Keyboard placement and text-field geometry over the shared UI scene.
use crate::{
    ffi::SceneView,
    keyboard::{self, Layouts, State},
    scene::{Arena, Command, Error, Interaction, Kind, Rect, Refresh},
};
use core::ffi::{c_void, CStr};
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Layout {
    pub screen: Rect,
    pub content: Rect,
    pub font: i32,
    pub small_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub spacing: i16,
    pub vertical_offset: i16,
    pub key_spacing: i16,
    pub key_height: i16,
    pub hints_height: i16,
    pub side_width: i16,
    pub text_percent: i16,
    pub field_normal: i16,
    pub field_cursor: i16,
    pub field_end: i16,
    pub centered: u8,
    pub touch: u8,
    pub x3: u8,
    pub language: u8,
    pub strings: [u16; 18],
}
#[repr(C)]
pub struct Metrics {
    pub context: *mut c_void,
    pub measure: Option<unsafe extern "C" fn(*mut c_void, i32, *const u8, u32, u8) -> i32>,
    pub line_height: Option<unsafe extern "C" fn(*mut c_void, i32) -> i32>,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct TextSpan {
    pub start: u32,
    pub length: u32,
    pub raw: u8,
    pub reserved: [u8; 3],
}
#[repr(C)]
pub struct Buffer {
    pub commands: *mut Command,
    pub hits: *mut Interaction,
    pub text: *mut u8,
    pub command_capacity: u16,
    pub hit_capacity: u16,
    pub text_capacity: u16,
    pub spans: *mut TextSpan,
    pub span_capacity: u16,
}
impl Metrics {
    fn measure(&self, font: i32, text: &[u8], mode: u8) -> i32 {
        self.measure.map_or(0, |f| unsafe {
            f(self.context, font, text.as_ptr(), text.len() as u32, mode)
        })
    }
    fn line(&self, font: i32) -> i16 {
        self.line_height
            .map_or(0, |f| unsafe { f(self.context, font) }) as i16
    }
}
fn rect(x: i16, y: i16, w: i16, h: i16) -> Rect {
    Rect { x, y, w, h }
}
fn fill(a: &mut Arena<'_>, r: Rect, paint: u8, radius: u8) -> Result<(), Error> {
    a.push(Command {
        rect: r,
        paint,
        radius,
        ..Command::default()
    })
}
fn text(
    a: &mut Arena<'_>,
    r: Rect,
    font: i32,
    paint: u8,
    align: u8,
    box_text: bool,
    bytes: &[u8],
) -> Result<(), Error> {
    a.text(
        Command {
            rect: r,
            font,
            paint,
            align,
            max_lines: u8::from(box_text),
            ..Command::default()
        },
        bytes,
    )
}
fn resource(a: &mut Arena<'_>, r: Rect, id: u32, paint: u8) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Bitmap as u8,
        rect: r,
        resource: id,
        generation: 1,
        paint,
        ..Command::default()
    })
}
fn translated(l: &Layout, index: usize) -> &'static [u8] {
    unsafe { CStr::from_ptr(xp_core::i18n::get(l.language, l.strings[index])).to_bytes() }
}
fn centered(a: &mut Arena<'_>, l: &Layout, m: &Metrics, y: i16, bytes: &[u8]) -> Result<(), Error> {
    let x = (i32::from(l.screen.w) - m.measure(l.small_font, bytes, 0)) / 2;
    text(a, rect(x as i16, y, 0, 0), l.small_font, 0, 0, false, bytes)
}
fn keyboard_rect(s: &State, l: &Layout, layouts: &Layouts) -> (Rect, Rect) {
    let rows = layouts.rows(s);
    let gap = l.key_spacing.max(6);
    let key_h = if l.touch != 0 { l.key_height } else { 42 };
    let height = rows * key_h + (rows - 1).max(0) * gap + 8;
    let hint_gap = if l.touch != 0 {
        l.spacing - l.vertical_offset
    } else {
        4
    };
    let y = l.screen.h - l.hints_height - height - hint_gap;
    let h = if l.touch != 0 { l.screen.h - y } else { height };
    (
        rect(l.content.x, y, l.content.w, h),
        rect(l.content.x, y + (h - height) / 2, l.content.w, height),
    )
}
fn hit_rect(mut r: Rect, screen: Rect) -> Rect {
    if r.w < 28 {
        r.x -= (28 - r.w) / 2;
        r.w = 28;
    }
    if r.h < 28 {
        r.y -= (28 - r.h) / 2;
        r.h = 28;
    }
    r.x = r.x.max(screen.x);
    r.y = r.y.max(screen.y);
    if r.x + r.w > screen.x + screen.w {
        r.x = screen.x + screen.w - r.w;
    }
    if r.y + r.h > screen.y + screen.h {
        r.y = screen.y + screen.h - r.h;
    }
    if r.x - screen.x < 12 {
        r.w += r.x - screen.x;
        r.x = screen.x;
    }
    if r.y - screen.y < 12 {
        r.h += r.y - screen.y;
        r.y = screen.y;
    }
    if screen.x + screen.w - r.x - r.w < 12 {
        r.w = screen.x + screen.w - r.x;
    }
    if screen.y + screen.h - r.y - r.h < 12 {
        r.h = screen.y + screen.h - r.y;
    }
    r
}
pub fn keys(
    a: &mut Arena<'_>,
    s: &State,
    l: &Layout,
    _m: &Metrics,
    layouts: &Layouts,
) -> Result<(), Error> {
    let (panel, keys) = keyboard_rect(s, l, layouts);
    if l.touch != 0 {
        fill(a, panel, 2, 0)?;
    }
    let bottom = if l.touch != 0 {
        l.screen.h
    } else {
        l.screen.h - l.hints_height
    };
    if l.touch != 0 {
        fill(a, keys, 2, 0)?;
    }
    walk_keys(
        s,
        l,
        layouts,
        keys,
        bottom,
        core::ptr::null_mut(),
        |_, _, _, _| Ok(true),
    )?;
    for row in 0..layouts.rows(s) as u32 {
        resource(a, keys, 0x100 + row, 0)?;
    }
    Ok(())
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PlacedKey {
    pub visual: Rect,
    pub hit: Rect,
    pub value: i16,
    pub selected: u8,
    pub reserved: u8,
}
#[inline(never)]
fn walk_keys(
    s: &State,
    l: &Layout,
    layouts: &Layouts,
    keys: Rect,
    bottom: i16,
    context: *mut c_void,
    visit: fn(*mut c_void, u8, u8, PlacedKey) -> Result<bool, Error>,
) -> Result<(), Error> {
    let area = rect(keys.x + 4, keys.y + 4, keys.w - 8, keys.h - 8);
    if area.w < 10 || area.h < 10 {
        return Ok(());
    }
    let rows = layouts.rows(s);
    if rows == 0 {
        return Ok(());
    }
    let gap = l.key_spacing.max(6);
    let row_h = (area.h - gap * (rows - 1)) / rows;
    let uniform = s.url_panel == 0;
    let mut uniform_unit = 0;
    for row in 0..rows as u8 {
        let r = layouts.row(s, row);
        let mut units = i16::from(r.inset) * 2;
        let mut character = false;
        for col in 0..r.count {
            let k = layouts.key(s, row, col).ok_or(Error::Invalid)?;
            units += i16::from(k.units);
            character |= k.kind == 0;
        }
        if uniform && character && units > 0 && r.independent == 0 {
            let candidate = (area.w - gap * (i16::from(r.count) - 1)) / units;
            if uniform_unit == 0 || candidate < uniform_unit {
                uniform_unit = candidate;
            }
        }
    }
    let mut top = Rect::default();
    let mut index = 0;
    let selected = if s.cursor_mode != 0 || l.touch != 0 {
        -1
    } else {
        s.logical_index(layouts)
    };
    for row in 0..rows as u8 {
        let r = layouts.row(s, row);
        let mut units = i16::from(r.inset) * 2;
        let mut character = false;
        for col in 0..r.count {
            let k = layouts.key(s, row, col).ok_or(Error::Invalid)?;
            units += i16::from(k.units);
            character |= k.kind == 0;
        }
        if units == 0 {
            continue;
        }
        let gap_width = gap * (i16::from(r.count) - 1);
        let own = (area.w - gap_width) / units;
        let uniform_row = uniform
            && character
            && if r.independent != 0 {
                own > 0
            } else {
                uniform_unit > 0
            };
        let unit = if uniform_row && r.independent == 0 {
            uniform_unit
        } else {
            own
        };
        let used = unit * units + gap_width;
        let y = area.y + i16::from(row) * (row_h + gap);
        let mut x = area.x + i16::from(r.inset) * unit;
        let right = area.x + area.w - i16::from(r.inset) * unit;
        if uniform_row {
            x += (area.w - used) / 2;
        }
        if row == 0 && uniform_row {
            top = rect(x, y, used - 2 * i16::from(r.inset) * unit, row_h);
        }
        for col in 0..r.count {
            let k = layouts.key(s, row, col).ok_or(Error::Invalid)?;
            let w = if !uniform_row && col + 1 == r.count {
                right - x
            } else {
                unit * i16::from(k.units)
            };
            let mut visual = rect(x, y, w, row_h);
            if top.w > 0 && r.inset == 0 {
                if col == 0 && (k.kind == 1 || k.kind == 2) {
                    visual.w = visual.x + visual.w - top.x;
                    visual.x = top.x;
                }
                if col + 1 == r.count && (k.kind == 4 || k.kind == 5) {
                    visual.w = top.x + top.w - visual.x;
                }
            }
            let overflow = if i16::from(row) == rows - 1 {
                (bottom - keys.y - keys.h).max(0)
            } else {
                0
            };
            let hit = hit_rect(
                rect(visual.x, visual.y, visual.w, visual.h + overflow),
                l.screen,
            );
            if paint_key(s, layouts, row, col).is_none() {
                return Err(Error::Invalid);
            }
            if !visit(
                context,
                row,
                col,
                PlacedKey {
                    visual,
                    hit,
                    value: k.value,
                    selected: u8::from(index == selected || s.active_key == k.value),
                    reserved: 0,
                },
            )? {
                return Ok(());
            }
            index += 1;
            x += w + gap;
        }
    }
    Ok(())
}

/// Expanded commands are used only by the frozen SDK placement oracle.
pub fn key_commands(
    a: &mut Arena<'_>,
    s: &State,
    l: &Layout,
    layouts: &Layouts,
    keys: Rect,
    bottom: i16,
) -> Result<(), Error> {
    if l.touch != 0 {
        fill(a, keys, 2, 0)?;
    }
    walk_keys(
        s,
        l,
        layouts,
        keys,
        bottom,
        (a as *mut Arena<'_>).cast(),
        |p, row, col, k| {
            let a = unsafe { &mut *p.cast::<Arena<'_>>() };
            a.hit(Interaction {
                rect: k.hit,
                action: 1,
                value: i32::from(k.value),
                generation: 1,
                inputs: 1,
                flags: 0,
            })?;
            resource(
                a,
                k.visual,
                0x100 + u32::from(row) * 16 + u32::from(col),
                k.selected,
            )?;
            Ok(true)
        },
    )
}
fn bottom(l: &Layout) -> i16 {
    if l.touch != 0 {
        l.screen.h
    } else {
        l.screen.h - l.hints_height
    }
}
/// # Safety
/// Input descriptors are readable, callbacks valid, and output has capacity
/// exclusive slots. Failure returns zero; scratch contents are unspecified.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_row(
    s: *const State,
    layouts: *const Layouts,
    l: *const Layout,
    row: u8,
    out: *mut PlacedKey,
    capacity: u8,
) -> u8 {
    let (Some(s), Some(layouts), Some(l)) =
        (unsafe { s.as_ref() }, unsafe { layouts.as_ref() }, unsafe {
            l.as_ref()
        })
    else {
        return 0;
    };
    if out.is_null() || capacity == 0 || capacity > 16 || row >= layouts.rows(s) as u8 {
        return 0;
    }
    struct Output {
        row: u8,
        count: u8,
        capacity: u8,
        keys: *mut PlacedKey,
    }
    let mut output = Output {
        row,
        count: 0,
        capacity,
        keys: out,
    };
    let result = walk_keys(
        s,
        l,
        layouts,
        keyboard_rect(s, l, layouts).1,
        bottom(l),
        (&mut output as *mut Output).cast(),
        |p, r, _, k| {
            let out = unsafe { &mut *p.cast::<Output>() };
            if r > out.row {
                return Ok(false);
            }
            if r == out.row {
                if out.count == out.capacity {
                    return Err(Error::Capacity);
                }
                unsafe {
                    out.keys.add(usize::from(out.count)).write(k);
                }
                out.count += 1;
            }
            Ok(true)
        },
    );
    if result.is_ok() {
        output.count
    } else {
        0
    }
}
/// # Safety
/// Inputs refer to the immutable published snapshot; output is writable and
/// disjoint. Failure leaves output untouched. Callbacks live through the call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_hit(
    s: *const State,
    layouts: *const Layouts,
    l: *const Layout,
    x: i16,
    y: i16,
    out: *mut Interaction,
) -> u8 {
    let (Some(s), Some(layouts), Some(l), Some(out)) = (
        unsafe { s.as_ref() },
        unsafe { layouts.as_ref() },
        unsafe { l.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    struct Hit {
        x: i16,
        y: i16,
        found: Option<Interaction>,
    }
    let mut hit = Hit { x, y, found: None };
    let result = walk_keys(
        s,
        l,
        layouts,
        keyboard_rect(s, l, layouts).1,
        bottom(l),
        (&mut hit as *mut Hit).cast(),
        |p, _, _, k| {
            let hit = unsafe { &mut *p.cast::<Hit>() };
            if k.hit.contains(hit.x, hit.y) {
                hit.found = Some(Interaction {
                    rect: k.hit,
                    action: 1,
                    value: i32::from(k.value),
                    generation: 1,
                    inputs: 1,
                    flags: 0,
                });
            }
            Ok(true)
        },
    );
    if result.is_ok() {
        if let Some(found) = hit.found {
            *out = found;
            return 1;
        }
    }
    0
}
fn line_end(text: &[u8], start: usize, width: i16, font: i32, m: &Metrics) -> usize {
    if m.measure(font, &text[start..], 1) <= i32::from(width) {
        return text.len();
    }
    let (mut lo, mut hi, mut best) = (start + 1, text.len().saturating_sub(1), start + 1);
    while lo <= hi {
        let mid = lo + (hi - lo) / 2;
        if m.measure(font, &text[start..mid], 1) <= i32::from(width) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    let first = keyboard::following(text, start);
    while best > start && text.get(best).is_some_and(|b| b & 0xc0 == 0x80) {
        best -= 1;
    }
    while best > first && m.measure(font, &text[start..best], 1) > i32::from(width) {
        best = keyboard::previous(text, best);
    }
    best.max(first).min(text.len())
}
fn field(s: &State, l: &Layout, m: &Metrics) -> (i16, i16, i16, i16) {
    let y = l.top_padding + l.header_height + 5 * l.spacing + l.vertical_offset;
    let available = l.content.w - if l.x3 != 0 { 2 * l.side_width } else { 0 };
    let margin =
        (l.content.w - (i32::from(available) * i32::from(l.text_percent) / 100) as i16) / 2;
    let reserve = if s.input_type == 1 {
        m.measure(l.font, b"[abc]", 0)
            .max(m.measure(l.font, b"[***]", 0)) as i16
            + 4
    } else {
        0
    };
    (y, margin, reserve, l.content.w - 2 * margin - reserve)
}
pub fn cursor_at(
    s: &State,
    display: &[u8],
    l: &Layout,
    m: &Metrics,
    layouts: &Layouts,
    x: i16,
    y: i16,
) -> Option<u32> {
    if y >= keyboard_rect(s, l, layouts).0.y {
        return None;
    }
    let (start_y, margin, reserve, width) = field(s, l, m);
    let left = l.content.x + margin;
    let lh = m.line(l.font);
    if lh <= 0 {
        return None;
    }
    let (mut start, mut line_y) = (0, start_y);
    loop {
        let end = line_end(display, start, width, l.font, m);
        let advance = m.measure(l.font, &display[start..end], 1) as i16;
        let line_x = if l.centered != 0 {
            left + (width - advance) / 2
        } else {
            left
        };
        let rtl = m.measure(l.font, &display[start..end], 2) != 0;
        if y >= line_y - l.spacing && y < line_y + lh + l.spacing {
            if x <= line_x {
                return Some(if rtl { end } else { start } as u32);
            }
            if x >= line_x + advance {
                return Some(if rtl { start } else { end } as u32);
            }
            let mut previous = 0;
            let mut i = start;
            while i < end {
                let next = keyboard::following(display, i);
                let next_width = m.measure(l.font, &display[start..next], 1) as i16;
                let half = (next_width - previous) / 2;
                let midpoint = if rtl {
                    line_x + advance - previous - half
                } else {
                    line_x + previous + half
                };
                if (rtl && x >= midpoint) || (!rtl && x < midpoint) {
                    return Some(i as u32);
                }
                previous = next_width;
                i = next;
            }
            return Some(end as u32);
        }
        if end == display.len() {
            let bottom = line_y + lh + l.spacing + 8;
            if y >= start_y - l.spacing && y < bottom && x >= left && x < left + width + reserve {
                return Some(if (x < line_x + advance) == rtl {
                    end
                } else {
                    start
                } as u32);
            }
            return None;
        }
        if line_y > l.screen.h {
            return None;
        }
        start = end;
        line_y += lh;
    }
}
struct FieldText<'a> {
    spans: &'a mut [TextSpan],
    used: usize,
    layout: &'a Layout,
    line_height: i16,
}
impl FieldText<'_> {
    fn push(
        &mut self,
        a: &mut Arena<'_>,
        y: i32,
        x: i16,
        paint: u8,
        raw: bool,
        range: core::ops::Range<usize>,
    ) -> Result<(), Error> {
        let l = self.layout;
        let (start, end) = (range.start, range.end);
        if y >= i32::from(l.screen.h) || y + i32::from(self.line_height) <= 0 || start == end {
            return Ok(());
        }
        let slot = self.spans.get_mut(self.used).ok_or(Error::Capacity)?;
        *slot = TextSpan {
            start: start as u32,
            length: (end - start) as u32,
            raw: u8::from(raw),
            reserved: [0; 3],
        };
        a.push(Command {
            kind: Kind::TextHandle as u8,
            resource: 0x200 + self.used as u32,
            generation: 1,
            rect: rect(x, y as i16, 0, 0),
            font: l.font,
            paint,
            ..Command::default()
        })?;
        self.used += 1;
        Ok(())
    }
}
pub fn view(
    a: &mut Arena<'_>,
    s: &State,
    texts: (&[u8], &[u8]),
    l: &Layout,
    m: &Metrics,
    layouts: &Layouts,
    spans: &mut [TextSpan],
) -> Result<(), Error> {
    let (raw, display) = texts;
    if l.screen.w <= 0
        || l.screen.h <= 0
        || l.screen.w > 8192
        || l.screen.h > 8192
        || l.content.w <= 0
        || l.content.w > l.screen.w
        || l.text_percent < 1
        || l.text_percent > 100
        || l.spacing < 0
        || l.spacing > 128
        || l.key_spacing > 128
        || l.key_height > 256
        || s.cursor > s.length
        || s.length as usize != raw.len()
        || raw.len() != display.len()
    {
        return Err(Error::Invalid);
    }
    let lh = m.line(l.font);
    let small_lh = m.line(l.small_font);
    if lh <= 0 || lh > 256 || small_lh <= 0 || small_lh > 256 {
        return Err(Error::Invalid);
    }
    let mut field_text = FieldText {
        spans,
        used: 0,
        layout: l,
        line_height: lh,
    };
    a.push(Command {
        kind: Kind::Clear as u8,
        rect: l.screen,
        paint: 1,
        ..Command::default()
    })?;
    resource(a, rect(0, l.top_padding, l.screen.w, l.header_height), 1, 0)?;
    let (start_y, margin, _reserve, width) = field(s, l, m);
    let left = l.content.x + margin;
    let cursor = s.cursor as usize;
    let char_end = keyboard::following(raw, cursor);
    let char_end = char_end.min(cursor.saturating_add(7));
    let actual = &raw[cursor..char_end];
    let shown = &display[cursor..char_end];
    let char_width = 6.max(m.measure(l.font, actual, 0)) as i16;
    let (mut start, mut height, mut cursor_x, mut cursor_y, mut drawn) =
        (0, 0i32, left, i32::from(start_y), false);
    loop {
        let end = line_end(display, start, width, l.font, m);
        let line = &display[start..end];
        let advance = m.measure(l.font, line, 1) as i16;
        let rtl = m.measure(l.font, line, 2) != 0;
        let line_x = if l.centered != 0 {
            left + (width - advance) / 2
        } else {
            left
        };
        let last = end == display.len();
        let mut cursor_line = false;
        if !drawn && cursor >= start && if last { cursor <= end } else { cursor < end } {
            let before = m.measure(l.font, &display[start..cursor], 1) as i16;
            let through = if actual.is_empty() {
                before
            } else {
                m.measure(l.font, &display[start..char_end], 1) as i16
            };
            let kern = if actual.is_empty() {
                0
            } else {
                through - before - m.measure(l.font, shown, 1) as i16
            };
            cursor_x = if rtl {
                line_x + advance
                    - if s.cursor_mode != 0 && !actual.is_empty() {
                        through
                    } else {
                        before
                    }
            } else {
                line_x + before + kern
            };
            cursor_y = i32::from(start_y) + height;
            drawn = true;
            cursor_line = true;
        }
        if cursor_line
            && s.cursor_mode != 0
            && s.input_type == 1
            && s.password_visible == 0
            && s.toggle_position == 0
        {
            field_text.push(
                a,
                i32::from(start_y) + height,
                line_x,
                0,
                false,
                start..cursor,
            )?;
            if char_end < end {
                field_text.push(
                    a,
                    i32::from(start_y) + height,
                    cursor_x + char_width,
                    0,
                    false,
                    char_end..end,
                )?;
            }
        } else {
            field_text.push(a, i32::from(start_y) + height, line_x, 0, false, start..end)?;
        }
        if last {
            break;
        }
        height += i32::from(lh);
        start = end;
    }
    let underline = i32::from(start_y) + height + i32::from(lh) + i32::from(l.spacing);
    if underline < i32::from(l.screen.h) {
        fill(
            a,
            rect(
                left,
                underline as i16,
                l.content.w - 2 * margin + l.field_end + 1,
                if s.cursor_mode != 0 {
                    l.field_cursor
                } else {
                    l.field_normal
                },
            ),
            0,
            0,
        )?;
    }
    if cursor_y < i32::from(l.screen.h) {
        if s.cursor_mode != 0 && s.toggle_position == 0 {
            fill(
                a,
                rect(cursor_x - 1, cursor_y as i16, char_width + 2, lh),
                0,
                0,
            )?;
            if !actual.is_empty() {
                field_text.push(a, cursor_y, cursor_x, 1, true, cursor..char_end)?;
            }
        } else {
            fill(a, rect(cursor_x, cursor_y as i16, 2, lh), 0, 0)?;
            for y in [cursor_y as i16, cursor_y as i16 + lh - 1] {
                fill(a, rect(cursor_x - 3, y, 3, 2), 0, 0)?;
                fill(a, rect(cursor_x + 1, y, 3, 2), 0, 0)?;
            }
        }
    }
    if s.input_type == 1 && i32::from(start_y) + height < i32::from(l.screen.h) {
        let label = if s.password_visible != 0 {
            b"[***]".as_slice()
        } else {
            b"[abc]"
        };
        let width = m.measure(l.font, label, 0) as i16;
        let x = l.content.x + l.content.w - margin - width;
        let y = (i32::from(start_y) + height) as i16;
        let selected = s.cursor_mode != 0 && s.toggle_position != 0;
        if selected {
            fill(a, rect(x - 2, y, width + 5, lh + 3), 0, 0)?;
        }
        text(
            a,
            rect(x, y, 0, 0),
            l.font,
            u8::from(selected),
            0,
            false,
            label,
        )?;
    }
    if s.hint_visible != 0
        && s.cursor_mode != 0
        && !raw.is_empty()
        && underline + 4 < i32::from(l.screen.h)
    {
        let y = (underline + 4) as i16;
        if s.input_type == 1 && s.toggle_position != 0 {
            centered(
                a,
                l,
                m,
                y,
                translated(l, if s.password_visible != 0 { 4 } else { 5 }),
            )?;
            centered(a, l, m, y + small_lh, translated(l, 6))?;
        } else {
            centered(a, l, m, y, translated(l, 7))?;
            if s.input_type == 1 {
                centered(
                    a,
                    l,
                    m,
                    y + small_lh,
                    translated(l, if s.password_visible != 0 { 8 } else { 9 }),
                )?;
            }
        }
    }
    let panel = keyboard_rect(s, l, layouts).0;
    let count = if s.cursor_mode != 0 {
        1
    } else if s.url_panel != 0 {
        3
    } else if s.symbols != 0 {
        2
    } else {
        3 + i16::from(s.input_type == 2)
    };
    let tips_height = (count + 1) * small_lh;
    let tips_top = underline
        + 4
        + if s.cursor_mode != 0 {
            (if s.input_type == 1 { 2 } else { 1 }) * i32::from(small_lh)
        } else {
            0
        };
    if i32::from(panel.y) - tips_top >= i32::from(tips_height) {
        let mut y =
            (tips_top + (i32::from(panel.y) - tips_top - i32::from(tips_height)) / 2) as i16;
        centered(a, l, m, y, translated(l, 3))?;
        y += small_lh;
        if s.cursor_mode == 0 {
            centered(a, l, m, y, translated(l, 10))?;
            y += small_lh;
        }
        if s.cursor_mode != 0 {
            centered(a, l, m, y, translated(l, 11))?;
        } else if s.url_panel != 0 {
            centered(a, l, m, y, translated(l, 12))?;
            y += small_lh;
            if !raw.is_empty() {
                centered(a, l, m, y, translated(l, 13))?;
            }
        } else if s.symbols != 0 {
            if !raw.is_empty() {
                centered(a, l, m, y, translated(l, 13))?;
            }
        } else {
            centered(
                a,
                l,
                m,
                y,
                translated(
                    l,
                    if s.input_type == 2 {
                        14
                    } else if s.shifted != 0 {
                        15
                    } else {
                        16
                    },
                ),
            )?;
            y += small_lh;
            if s.input_type == 2 {
                centered(a, l, m, y, translated(l, 17))?;
                y += small_lh;
            }
            if !raw.is_empty() {
                centered(a, l, m, y, translated(l, 13))?;
            }
        }
    }
    keys(a, s, l, m, layouts)?;
    resource(a, l.screen, 2, 0)?;
    resource(a, l.screen, 3, 0)?;
    Ok(())
}
/// # Safety
/// Readable state/text; exclusive output of capacity bytes, disjoint from text.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_mask(
    s: *const State,
    text: *const u8,
    display: *mut u8,
    capacity: u32,
) -> u8 {
    let Some(s) = (unsafe { s.as_ref() }) else {
        return 0;
    };
    if text.is_null()
        || display.is_null()
        || capacity as usize > isize::MAX as usize
        || s.length >= capacity
    {
        return 0;
    }
    u8::from(s.mask(
        unsafe { core::slice::from_raw_parts(text, s.length as usize) },
        unsafe { core::slice::from_raw_parts_mut(display, capacity as usize) },
    ))
}
/// # Safety
/// Descriptors and text slices are readable; output storage is exclusive and
/// disjoint. All callbacks and borrowed resources live through synchronous paint.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_view(
    s: *const State,
    text: *const u8,
    display: *const u8,
    layouts: *const Layouts,
    l: *const Layout,
    m: *const Metrics,
    b: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(s), Some(layouts), Some(l), Some(m), Some(b), Some(out)) = (
        unsafe { s.as_ref() },
        unsafe { layouts.as_ref() },
        unsafe { l.as_ref() },
        unsafe { m.as_ref() },
        unsafe { b.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if text.is_null()
        || display.is_null()
        || b.commands.is_null()
        || (b.hits.is_null() && b.hit_capacity != 0)
        || b.text.is_null()
        || b.spans.is_null()
        || b.span_capacity > 256
        || s.length as usize > isize::MAX as usize
        || b.command_capacity > 256
        || b.hit_capacity > 64
        || b.text_capacity > 4096
        || m.measure.is_none()
        || m.line_height.is_none()
    {
        return 0;
    }
    let Ok(mut a) = Arena::new(
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) },
        unsafe { core::slice::from_raw_parts_mut(b.text, usize::from(b.text_capacity)) },
        if b.hit_capacity == 0 {
            &mut []
        } else {
            unsafe { core::slice::from_raw_parts_mut(b.hits, usize::from(b.hit_capacity)) }
        },
    ) else {
        return 0;
    };
    if view(
        &mut a,
        s,
        (
            unsafe { core::slice::from_raw_parts(text, s.length as usize) },
            unsafe { core::slice::from_raw_parts(display, s.length as usize) },
        ),
        l,
        m,
        layouts,
        unsafe { core::slice::from_raw_parts_mut(b.spans, usize::from(b.span_capacity)) },
    )
    .is_err()
    {
        return 0;
    }
    let Ok(scene) = a.finish(Refresh::Fast, 1) else {
        return 0;
    };
    *out = SceneView {
        commands: scene.commands.as_ptr(),
        text: scene.text.as_ptr(),
        interactions: scene.interactions.as_ptr(),
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: scene.text.len() as u16,
        interaction_count: scene.interactions.len() as u16,
        refresh: 1,
        reserved: 0,
    };
    1
}
/// # Safety
/// Inputs are readable and disjoint from writable position; callbacks are valid.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_cursor_at(
    s: *const State,
    display: *const u8,
    layouts: *const Layouts,
    l: *const Layout,
    m: *const Metrics,
    x: i16,
    y: i16,
    out: *mut u32,
) -> u8 {
    let (Some(s), Some(layouts), Some(l), Some(m), Some(out)) = (
        unsafe { s.as_ref() },
        unsafe { layouts.as_ref() },
        unsafe { l.as_ref() },
        unsafe { m.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if display.is_null()
        || s.length as usize > isize::MAX as usize
        || m.measure.is_none()
        || m.line_height.is_none()
    {
        return 0;
    }
    if let Some(position) = cursor_at(
        s,
        unsafe { core::slice::from_raw_parts(display, s.length as usize) },
        l,
        m,
        layouts,
        x,
        y,
    ) {
        *out = position;
        1
    } else {
        0
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PaintKey {
    pub label: [u8; 16],
    pub alternate: [u8; 8],
    pub value: i16,
    pub kind: u8,
    pub units: u8,
    pub label_present: u8,
    pub alternate_present: u8,
}
fn paint_key(s: &State, layouts: &Layouts, row: u8, col: u8) -> Option<PaintKey> {
    let k = layouts.key(s, row, col)?;
    if k.label.len() >= 16 || k.alternate.len() >= 8 {
        return None;
    }
    let mut out = PaintKey {
        value: k.value,
        kind: k.kind,
        units: k.units,
        label_present: u8::from(!k.label.is_empty()),
        alternate_present: u8::from(!k.alternate.is_empty()),
        ..PaintKey::default()
    };
    out.label[..k.label.len()].copy_from_slice(k.label);
    out.alternate[..k.alternate.len()].copy_from_slice(k.alternate);
    Some(out)
}
/// # Safety
/// State/layouts are readable, output is writable and disjoint. Callback strings
/// are bounded and immutable for this call. Oversized labels leave output intact.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_paint_key(
    s: *const State,
    layouts: *const Layouts,
    row: u8,
    col: u8,
    out: *mut PaintKey,
) -> u8 {
    let (Some(s), Some(layouts), Some(out)) =
        (unsafe { s.as_ref() }, unsafe { layouts.as_ref() }, unsafe {
            out.as_mut()
        })
    else {
        return 0;
    };
    if let Some(key) = paint_key(s, layouts, row, col) {
        *out = key;
        1
    } else {
        0
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, offset_of, size_of};
    #[test]
    fn wire_layout() {
        assert_eq!(
            (
                size_of::<PlacedKey>(),
                align_of::<PlacedKey>(),
                offset_of!(PlacedKey, value),
                offset_of!(PlacedKey, selected)
            ),
            (20, 2, 16, 18)
        );
        let p = size_of::<usize>();
        assert_eq!(
            (
                size_of::<Layout>(),
                align_of::<Layout>(),
                offset_of!(Layout, strings)
            ),
            (88, 4, 52)
        );
        assert_eq!(size_of::<Metrics>(), 3 * p);
        assert_eq!(align_of::<Metrics>(), p);
        assert_eq!(offset_of!(Metrics, line_height), 2 * p);
        assert_eq!(size_of::<Buffer>(), if p == 4 { 28 } else { 48 });
        assert_eq!(offset_of!(Buffer, spans), if p == 4 { 20 } else { 32 });
        assert_eq!(
            (
                size_of::<TextSpan>(),
                align_of::<TextSpan>(),
                offset_of!(TextSpan, raw)
            ),
            (12, 4, 8)
        );
        assert_eq!(align_of::<Buffer>(), p);
        assert_eq!(offset_of!(Buffer, command_capacity), 3 * p);
        assert_eq!(
            (
                size_of::<PaintKey>(),
                align_of::<PaintKey>(),
                offset_of!(PaintKey, value),
                offset_of!(PaintKey, label_present)
            ),
            (30, 2, 24, 28)
        );
    }
}
