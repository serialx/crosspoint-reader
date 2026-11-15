//! Label-only lists used by chapter selection. Strings remain in the immutable
//! book model; each row stores a generation-checked text handle, not a copy.
use crate::{
    model::ListState,
    scene::{Arena, Command, Error, Interaction, Kind, Rect},
};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Style {
    pub area: Rect,
    pub screen: Rect,
    pub min_touch: i16,
    pub font: i32,
    pub row_height: i16,
    pub row_gap: i16,
    pub inset: i16,
    pub padding: i16,
    pub scroll_width: i16,
    pub radius: u8,
    pub selection: u8,
    pub scroll_left: u8,
    pub bold: u8,
}

/// Per-row content shares the chapter list's viewport, selection and hit rules.
pub trait Rows {
    /// Decorative top padding and total heading height, including that padding.
    fn section(&mut self, _: u16, _: bool, _: Style) -> Result<(i16, i16), Error> {
        Ok((0, 0))
    }
    fn paint_section(
        &mut self,
        _: &mut Arena<'_>,
        _: u16,
        _: Rect,
        _: Style,
        _: u32,
    ) -> Result<(), Error> {
        Ok(())
    }
    fn action(&self) -> u32 {
        1
    }
    fn height(&mut self, index: u16, width: i16, style: Style) -> Result<i16, Error>;
    fn paint(
        &mut self,
        arena: &mut Arena<'_>,
        index: u16,
        rect: Rect,
        style: Style,
        foreground: u8,
        generation: u32,
    ) -> Result<(), Error>;
    fn available(&self, _: u16) -> bool {
        true
    }
    fn overlay(
        &mut self,
        _: &mut Arena<'_>,
        _: u16,
        _: Rect,
        _: Style,
        _: u32,
    ) -> Result<(), Error> {
        Ok(())
    }
    fn partial(&self) -> bool {
        true
    }
    fn nav_managed(&self) -> bool {
        true
    }
}
struct Labels;
impl Rows for Labels {
    fn height(&mut self, _: u16, _: i16, style: Style) -> Result<i16, Error> {
        Ok(style.row_height)
    }
    fn paint(
        &mut self,
        arena: &mut Arena<'_>,
        index: u16,
        rect: Rect,
        style: Style,
        foreground: u8,
        generation: u32,
    ) -> Result<(), Error> {
        arena.push(Command {
            kind: Kind::TextHandle as u8,
            rect: Rect {
                x: rect.x + style.padding,
                w: rect.w - 2 * style.padding,
                ..rect
            },
            resource: u32::from(index) + 1,
            generation,
            font: style.font,
            paint: foreground,
            style: style.bold,
            max_lines: 1,
            ..Command::default()
        })
    }
}

pub fn view(
    arena: &mut Arena<'_>,
    state: ListState,
    style: Style,
    count: u16,
    generation: u32,
) -> Result<ListState, Error> {
    view_rows(arena, state, style, count, generation, &mut Labels)
}

