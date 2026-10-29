//! Transfer mode rows use the shared list renderer and static borrowed resources.
use super::usb_view::{FOOTER, HEADER};
use crate::settings::view::Layout;
use crate::{
    list,
    model::ListState,
    scene::{Arena, Command, Error, Kind, Rect},
    settings::view::{Content, Resources},
};
fn narrow(n: i32) -> Result<i16, Error> {
    i16::try_from(n).map_err(|_| Error::Invalid)
}
pub fn view(
    arena: &mut Arena<'_>,
    state: ListState,
    l: Layout,
    resources: &mut dyn Resources,
    count: u16,
) -> Result<ListState, Error> {
    if !(3..=4).contains(&count)
        || l.touch > 1
        || l.top_padding < 0
        || l.header_height < 0
        || l.hints_height < 0
        || l.spacing < 0
    {
        return Err(Error::Invalid);
    }
    let mut style = l.list;
    let screen = style.screen;
    let safe = style.area;
    let top = (i32::from(l.top_padding) + i32::from(l.header_height)).max(i32::from(safe.y))
        + i32::from(l.spacing);
    let bottom = (i32::from(screen.h) - i32::from(l.hints_height))
        .min(i32::from(safe.y) + i32::from(safe.h));
    style.area = Rect {
        x: safe.x,
        y: narrow(top)?,
        w: safe.w,
        h: narrow((bottom - top).max(0))?,
    };
    let padding = if l.touch != 0 { 8 } else { 4 };
    let line = resources.line(style.font)?;
    style.min_touch = narrow((line + 14).max(44))?;
    let mut minimum = (line + 2 * padding).max(i32::from(style.row_height));
    if l.touch != 0 {
        minimum = minimum.max(i32::from(style.min_touch)).max(56);
        style.row_gap = style.row_gap.max(6);
    }
    style.row_height = narrow(minimum)?;
    arena.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    arena.push(Command {
        kind: Kind::Page as u8,
        rect: Rect {
            x: 0,
            y: l.top_padding,
            w: screen.w,
            h: l.header_height,
        },
        resource: HEADER,
        generation: 1,
        ..Command::default()
    })?;
    let mut content = Content::new(resources, count, l.small_font, padding as i16, 1, 0);
    content.capacity = 4;
    content.subtitle_lines = 2;
    let next = list::view_rows(arena, state, style, count, 1, &mut content)?;
    arena.push(Command {
        kind: Kind::Page as u8,
        rect: screen,
        resource: FOOTER,
        generation: 1,
        ..Command::default()
    })?;
    Ok(next)
}
