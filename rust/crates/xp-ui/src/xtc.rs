use crate::{
    list,
    model::{ListState, Metrics},
    scene::{Arena, Command, Error, Kind, Rect},
};

pub const TITLE: u32 = 0x8000_0001;
pub const EMPTY: u32 = 0x8000_0002;
pub const FOOTER: u32 = 0x8000_0010;
pub trait Texts {
    fn text(&self, handle: u32) -> &[u8];
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub safe: Rect,
    pub list: list::Style,
    pub title_font: i32,
    pub hint_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub spacing: i16,
    pub footer_style: u8,
    pub touch: u8,
}
fn text(
    arena: &mut Arena<'_>,
    resource: u32,
    generation: u32,
    rect: Rect,
    font: i32,
    style: u8,
) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::TextHandle as u8,
        resource,
        generation,
        rect,
        font,
        style,
        ..Command::default()
    })
}

pub struct Book {
    pub count: u16,
    pub present: bool,
    pub generation: u32,
}

pub fn view(
    arena: &mut Arena<'_>,
    state: ListState,
    mut layout: Layout,
    book: Book,
    metrics: &mut dyn Metrics,
    texts: &dyn Texts,
) -> Result<ListState, Error> {
    let Book {
        count,
        present: has_book,
        generation,
    } = book;
    if layout.screen.x != 0
        || layout.screen.y != 0
        || !(1..=8192).contains(&layout.screen.w)
        || !(1..=8192).contains(&layout.screen.h)
        || layout.safe.x < 0
        || layout.safe.y < 0
        || layout.safe.w <= 0
        || layout.safe.h < 0
        || i32::from(layout.safe.x) + i32::from(layout.safe.w) > i32::from(layout.screen.w)
        || i32::from(layout.safe.y) + i32::from(layout.safe.h) > i32::from(layout.screen.h)
        || layout.top_padding < 0
        || layout.header_height < 0
        || layout.spacing < 0
        || i32::from(layout.top_padding)
            + i32::from(layout.header_height)
            + i32::from(layout.spacing)
            > i32::from(layout.safe.h)
        || layout.footer_style > 2
    {
        return Err(Error::Invalid);
    }
    let line = metrics.line_height(layout.list.font);
    if !(1..=8192).contains(&line) {
        return Err(Error::Invalid);
    }
    layout.list.min_touch = i16::try_from((line + 14).max(44)).map_err(|_| Error::Invalid)?;
    let padding = if layout.touch != 0 { 8 } else { 4 };
    layout.list.row_height = layout.list.row_height.max((line + 2 * padding) as i16);
    if layout.touch != 0 {
        layout.list.row_height = layout.list.row_height.max(layout.list.min_touch).max(56);
        layout.list.row_gap = layout.list.row_gap.max(6);
    }
    arena.push(Command {
        kind: Kind::Clear as u8,
        rect: layout.screen,
        paint: 1,
        ..Command::default()
    })?;
    let title_width = metrics.width(layout.title_font, texts.text(TITLE), 1);
    let title_height = metrics.line_height(layout.title_font);
    let title_x =
        i64::from(layout.safe.x) + (i64::from(layout.safe.w) - i64::from(title_width)) / 2;
    let title_y = i64::from(layout.safe.y)
        + i64::from(layout.top_padding)
        + (i64::from(layout.header_height) - i64::from(title_height)) / 2;
    text(
        arena,
        TITLE,
        generation,
        Rect {
            x: i16::try_from(title_x).map_err(|_| Error::Invalid)?,
            y: i16::try_from(title_y).map_err(|_| Error::Invalid)?,
            w: 0,
            h: 0,
        },
        layout.title_font,
        1,
    )?;
    let band = layout.top_padding + layout.header_height + layout.spacing;
    let area = Rect {
        x: layout.safe.x,
        y: layout.safe.y + band,
        w: layout.safe.w,
        h: (layout.safe.h - band).max(0),
    };
    let mut next = state;
    if has_book {
        if count == 0 {
            arena.push(Command {
                kind: Kind::TextHandle as u8,
                rect: area,
                resource: EMPTY,
                generation,
                font: layout.list.font,
                style: layout.list.bold,
                align: 1,
                max_lines: 1,
                ..Command::default()
            })?;
        } else {
            next = list::view(
                arena,
                state,
                list::Style {
                    area,
                    ..layout.list
                },
                count,
                generation,
            )?;
        }
    }
    if layout.touch == 0 {
        footer(arena, layout, generation, metrics, texts)?;
    }
    Ok(next)
}
fn footer(
    arena: &mut Arena<'_>,
    layout: Layout,
    generation: u32,
    metrics: &mut dyn Metrics,
    texts: &dyn Texts,
) -> Result<(), Error> {
    let w = layout.screen.w;
    let h = layout.screen.h;
    if layout.footer_style == 2 {
        let group = (w - 50) / 2;
        let y = h - 40;
        for x in [20, 20 + group + 10] {
            let rect = Rect {
                x,
                y,
                w: group,
                h: 30,
            };
            arena.push(Command {
                rect,
                paint: 1,
                ..Command::default()
            })?;
            arena.push(Command {
                kind: Kind::Stroke as u8,
                rect,
                width: 2,
                radius: 15,
                ..Command::default()
            })?;
        }
        let y = i16::try_from(
            i64::from(y) + (30 - i64::from(metrics.line_height(layout.hint_font))) / 2,
        )
        .map_err(|_| Error::Invalid)?;
        for i in 0..4 {
            let group_x = if i < 2 { 20 } else { 20 + group + 10 };
            let width = metrics.width(layout.hint_font, texts.text(FOOTER + i), 0);
            let x = if i % 2 == 0 {
                i64::from(group_x + 16)
            } else {
                i64::from(group_x + group - 16) - i64::from(width)
            };
            text(
                arena,
                FOOTER + i,
                generation,
                Rect {
                    x: i16::try_from(x).map_err(|_| Error::Invalid)?,
                    y,
                    w: 0,
                    h: 0,
                },
                layout.hint_font,
                0,
            )?;
        }
    } else {
        let lyra = layout.footer_style == 1;
        let positions = match (lyra, w >= 528) {
            (false, false) => [25, 130, 245, 350],
            (false, true) => [38, 154, 268, 384],
            (true, false) => [58, 146, 254, 342],
            (true, true) => [65, 157, 291, 383],
        };
        for (i, x) in positions.into_iter().enumerate() {
            let empty = texts.text(FOOTER + i as u32).is_empty();
            if !lyra && empty {
                continue;
            }
            let height = if lyra && empty { 15 } else { 40 };
            let rect = Rect {
                x,
                y: h - height,
                w: if lyra { 80 } else { 106 },
                h: height,
            };
            arena.push(Command {
                rect,
                paint: 1,
                radius: if lyra { 6 } else { 0 },
                ..Command::default()
            })?;
            arena.push(Command {
                kind: Kind::Stroke as u8,
                rect,
                width: 1,
                radius: if lyra { 6 } else { 0 },
                corners: if lyra { 3 } else { 15 },
                ..Command::default()
            })?;
            if !empty {
                arena.push(Command {
                    kind: Kind::HintText as u8,
                    rect,
                    resource: FOOTER + i as u32,
                    generation,
                    font: layout.hint_font,
                    ..Command::default()
                })?;
            }
        }
    }
    Ok(())
}
