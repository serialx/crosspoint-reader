use super::{
    paint,
    selection::{action_button, Layout, Painted, State, Word},
};
use crate::scene::{Arena, Command, Error, Kind, Rect};
pub const PAGE: u32 = 1;
pub const SELECTION: u32 = 2;
pub const FOOTER: u32 = 3;
pub const HEADER: u32 = 4;
pub const BODY: u32 = 5;
pub const MESSAGE: u32 = 6;
pub const LOOKUP: u32 = 7;
fn resource(arena: &mut Arena<'_>, handle: u32, r: Rect) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::Page as u8,
        resource: handle,
        generation: 1,
        rect: r,
        ..Command::default()
    })
}
fn fill(arena: &mut Arena<'_>, r: Rect, radius: u8, paint: u8) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::Fill as u8,
        rect: r,
        radius,
        paint,
        ..Command::default()
    })
}
pub fn selection(
    state: &State,
    words: &[Word],
    layout: &Layout,
    old: &Painted,
    arena: &mut Arena<'_>,
) -> Result<(Painted, u8), Error> {
    if !state.valid(words) {
        return Err(Error::Invalid);
    }
    let mode = paint::incremental(state, words, layout, old);
    let (x, y) = state.offsets(words, layout);
    let next = Painted {
        selected: state.selected,
        range_start: state.range_start,
        page: state.page,
        offset_x: x,
        offset_y: y,
        valid: 1,
        reserved: [0; 3],
    };
    if mode == 4 {
        return Ok((next, mode));
    }
    if mode == 0 {
        arena.push(Command {
            kind: Kind::Clear as u8,
            ..Command::default()
        })?;
        arena.push(Command {
            kind: Kind::Clip as u8,
            rect: layout.safe,
            ..Command::default()
        })?;
        resource(arena, PAGE, layout.screen)?;
    }
    if mode != 2 {
        arena.push(Command {
            kind: Kind::Page as u8,
            resource: SELECTION,
            generation: 1,
            rect: layout.screen,
            style: mode,
            ..Command::default()
        })?;
    }
    if mode == 0 && layout.touch != 0 && state.range_start >= 0 {
        let (first, last) = state.bounds();
        for (index, start) in [(first, true), (last, false)] {
            if i32::from(words[index].pageOffset) != state.page {
                continue;
            }
            let r = state.handle(words, layout, index, start);
            let radius = r.w / 2;
            fill(arena, r, radius as u8, 0)?;
            let left = start != (words[index].isRtl != 0);
            fill(
                arena,
                Rect {
                    x: r.x + if left { radius } else { 0 },
                    y: r.y,
                    w: radius,
                    h: radius,
                },
                0,
                0,
            )?;
        }
        if state.dragging == 0 {
            let count = if state.mode == 2 { 1 } else { 3 };
            let actions = state.actions(words, layout);
            for i in 0..count {
                let r = action_button(actions, layout.padding, count, i);
                fill(arena, r, layout.radius, 1)?;
                arena.push(Command {
                    kind: Kind::Stroke as u8,
                    rect: r,
                    radius: layout.radius,
                    width: 1,
                    ..Command::default()
                })?;
                arena.push(Command {
                    kind: Kind::TextHandle as u8,
                    rect: r,
                    resource: LOOKUP + i as u32,
                    generation: 1,
                    font: layout.action_font,
                    align: 1,
                    max_lines: 2,
                    ..Command::default()
                })?;
            }
        }
    }
    if mode == 0 {
        arena.push(Command {
            kind: Kind::Pop as u8,
            ..Command::default()
        })?;
    }
    resource(arena, FOOTER, layout.screen)?;
    Ok((next, mode))
}