pub fn view_rows(
    arena: &mut Arena<'_>,
    state: ListState,
    style: Style,
    count: u16,
    generation: u32,
    rows: &mut dyn Rows,
) -> Result<ListState, Error> {
    if !(-1..=65535).contains(&state.selected)
        || !(0..=65535).contains(&state.top)
        || !(0..=64).contains(&state.drawn)
        || !(0..=65535).contains(&state.drawn_count)
        || style.screen.x != 0
        || style.screen.y != 0
        || !(1..=8192).contains(&style.screen.w)
        || !(1..=8192).contains(&style.screen.h)
        || style.area.x < 0
        || style.area.y < 0
        || i32::from(style.area.x) + i32::from(style.area.w) > i32::from(style.screen.w)
        || i32::from(style.area.y) + i32::from(style.area.h) > i32::from(style.screen.h)
        || style.min_touch < 0
        || style.min_touch > style.screen.w
        || style.row_height > 8192
        || style.row_gap > 8192
        || style.padding > style.area.w / 2
        || style.inset > style.area.w / 2
        || style.scroll_width > style.area.w
        || style.row_height <= 0
        || style.row_gap < 0
        || style.inset < 0
        || style.padding < 0
        || style.scroll_width < 0
        || style.area.w <= 0
        || style.area.h < 0
        || style.selection > 3
    {
        return Err(Error::Invalid);
    }
    let mut next = state;
    let height = i32::from(style.area.h);
    let row_height = i32::from(style.row_height);
    let gap = i32::from(style.row_gap);
    let visible = ((height + gap) / (row_height + gap)).max(1);
    next.visible = visible;
    let count = i32::from(count);
    next.selected = next.selected.clamp(-1, (count - 1).max(0));
    next.top = next.top.clamp(0, (count - 1).max(0));
    if count <= 0 {
        next.selected = 0;
        next.top = 0;
        return Ok(next);
    }
    let trusted = state.drawn > 0 && (state.drawn_count == 0 || state.drawn_count == count);
    if state.follow != 0 && next.selected >= 0 {
        if next.selected < next.top {
            next.top = next.selected;
        } else if next.selected >= next.top + visible {
            next.top = next.selected - visible + 1;
        }
        next.top = next.top.clamp(0, (count - visible).max(0));
        if trusted && state.selected >= state.top && state.selected < state.top + state.drawn {
            next.top = state.top;
        }
    }
    let page_size = if trusted {
        state.drawn.min(visible)
    } else {
        visible
    };
    next.top = next
        .top
        .saturating_add(state.pending_scroll)
        .clamp(0, (count - page_size).max(0));
    next.follow = 0;
    next.pending_scroll = 0;
    let overflows = count > visible || trusted && state.drawn < count;
    let cut = if style.scroll_width > 0 && (overflows || rows.nav_managed()) {
        (i32::from(style.scroll_width) + 2 - i32::from(style.inset)).max(0)
    } else {
        0
    };
    let row_x = i32::from(style.area.x)
        + i32::from(style.inset)
        + if style.scroll_left != 0 { cut } else { 0 };
    let row_width = i32::from(style.area.w) - 2 * i32::from(style.inset) - cut;
    if row_width <= 0 || row_width > i32::from(i16::MAX) {
        return Err(Error::Invalid);
    }
    let bottom = i32::from(style.area.y) + height;
    let mut y = i32::from(style.area.y);
    let mut drawn = 0;
    let mut selected_drawn = false;
    for index in next.top..count {
        if y >= bottom || !rows.available(index as u16) {
            break;
        }
        let item_height = rows.height(index as u16, row_width as i16, style)?;
        if !(1..=8192).contains(&item_height) {
            return Err(Error::Invalid);
        }
        let (section_pad, section_height) = rows.section(index as u16, index == next.top, style)?;
        if section_pad < 0 || section_height < section_pad || section_height > 8192 {
            return Err(Error::Invalid);
        }
        let row_height = i32::from(item_height);
        let partial = y + i32::from(section_height) + row_height > bottom;
        if partial && !rows.partial() {
            break;
        }
        if partial && bottom - y - i32::from(section_pad) < 18 {
            break;
        }
        if partial {
            arena.push(Command {
                kind: Kind::Clip as u8,
                rect: Rect {
                    x: row_x as i16,
                    y: style.area.y,
                    w: row_width as i16,
                    h: style.area.h,
                },
                ..Command::default()
            })?;
        }
        if section_height > 0 {
            rows.paint_section(
                arena,
                index as u16,
                Rect {
                    x: row_x as i16,
                    y: (y + i32::from(section_pad)) as i16,
                    w: row_width as i16,
                    h: section_height - section_pad,
                },
                style,
                generation,
            )?;
            y += i32::from(section_height);
        }
        let rect = Rect {
            x: row_x as i16,
            y: y as i16,
            w: row_width as i16,
            h: item_height,
        };
        let selected = !partial && index == next.selected;
        let active = !partial && index == state.active;
        selected_drawn |= selected;
        let inverted = (selected && style.selection == 0) || (active && style.selection != 1);
        let background = if inverted {
            0
        } else if (selected || active) && style.selection == 1 {
            2
        } else {
            1
        };
        let foreground = u8::from(inverted);
        arena.push(Command {
            rect,
            paint: background,
            radius: style.radius,
            ..Command::default()
        })?;
        rows.paint(arena, index as u16, rect, style, foreground, generation)?;
        if !partial {
            let mut hit = Rect {
                x: rect.x
                    - if !overflows && style.scroll_left != 0 {
                        cut as i16
                    } else {
                        0
                    },
                w: rect.w + if !overflows { cut as i16 } else { 0 },
                ..rect
            };
            if hit.w < style.min_touch {
                let delta = style.min_touch - hit.w;
                hit.x -= delta / 2;
                hit.w = style.min_touch;
            }
            hit.x = hit.x.max(style.screen.x);
            let right = i32::from(style.screen.x) + i32::from(style.screen.w);
            if i32::from(hit.x) + i32::from(hit.w) > right {
                hit.x = (right - i32::from(hit.w)) as i16;
            }
            if hit.x - style.screen.x < 12 {
                hit.w += hit.x - style.screen.x;
                hit.x = style.screen.x;
            }
            if right - i32::from(hit.x) - i32::from(hit.w) < 12 {
                hit.w = (right - i32::from(hit.x)) as i16;
            }
            arena.hit(Interaction {
                rect: hit,
                action: rows.action(),
                value: index,
                generation,
                inputs: 1,
                flags: 0,
            })?;
            drawn += 1;
            if selected && style.selection == 2 {
                arena.push(Command {
                    rect: Rect {
                        x: rect.x + style.padding,
                        y: rect.y + rect.h - 2,
                        w: rect.w - 2 * style.padding,
                        h: 2,
                    },
                    ..Command::default()
                })?;
            }
            if selected && style.selection == 3 {
                arena.push(Command {
                    kind: Kind::Triangle as u8,
                    rect: Rect {
                        x: rect.x,
                        y: rect.y + rect.h / 2 - 9,
                        w: 12,
                        h: 18,
                    },
                    ..Command::default()
                })?;
            }
        }
        if !partial {
            rows.overlay(arena, index as u16, rect, style, generation)?;
        }
        y += row_height + gap;
        if partial {
            arena.push(Command {
                kind: Kind::Pop as u8,
                ..Command::default()
            })?;
            break;
        }
    }
    if drawn > 0 && (next.top > 0 || drawn < count) && style.scroll_width > 0 {
        let track = Rect {
            x: if style.scroll_left != 0 {
                style.area.x
            } else {
                style.area.x + style.area.w - style.scroll_width
            },
            w: style.scroll_width,
            ..style.area
        };
        arena.push(Command {
            rect: track,
            paint: 2,
            ..Command::default()
        })?;
        let thumb_height = (height * drawn / count).max(12);
        let range = count - drawn;
        let thumb_y = i32::from(track.y) + (height - thumb_height) * next.top.min(range) / range;
        arena.push(Command {
            rect: Rect {
                y: thumb_y as i16,
                h: thumb_height as i16,
                ..track
            },
            ..Command::default()
        })?;
    }
    if drawn > 0 {
        next.drawn = drawn;
        next.drawn_count = count;
    }
    if state.follow != 0
        && !selected_drawn
        && next.selected >= next.top
        && !(drawn == 0 && next.selected == next.top)
    {
        next.top = (next.selected - drawn.max(1) + 1)
            .max(next.top + 1)
            .min(next.selected);
        next.follow = 1;
    }
    Ok(next)
}
