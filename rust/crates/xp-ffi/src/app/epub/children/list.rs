use super::{
    owned::{ListFrame, ListPublication},
    physical::Physical,
};
use crate::app::abi;
use core::ffi::c_void;
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::{self, hits, list},
    scene::{Arena, Command, Error, Rect, Refresh},
};

pub(super) trait Rows: list::Resources {
    fn text(&self, handle: u32) -> Option<abi::Text>;
}
struct Drawing<'a> {
    rows: &'a mut dyn Rows,
    services: &'a abi::Resources,
    physical: &'a Physical,
}
impl Drawing<'_> {
    unsafe extern "C" fn valid(context: *mut c_void, handle: u32, generation: u32) -> u8 {
        let this = unsafe { &*context.cast::<Self>() };
        u8::from(generation == 1 && (handle == list::FOOTER || this.rows.text(handle).is_some()))
    }
    unsafe extern "C" fn paint(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        let command = unsafe { &*command };
        match command.resource {
            list::HEADER => {
                if let Some(title) = this.rows.text(list::HEADER) {
                    unsafe { (this.physical.header)(command.rect, title, 1) };
                }
            }
            list::FOOTER => {
                let labels = [STR_BACK, STR_SELECT, STR_DIR_UP, STR_DIR_DOWN]
                    .map(|key| unsafe { (this.services.translate)(key) });
                unsafe { (this.services.footer_draw)(labels.as_ptr()) };
            }
            handle => {
                if let Some(text) = this.rows.text(handle) {
                    unsafe { (this.services.text_draw)(command, clip, text) };
                }
            }
        }
    }
}
impl ListFrame {
    /// Exclusive render scratch; rows and callbacks remain borrowed for this paint.
    #[allow(clippy::too_many_arguments)]
    pub(super) unsafe fn paint(
        &mut self,
        snapshot: reader_chrome::ListModel,
        kind: list::Screen,
        services: &'static abi::Resources,
        physical: &Physical,
        rows: &mut dyn Rows,
        count: u16,
        capacity: u16,
    ) -> Result<u16, Error> {
        self.consumed_scroll = snapshot.list.pending_scroll;
        if !self.commands.recover(services) {
            return Err(Error::Capacity);
        }
        'capacity: loop {
            self.next = snapshot;
            for pass in 0..9 {
                self.next_hits = hits::Layout::new(1, 1);
                let mut arena =
                    Arena::with_hit_sink(self.commands.as_mut_slice(), &mut self.next_hits)?;
                let result = list::view(
                    &mut arena,
                    self.next.list,
                    kind,
                    self.theme,
                    rows,
                    count,
                    capacity,
                    1,
                    self.next.revealed,
                );
                self.next.list = match result {
                    Ok(next) => next,
                    Err(Error::Capacity) if self.commands.grow(services) => continue 'capacity,
                    Err(error) => return Err(error),
                };
                if self.next.list.follow != 0 && pass < 8 && kind == list::Screen::Chapters {
                    continue;
                }
                let scene = arena.finish(Refresh::Fast, 1)?;
                let view = SceneView {
                    commands: scene.commands.as_ptr(),
                    text: core::ptr::null(),
                    interactions: core::ptr::null(),
                    generation: 1,
                    command_count: scene.commands.len() as u16,
                    text_len: 0,
                    interaction_count: 0,
                    refresh: Refresh::None as u8,
                    reserved: 0,
                };
                let mut drawing = Drawing {
                    rows,
                    services,
                    physical,
                };
                let paint = Paint {
                    context: (&mut drawing as *mut Drawing<'_>).cast(),
                    clear: None,
                    text: None,
                    resource_valid: Some(Drawing::valid),
                    resource: Some(Drawing::paint),
                };
                if unsafe { (services.paint_scene)(&view, &paint) } == 0 {
                    return Err(Error::Invalid);
                }
                return Ok(view.command_count);
            }
            return Err(Error::Invalid);
        }
    }
}
impl ListPublication {
    pub(super) fn painted(&mut self, frame: &ListFrame, success: bool) {
        self.ready = success;
        self.failed = !success;
        if success {
            self.state
                .list
                .publish(&frame.next.list, frame.consumed_scroll);
            self.hits = frame.next_hits;
        } else {
            self.hits = hits::Layout::default();
            self.state.list.last_repeat = 0;
        }
    }
}
pub(super) fn width(
    services: &abi::Resources,
    text: Option<abi::Text>,
    font: i32,
    style: u8,
) -> Result<i32, Error> {
    let value = unsafe { (services.text_width)(text.ok_or(Error::Invalid)?, font, style) };
    if value < 0 {
        Err(Error::Invalid)
    } else {
        Ok(value)
    }
}
pub(super) fn height(
    services: &abi::Resources,
    text: Option<abi::Text>,
    font: i32,
    style: u8,
    lines: u8,
    width: i16,
) -> Result<i32, Error> {
    let value = unsafe {
        (services.text_box_height)(
            text.ok_or(Error::Invalid)?,
            abi::TextLayout {
                font,
                style,
                lines,
                width: i32::from(width),
            },
        )
    };
    if value < 0 {
        Err(Error::Invalid)
    } else {
        Ok(value)
    }
}
pub(super) fn line(services: &abi::Resources, font: i32) -> Result<i32, Error> {
    let value = unsafe { (services.line_height)(font) };
    if value < 0 {
        Err(Error::Invalid)
    } else {
        Ok(value)
    }
}
