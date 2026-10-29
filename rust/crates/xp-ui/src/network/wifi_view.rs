use super::{
    view::resource,
    wifi::{Phase, State},
};
use crate::{
    list,
    model::ListState,
    scene::{Arena, Command, Error, HitSink, Interaction, Kind, Rect, Refresh},
    settings::{
        hits, settings_ffi,
        view::{self, Content, Resources},
    },
};
use core::ffi::{c_char, c_void};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub base: view::Layout,
    pub legacy_body_font: i32,
    pub popup_border: u8,
    pub popup_radius: u8,
    pub reserved: [u8; 2],
}
pub const HEADER: u32 = 0x100000;
pub const SUBHEADER: u32 = 0x100001;
pub const LIST_FOOTER: u32 = 0x100002;
pub const SAVED_FOOTER: u32 = 0x100003;
pub const AUTO_FOOTER: u32 = 0x100004;
pub const SAVE_FOOTER: u32 = 0x100005;
pub const FORGET_FOOTER: u32 = 0x100006;
pub const CONNECTED_FOOTER: u32 = 0x100007;
pub const FAILED_FOOTER: u32 = 0x100008;
pub const LEGEND: u32 = 0x100009;
pub const EMPTY: u32 = 0x10000a;
pub const RETRY: u32 = 0x10000b;
pub const PRESS_SCAN: u32 = 0x10000c;
pub const SCANNING: u32 = 0x10000d;
pub const FINDING_SAVED: u32 = 0x10000e;
pub const CONNECTING: u32 = 0x10000f;
pub const CONNECTING_SAVED: u32 = 0x100010;
pub const TO_SSID: u32 = 0x100011;
pub const NETWORK_SSID: u32 = 0x100012;
pub const IP: u32 = 0x100013;
pub const CONNECTED: u32 = 0x100014;
pub const FAILED: u32 = 0x100015;
pub const ERROR: u32 = 0x100016;
pub const FORGET: u32 = 0x100017;
pub const PROMPT_SSID: u32 = 0x100018;
pub const REMOVE: u32 = 0x100019;
pub const SAVE_PASSWORD: u32 = 0x10001a;
pub const CANCEL: u32 = 0x10001b;
pub const YES: u32 = 0x10001c;
pub const NO: u32 = 0x10001d;
pub const PROMPT_CONNECTED: u32 = 0x10001e;
pub const PROMPT_FORGET: u32 = 0x10001f;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub rows: settings_ffi::Services,
    pub text: Option<unsafe extern "C" fn(*mut c_void, u32) -> *const c_char>,
}
impl Services {
    #[inline(never)]
    fn text(&self, handle: u32) -> Result<*const c_char, Error> {
        let p = unsafe { self.text.ok_or(Error::Invalid)?(self.rows.context, handle) };
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
    fn window(&mut self, first: u16, requested: u16) -> Result<(), Error> {
        self.rows.window(first, requested)
    }
    fn row(&mut self, index: u16) -> Result<view::Row, Error> {
        self.rows.row(index)
    }
    fn width(&mut self, h: u32, font: i32, bold: u8) -> Result<i32, Error> {
        if h < HEADER {
            return self.rows.width(h, font, bold);
        }
        metric(unsafe {
            self.rows.width.ok_or(Error::Invalid)?(self.rows.context, self.text(h)?, font, bold)
        })
    }
    fn height(&mut self, h: u32, font: i32, bold: u8, lines: u8, width: i16) -> Result<i32, Error> {
        if h < HEADER {
            return self.rows.height(h, font, bold, lines, width);
        }
        metric(unsafe {
            self.rows.height.ok_or(Error::Invalid)?(
                self.rows.context,
                self.text(h)?,
                font,
                bold,
                lines,
                width,
            )
        })
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        self.rows.line(font)
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Hits {
    pub rows: hits::Layout,
    pub buttons: [Rect; 2],
    pub generation: u32,
    pub count: u16,
    pub phase: u8,
    pub button_action: u8,
}
impl HitSink for Hits {
    fn push(&mut self, hit: Interaction) -> Result<(), Error> {
        if hit.action == 1 {
            return self.rows.push(hit);
        }
        if !(2..=3).contains(&hit.action) || !(0..=1).contains(&hit.value) {
            return Err(Error::Invalid);
        }
        self.buttons[hit.value as usize] = hit.rect;
        self.button_action = hit.action as u8;
        Ok(())
    }
}
impl Hits {
    pub fn hit(&self, state: &State, x: i16, y: i16) -> Option<Interaction> {
        if self.generation != state.generation
            || self.phase != state.phase
            || self.count != state.count
        {
            return None;
        }
        if self.button_action != 0 {
            for (value, rect) in self.buttons.iter().enumerate() {
                if rect.contains(x, y) {
                    return Some(Interaction {
                        rect: *rect,
                        action: u32::from(self.button_action),
                        value: value as i32,
                        ..Interaction::default()
                    });
                }
            }
            return None;
        }
        self.rows.hit(x, y)
    }
}
fn narrow(v: i32) -> Result<i16, Error> {
    i16::try_from(v).map_err(|_| Error::Invalid)
}
#[inline(never)]
fn rect(x: i32, y: i32, w: i32, h: i32) -> Result<Rect, Error> {
    Ok(Rect {
        x: narrow(x)?,
        y: narrow(y)?,
        w: narrow(w)?,
        h: narrow(h)?,
    })
}
#[inline(never)]
fn text(
    a: &mut Arena<'_>,
    r: Rect,
    id: u32,
    font: i32,
    bold: u8,
    lines: u8,
    align: u8,
) -> Result<(), Error> {
    view::text(a, r, id, 1, font, 0, bold, align, lines)
}
#[inline(never)]
fn fill(a: &mut Arena<'_>, r: Rect, paint: u8, radius: u8) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Fill as u8,
        rect: r,
        paint,
        radius,
        ..Command::default()
    })
}
struct Button {
    id: u32,
    font: i32,
    selected: bool,
    active: bool,
    action: u32,
    value: i32,
    min_touch: i16,
    screen: Rect,
}
fn button(a: &mut Arena<'_>, r: Rect, p: Button) -> Result<(), Error> {
    let Button {
        id,
        font,
        selected,
        active,
        action,
        value,
        min_touch,
        screen,
    } = p;
    view::button(
        a,
        r,
        id,
        font,
        if active {
            0
        } else if selected {
            2
        } else {
            1
        },
        0,
        u8::from(active),
    )?;
    let w = r.w.max(min_touch).min(screen.w);
    let h = r.h.max(min_touch).min(screen.h);
    let x = (r.x - (w - r.w) / 2).clamp(0, screen.w - w);
    let y = (r.y - (h - r.h) / 2).clamp(0, screen.h - h);
    a.hit(Interaction {
        rect: Rect { x, y, w, h },
        action,
        value,
        generation: 1,
        inputs: 1,
        ..Interaction::default()
    })
}
#[inline(never)]
fn prompt(
    a: &mut Arena<'_>,
    s: &State,
    l: &Layout,
    body: Rect,
    r: &mut dyn Resources,
) -> Result<(), Error> {
    let b = &l.base;
    let forget = s.phase == Phase::Forget as u8;
    let title = if forget {
        PROMPT_FORGET
    } else {
        PROMPT_CONNECTED
    };
    let msg = if forget { REMOVE } else { SAVE_PASSWORD };
    let width = (b.list.screen.w * 3 / 4).min(body.w);
    let content_width = width - 32;
    let heights = [
        r.height(title, b.small_font, 1, 1, content_width)?,
        r.height(PROMPT_SSID, b.list.font, b.list.bold, 2, content_width)?,
        r.height(msg, b.small_font, 0, 3, content_width)?,
    ];
    let height = narrow(24 + heights.iter().sum::<i32>() + 24 + 96)?;
    let panel = Rect {
        x: body.x + (body.w - width) / 2,
        y: body.y + (body.h - height) / 2,
        w: width,
        h: height,
    };
    fill(a, panel, 1, l.popup_radius)?;
    if l.popup_border != 0 {
        a.push(Command {
            kind: Kind::Stroke as u8,
            rect: panel,
            width: l.popup_border,
            radius: l.popup_radius,
            ..Command::default()
        })?;
    }
    let x = i32::from(panel.x) + 16;
    let mut y = i32::from(panel.y) + 12;
    text(
        a,
        rect(x, y, i32::from(content_width), 1)?,
        title,
        b.small_font,
        1,
        1,
        0,
    )?;
    y += heights[0] + 8;
    text(
        a,
        rect(x, y, i32::from(content_width), 1)?,
        PROMPT_SSID,
        b.list.font,
        b.list.bold,
        2,
        0,
    )?;
    y += heights[1] + 8;
    text(
        a,
        rect(x, y, i32::from(content_width), heights[2])?,
        msg,
        b.small_font,
        0,
        3,
        0,
    )?;
    y += heights[2] + 8;
    let selection = if forget {
        s.forget_selection
    } else {
        s.save_selection
    };
    let minimum = narrow((r.line(b.list.font)? + 14).max(44))?;
    for i in 0..2 {
        let label = if forget {
            if i == 0 {
                CANCEL
            } else {
                FORGET
            }
        } else if i == 0 {
            YES
        } else {
            NO
        };
        button(
            a,
            rect(x, y, i32::from(content_width), 44)?,
            Button {
                id: label,
                font: b.small_font,
                selected: selection == i,
                active: s.list.active == i32::from(i),
                action: 3,
                value: i32::from(i),
                min_touch: minimum,
                screen: b.list.screen,
            },
        )?;
        y += 52;
    }
    resource(
        a,
        if forget { FORGET_FOOTER } else { SAVE_FOOTER },
        b.list.screen,
    )
}
struct WifiRows<'a> {
    content: Content<'a>,
    flash: i32,
    active: i32,
}
impl list::Rows for WifiRows<'_> {
    fn height(&mut self, index: u16, width: i16, style: list::Style) -> Result<i16, Error> {
        self.content.height(index, width, style)
    }
    fn paint(
        &mut self,
        a: &mut Arena<'_>,
        index: u16,
        r: Rect,
        style: list::Style,
        fg: u8,
        generation: u32,
    ) -> Result<(), Error> {
        let focused = i32::from(index) == self.flash
            && self.active != self.flash
            && i32::from(r.y) + i32::from(r.h) <= i32::from(style.area.y) + i32::from(style.area.h);
        if focused {
            fill(a, r, 2, style.radius)?;
        }
        self.content
            .paint(a, index, r, style, if focused { 0 } else { fg }, generation)
    }
}
fn body(l: &Layout) -> Result<Rect, Error> {
    let b = &l.base;
    let safe = b.safe;
    let device_safe = b.list.area;
    let left = i32::from(safe.x).max(i32::from(device_safe.x));
    let right = i32::from(safe.x + safe.w).min(i32::from(device_safe.x + device_safe.w));
    let top = (i32::from(safe.y)
        + i32::from(b.top_padding + b.header_height + b.tab_height + b.spacing))
    .max(i32::from(device_safe.y));
    let bottom =
        i32::from(safe.y + safe.h - b.spacing * 2).min(i32::from(device_safe.y + device_safe.h));
    rect(left, top, (right - left).max(0), (bottom - top).max(0))
}
#[inline(never)]
pub fn view(
    a: &mut Arena<'_>,
    s: &State,
    l: &Layout,
    r: &mut dyn Resources,
) -> Result<(ListState, Refresh), Error> {
    let phase = Phase::read(s.phase).ok_or(Error::Invalid)?;
    let b = &l.base;
    let screen = b.list.screen;
    let safe = b.safe;
    if !(1..=8192).contains(&screen.w)
        || !(1..=8192).contains(&screen.h)
        || b.touch > 1
        || [
            b.top_padding,
            b.header_height,
            b.tab_height,
            b.spacing,
            b.side_padding,
        ]
        .iter()
        .any(|v| !(0..=1024).contains(v))
    {
        return Err(Error::Invalid);
    }
    if matches!(phase, Phase::Password | Phase::HiddenSsid) {
        return Ok((s.list, Refresh::None));
    }
    a.push(Command {
        kind: Kind::Clear as u8,
        paint: 1,
        ..Command::default()
    })?;
    resource(
        a,
        HEADER,
        Rect {
            x: 0,
            y: b.top_padding,
            w: screen.w,
            h: b.header_height,
        },
    )?;
    resource(
        a,
        SUBHEADER,
        Rect {
            x: safe.x,
            y: b.top_padding + b.header_height,
            w: safe.w,
            h: b.tab_height,
        },
    )?;
    let body = body(l)?;
    let mut next = s.list;
    match phase {
        Phase::List => {
            if s.count == 0 {
                let line = narrow(r.line(b.list.font)?)?;
                text(
                    a,
                    Rect {
                        x: body.x,
                        y: body.y + (body.h - line) / 2,
                        w: body.w,
                        h: line,
                    },
                    EMPTY,
                    b.list.font,
                    b.list.bold,
                    1,
                    1,
                )?;
                if b.touch != 0 {
                    let w = body.w / 2;
                    button(
                        a,
                        Rect {
                            x: body.x + (body.w - w) / 2,
                            y: body.y + body.h * 2 / 3,
                            w,
                            h: b.preview_height,
                        },
                        Button {
                            id: RETRY,
                            font: b.list.font,
                            selected: false,
                            active: s.list.active == 0,
                            action: 2,
                            value: 0,
                            min_touch: narrow((r.line(b.list.font)? + 14).max(44))?,
                            screen,
                        },
                    )?;
                } else {
                    let line = narrow(r.line(b.fixed_font)?)?;
                    text(
                        a,
                        Rect {
                            x: safe.x,
                            y: safe.y + (safe.h - line) / 2 + line + 10,
                            w: safe.w,
                            h: line,
                        },
                        PRESS_SCAN,
                        b.help_font,
                        0,
                        0,
                        1,
                    )?;
                }
            } else {
                let mut style = b.list;
                style.area = body;
                let padding = if b.touch != 0 { 8 } else { 4 };
                let line = r.line(style.font)?;
                style.min_touch = narrow((line + 14).max(44))?;
                let mut minimum = (line + padding * 2).max(i32::from(style.row_height));
                if b.touch != 0 {
                    minimum = minimum.max(i32::from(style.min_touch)).max(56);
                    style.row_gap = style.row_gap.max(6);
                }
                style.row_height = narrow(minimum)?;
                let mut content = Content::new(r, s.count, b.small_font, padding as i16, 2, 8);
                content.capacity = 32;
                content.balance_value = false;
                let mut rows = WifiRows {
                    content,
                    flash: s.flash_index,
                    active: s.list.active,
                };
                next = list::view_rows(a, s.list, style, s.count, 1, &mut rows)?;
            }
            resource(
                a,
                LEGEND,
                Rect {
                    x: safe.x,
                    y: safe.y + safe.h - b.side_padding - 15,
                    w: safe.w,
                    h: 20,
                },
            )?;
            resource(
                a,
                if b.flags & 1 != 0 {
                    SAVED_FOOTER
                } else {
                    LIST_FOOTER
                },
                screen,
            )?;
        }
        Phase::Save | Phase::Forget => prompt(a, s, l, body, r)?,
        Phase::Scanning | Phase::Connecting | Phase::AutoConnecting => {
            let line = narrow(r.line(b.fixed_font)?)?;
            let top = safe.y + (safe.h - line) / 2;
            let status = Rect {
                x: safe.x + b.side_padding,
                y: safe.y,
                w: safe.w - b.side_padding * 2,
                h: if phase == Phase::Scanning {
                    safe.h
                } else {
                    top - b.spacing - safe.y
                },
            };
            let id = if phase == Phase::Scanning {
                if s.automatic != 0 {
                    FINDING_SAVED
                } else {
                    SCANNING
                }
            } else if s.automatic != 0 {
                CONNECTING_SAVED
            } else {
                CONNECTING
            };
            text(
                a,
                status,
                id,
                if phase == Phase::Scanning {
                    b.fixed_font
                } else {
                    l.legacy_body_font
                },
                u8::from(phase != Phase::Scanning),
                2,
                1,
            )?;
            if phase != Phase::Scanning {
                text(
                    a,
                    Rect {
                        x: status.x,
                        y: top,
                        w: status.w,
                        h: line,
                    },
                    TO_SSID,
                    b.fixed_font,
                    0,
                    0,
                    1,
                )?;
            }
            if s.automatic != 0 {
                resource(a, AUTO_FOOTER, screen)?;
            }
        }
        Phase::Connected => {
            let line = narrow(r.line(b.fixed_font)?)?;
            let top = safe.y + (safe.h - line * 4) / 2;
            text(
                a,
                Rect {
                    x: safe.x,
                    y: top - 30,
                    w: safe.w,
                    h: line,
                },
                CONNECTED,
                l.legacy_body_font,
                1,
                0,
                1,
            )?;
            text(
                a,
                Rect {
                    x: safe.x + b.side_padding,
                    y: top + 10,
                    w: safe.w - b.side_padding * 2,
                    h: line,
                },
                NETWORK_SSID,
                b.fixed_font,
                0,
                0,
                1,
            )?;
            text(
                a,
                Rect {
                    x: safe.x,
                    y: top + 40,
                    w: safe.w,
                    h: line,
                },
                IP,
                b.fixed_font,
                0,
                0,
                1,
            )?;
            resource(a, CONNECTED_FOOTER, screen)?;
        }
        Phase::Failed => {
            let line = narrow(r.line(b.fixed_font)?)?;
            let top = safe.y + (safe.h - line * 2) / 2;
            text(
                a,
                Rect {
                    x: safe.x,
                    y: top - 20,
                    w: safe.w,
                    h: line,
                },
                FAILED,
                l.legacy_body_font,
                1,
                0,
                1,
            )?;
            text(
                a,
                Rect {
                    x: safe.x,
                    y: top + 20,
                    w: safe.w,
                    h: line,
                },
                ERROR,
                b.fixed_font,
                0,
                0,
                1,
            )?;
            resource(a, FAILED_FOOTER, screen)?;
        }
        Phase::Password | Phase::HiddenSsid => {}
    }
    Ok((next, Refresh::Fast))
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Budget {
    pub rows: u16,
    pub commands: u16,
}
pub fn budget(state: &State, l: &Layout, line: i32) -> Result<Budget, Error> {
    let phase = Phase::read(state.phase).ok_or(Error::Invalid)?;
    let b = &l.base;
    let commands = match phase {
        Phase::List if state.count != 0 => {
            if line <= 0 || line > 1024 || b.list.row_gap < 0 {
                return Err(Error::Invalid);
            }
            let padding = if b.touch != 0 { 8 } else { 4 };
            let mut height = (line + padding * 2).max(i32::from(b.list.row_height));
            let mut gap = i32::from(b.list.row_gap);
            if b.touch != 0 {
                height = height.max(line + 14).max(56);
                gap = gap.max(6);
            }
            let h = i32::from(body(l)?.h);
            let visible = (h + gap + height + gap - 1) / (height + gap);
            let rows = i32::from(state.count).min(visible.max(1));
            if rows > 32 {
                return Err(Error::Capacity);
            }
            return Ok(Budget {
                rows: rows as u16,
                commands: (rows * 3 + 11) as u16,
            });
        }
        Phase::List => 8,
        Phase::Save | Phase::Forget => 13,
        Phase::Scanning => 4 + u16::from(state.automatic != 0),
        Phase::Connecting | Phase::AutoConnecting => 5 + u16::from(state.automatic != 0),
        Phase::Connected => 7,
        Phase::Failed => 6,
        Phase::Password | Phase::HiddenSsid => 0,
    };
    Ok(Budget { rows: 0, commands })
}

const _: () = {
    assert!(core::mem::size_of::<Layout>() == 88);
    assert!(core::mem::size_of::<Hits>() == 108);
    assert!(core::mem::size_of::<Budget>() == 4);
    assert!(
        core::mem::size_of::<Services>()
            == core::mem::size_of::<settings_ffi::Services>() + core::mem::size_of::<usize>()
    );
};

const _: () = {
    assert!(HEADER == 0x100000);
    assert!(SUBHEADER == 0x100001);
    assert!(LIST_FOOTER == 0x100002);
    assert!(SAVED_FOOTER == 0x100003);
    assert!(AUTO_FOOTER == 0x100004);
    assert!(SAVE_FOOTER == 0x100005);
    assert!(FORGET_FOOTER == 0x100006);
    assert!(CONNECTED_FOOTER == 0x100007);
    assert!(FAILED_FOOTER == 0x100008);
    assert!(LEGEND == 0x100009);
    assert!(EMPTY == 0x10000a);
    assert!(RETRY == 0x10000b);
    assert!(PRESS_SCAN == 0x10000c);
    assert!(SCANNING == 0x10000d);
    assert!(FINDING_SAVED == 0x10000e);
    assert!(CONNECTING == 0x10000f);
    assert!(CONNECTING_SAVED == 0x100010);
    assert!(TO_SSID == 0x100011);
    assert!(NETWORK_SSID == 0x100012);
    assert!(IP == 0x100013);
    assert!(CONNECTED == 0x100014);
    assert!(FAILED == 0x100015);
    assert!(ERROR == 0x100016);
    assert!(FORGET == 0x100017);
    assert!(PROMPT_SSID == 0x100018);
    assert!(REMOVE == 0x100019);
    assert!(SAVE_PASSWORD == 0x10001a);
    assert!(CANCEL == 0x10001b);
    assert!(YES == 0x10001c);
    assert!(NO == 0x10001d);
    assert!(PROMPT_CONNECTED == 0x10001e);
    assert!(PROMPT_FORGET == 0x10001f);
};
