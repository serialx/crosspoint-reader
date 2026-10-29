use crate::scene::{Arena, Command, Error, Kind, Rect};

#[inline(never)]
pub fn resource(arena: &mut Arena<'_>, handle: u32, rect: Rect) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::Bitmap as u8,
        rect,
        resource: handle,
        generation: 1,
        ..Command::default()
    })
}
