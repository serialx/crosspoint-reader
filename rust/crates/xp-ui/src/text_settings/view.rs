use super::State;
use crate::{
    list,
    reader_chrome::{
        hits::Layout as Hits,
        list::{Content, Resources, Screen, Theme, FOOTER, HEADER},
    },
    scene::{Arena, Command, Error, Interaction, Kind, Rect},
};
pub const PREVIEW: u32 = 0x8000_0020;
pub const TAB_BASE: u32 = 0x8000_0030;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub theme: Theme,
    pub bezel: Rect,
    pub preview_percent: i16,
    pub preview_padding: i16,
    pub side_padding: i16,
    pub tab_spacing: i16,
    pub underline: i16,
    pub full_slot: u8,
    pub reserved: u8,
}
fn resource(arena: &mut Arena<'_>, rect: Rect, id: u32, g: u32) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::Page as u8,
        rect,
        resource: id,
        generation: g,
        ..Command::default()
    })
}
fn i16n(v: i32) -> Result<i16, Error> {
    i16::try_from(v).map_err(|_| Error::Invalid)
}
fn tabs(
    arena: &mut Arena<'_>,
    state: &State,
    l: Layout,
    area: Rect,
    r: &mut dyn Resources,
    g: u32,
) -> Result<i16, Error> {
    let focused = state.tabs[usize::from(state.tab)].selected == 0;
    let full = l.full_slot != 0;
    let t = l.theme;
    let font = if full { t.body_font } else { t.small_font };
    let band = i16n(
        (if t.touch != 0 {
            50
        } else {
            i32::from(t.tab_height)
        })
        .max(r.line(font)? + 10),
    )?;
    let divider = l.underline.max(1);
    let rect = Rect {
        x: area.x - l.bezel.x,
        y: area.y,
        w: area.w + t.screen.w - l.bezel.w,
        h: band,
    };
    if focused && !full {
        arena.push(Command {
            rect,
            paint: 2,
            ..Command::default()
        })?;
    }
    let slots = Rect {
        x: area.x + l.side_padding,
        y: area.y,
        w: area.w - 2 * l.side_padding,
        h: band - divider,
    };
    let gap = if full { 0 } else { l.tab_spacing.max(0) };
    let slot_width = (slots.w - 3 * gap) / 4;
    if slot_width <= 8 || slots.h < 12 {
        return Err(Error::Invalid);
    }
    for i in 0..4 {
        let x = slots.x + i * (slot_width + gap);
        let slot = Rect {
            x,
            y: slots.y,
            w: if i == 3 {
                slots.x + slots.w - x
            } else {
                slot_width
            },
            h: slots.h,
        };
        let top = if full { 4 } else { 2 };
        let bottom = if full {
            7
        } else if focused {
            4
        } else {
            0
        };
        let mut pill = Rect {
            x: slot.x + 4,
            y: slot.y + top,
            w: slot.w - 8,
            h: slot.h - top - bottom,
        };
        if pill.w <= 0 || pill.h <= 0 {
            pill = slot;
        }
        let selected = i as u8 == state.tab;
        if selected {
            arena.push(Command {
                rect: pill,
                paint: if focused {
                    0
                } else if full {
                    3
                } else {
                    2
                },
                radius: if focused || full { t.row_radius } else { 0 },
                ..Command::default()
            })?;
            if !focused && !full {
                arena.push(Command {
                    rect: Rect {
                        y: pill.y + pill.h - 2,
                        h: 2,
                        ..pill
                    },
                    ..Command::default()
                })?;
            }
        }
        let mut content = Rect {
            y: pill.y + 2,
            h: pill.h - 4,
            ..pill
        };
        if content.h <= 0 {
            content = pill;
        }
        arena.push(Command {
            kind: Kind::TextHandle as u8,
            rect: content,
            resource: TAB_BASE + i as u32,
            generation: g,
            font,
            style: if full { t.title_bold } else { 0 },
            paint: u8::from(selected && (focused || full)),
            align: 1,
            max_lines: 1,
            ..Command::default()
        })?;
        let mut hit = slot;
        if hit.w < 44 {
            hit.x -= (44 - hit.w) / 2;
            hit.w = 44;
        }
        if hit.h < 44 {
            hit.y -= (44 - hit.h) / 2;
            hit.h = 44;
        }
        hit.x = hit.x.clamp(0, (t.screen.w - hit.w).max(0));
        hit.y = hit.y.clamp(0, (t.screen.h - hit.h).max(0));
        // SDK edge snapping expands a touch target within twelve pixels of an edge.
        if hit.x < 12 {
            hit.w += hit.x;
            hit.x = 0;
        }
        if t.screen.w - hit.x - hit.w < 12 {
            hit.w = t.screen.w - hit.x;
        }
        if hit.y < 12 {
            hit.h += hit.y;
            hit.y = 0;
        }
        if t.screen.h - hit.y - hit.h < 12 {
            hit.h = t.screen.h - hit.y;
        }
        arena.hit(Interaction {
            rect: hit,
            action: 2,
            value: i32::from(i),
            generation: g,
            inputs: 1,
            flags: 0,
        })?;
    }
    arena.push(Command {
        rect: Rect {
            y: rect.y + rect.h - divider,
            h: divider,
            ..rect
        },
        ..Command::default()
    })?;
    Ok(band)
}
pub fn view(
    arena: &mut Arena<'_>,
    s: State,
    l: Layout,
    r: &mut dyn Resources,
    count: u16,
    capacity: u16,
    g: u32,
) -> Result<State, Error> {
    l.theme.validate()?;
    if !s.valid()
        || count != s.count() as u16
        || !(0..=100).contains(&l.preview_percent)
        || l.full_slot > 1
        || l.side_padding < 0
        || l.preview_padding < 0
    {
        return Err(Error::Invalid);
    }
    let t = l.theme;
    let safe = t.safe;
    if l.bezel.x < 0
        || l.bezel.y < 0
        || l.bezel.w <= 0
        || l.bezel.w > t.screen.w
        || l.bezel.h < 0
        || l.bezel.h > t.screen.h
        || l.side_padding > safe.w / 2
        || l.tab_spacing > safe.w / 4
        || l.underline > t.tab_height
    {
        return Err(Error::Invalid);
    }
    let after = i32::from(safe.y)
        + i32::from(t.top_padding)
        + i32::from(t.header_height)
        + i32::from(t.spacing);
    let usable = i32::from(safe.y) + i32::from(safe.h) - after - i32::from(t.spacing);
    let preview = Rect {
        x: safe.x,
        y: i16n(after)?,
        w: safe.w,
        h: i16n(usable * i32::from(l.preview_percent) / 100)?,
    };
    if preview.h < 0 || after + i32::from(preview.h) > i32::from(safe.y) + i32::from(safe.h) {
        return Err(Error::Invalid);
    }
    arena.push(Command {
        kind: Kind::Clear as u8,
        paint: 1,
        rect: t.screen,
        ..Command::default()
    })?;
    resource(
        arena,
        Rect {
            x: safe.x,
            y: safe.y + t.top_padding,
            w: safe.w,
            h: t.header_height,
        },
        HEADER,
        g,
    )?;
    arena.push(Command {
        kind: Kind::Clip as u8,
        rect: preview,
        ..Command::default()
    })?;
    resource(arena, preview, PREVIEW, g)?;
    arena.push(Command {
        kind: Kind::Pop as u8,
        ..Command::default()
    })?;
    let y = preview.y + preview.h;
    let band = tabs(arena, &s, l, Rect { y, ..safe }, r, g)?;
    let top = y + band + t.spacing;
    let area = Rect {
        y: top,
        h: (safe.y + safe.h - top).max(0),
        ..safe
    };
    let style = t.style(Screen::Menu, area, r)?;
    let padding = if t.touch != 0 { 8 } else { 4 };
    let mut content = Content::new(
        r,
        count,
        capacity,
        Screen::Menu,
        t.small_font,
        t.small_font,
        padding,
    );
    let mut nav = s.tabs[usize::from(s.tab)];
    nav.selected -= 1;
    nav.active = -1;
    if nav.selected < 0 && nav.follow != 0 {
        nav.top = 0;
    }
    let next = list::view_rows(arena, nav, style, count, g, &mut content)?;
    resource(arena, t.screen, FOOTER, g)?;
    let mut result = s;
    result.tabs[usize::from(s.tab)] = next;
    result.tabs[usize::from(s.tab)].selected += 1;
    Ok(result)
}
// The shared compact hit sink stores row heights only when they differ.
pub type HitLayout = Hits;
