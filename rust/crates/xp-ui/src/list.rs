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

pub fn view(
    arena: &mut Arena<'_>,
    state: ListState,
    style: Style,
    count: u16,
    generation: u32,
) -> Result<ListState, Error> {
    if style.screen.x != 0
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
    next.selected = next.selected.clamp(0, (count - 1).max(0));
    next.top = next.top.clamp(0, (count - 1).max(0));
    if count <= 0 {
        next.selected = 0;
        next.top = 0;
        return Ok(next);
    }
    let trusted = state.drawn > 0 && (state.drawn_count == 0 || state.drawn_count == count);
    if state.follow != 0 {
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
    let cut = (i32::from(style.scroll_width) + 2 - i32::from(style.inset)).max(0);
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
    for index in next.top..count {
        if y >= bottom {
            break;
        }
        let partial = y + row_height > bottom;
        if partial && bottom - y < 18 {
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
        let rect = Rect {
            x: row_x as i16,
            y: y as i16,
            w: row_width as i16,
            h: style.row_height,
        };
        let selected = !partial && (index == next.selected || index == state.active);
        let background = if selected {
            match style.selection {
                0 => 0,
                1 => 2,
                _ => 1,
            }
        } else {
            1
        };
        let foreground = u8::from(selected && style.selection == 0);
        arena.push(Command {
            rect,
            paint: background,
            radius: style.radius,
            ..Command::default()
        })?;
        arena.push(Command {
            kind: Kind::TextHandle as u8,
            rect: Rect {
                x: rect.x + style.padding,
                w: rect.w - 2 * style.padding,
                ..rect
            },
            resource: index as u32 + 1,
            generation,
            font: style.font,
            paint: foreground,
            style: style.bold,
            max_lines: 1,
            ..Command::default()
        })?;
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
                action: 1,
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
    Ok(next)
}
