use super::{
    list as drawing,
    owned::{Toolbar, ToolbarFrame, ToolbarRow},
    physical::Physical,
};
use crate::app::abi;
use core::{
    ffi::{c_void, CStr},
    fmt::Write,
};
use xp_core::glyph::GlyphTarget;
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::{hits, list, toolbar},
    scene::{Arena, Command, Error, Rect, Refresh},
};

// Borrowed Rust render inputs, never retained by the toolbar or sent through C.
pub(super) struct Content<'a> {
    pub(super) panel: bool,
    pub(super) dense: bool,
    pub(super) active_tool: i32,
    pub(super) progress_permille: i32,
    pub(super) chapter: Option<&'a CStr>,
    pub(super) page_info: Option<&'a CStr>,
    pub(super) panel_title: Option<&'a CStr>,
    pub(super) count: i32,
    pub(super) selected: i32,
}
struct Rows<'a, 'b> {
    services: &'a abi::Resources,
    content: &'a Content<'b>,
    fill: &'a mut dyn FnMut(i32, &mut ToolbarRow) -> bool,
    rows: &'a mut [ToolbarRow; 16],
    first: &'a mut u16,
    count: &'a mut u16,
    indicator: &'a mut [u8; 16],
}
impl Rows<'_, '_> {
    fn text(&self, handle: u32) -> Option<abi::Text> {
        let label = match handle {
            list::TITLE => self.content.chapter,
            list::PROGRESS => self.content.page_info,
            list::HEADER => self.content.panel_title,
            toolbar::PAGE => {
                return Some(abi::Text::from_terminated(
                    &self.indicator[..=self.indicator.iter().position(|b| *b == 0)?],
                ))
            }
            1..list::HEADER => {
                let index = u16::try_from((handle - 1) / 2).ok()?;
                let row = self.row_ref(index).ok()?;
                return Some(if (handle - 1).is_multiple_of(2) {
                    row.label.wire()
                } else {
                    row.value.wire()
                });
            }
            _ => None,
        }?;
        Some(abi::Text::from_terminated(label.to_bytes_with_nul()))
    }
    fn row_ref(&self, index: u16) -> Result<&ToolbarRow, Error> {
        let offset = index
            .checked_sub(*self.first)
            .filter(|offset| *offset < *self.count)
            .ok_or(Error::Invalid)?;
        Ok(&self.rows[usize::from(offset)])
    }
}
impl list::Resources for Rows<'_, '_> {
    fn window(&mut self, first: u16, count: u16) -> Result<(), Error> {
        *self.count = 0;
        if usize::from(count) > self.rows.len()
            || i32::from(first) + i32::from(count) > self.content.count
        {
            return Err(Error::Invalid);
        }
        for index in 0..usize::from(count) {
            if !(self.fill)(i32::from(first) + index as i32, &mut self.rows[index]) {
                return Err(Error::Invalid);
            }
        }
        *self.first = first;
        *self.count = count;
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        let row = self.row_ref(index)?;
        let base = u32::from(index) * 2 + 1;
        Ok(list::Row {
            label: base,
            value: if row.value.is_empty() { 0 } else { base + 1 },
            toggle: u8::from(row.toggle),
            checked: u8::from(row.checked),
            ..Default::default()
        })
    }
    fn width(&mut self, handle: u32, font: i32, style: u8) -> Result<i32, Error> {
        drawing::width(self.services, self.text(handle), font, style)
    }
    fn height(
        &mut self,
        handle: u32,
        font: i32,
        style: u8,
        lines: u8,
        width: i16,
    ) -> Result<i32, Error> {
        drawing::height(self.services, self.text(handle), font, style, lines, width)
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        drawing::line(self.services, font)
    }
}
struct Drawing<'a, 'b, 'c> {
    rows: &'a Rows<'b, 'c>,
    physical: &'a Physical,
}
impl Drawing<'_, '_, '_> {
    unsafe extern "C" fn valid(context: *mut c_void, handle: u32, generation: u32) -> u8 {
        let this = unsafe { &*context.cast::<Self>() };
        u8::from(
            generation == 1
                && ((toolbar::CONTENTS..=toolbar::NEXT).contains(&handle)
                    || this.rows.text(handle).is_some()),
        )
    }
    unsafe extern "C" fn paint(
        context: *mut c_void,
        _: *const GlyphTarget,
        command: *const Command,
        clip: Rect,
    ) {
        let this = unsafe { &*context.cast::<Self>() };
        let command = unsafe { &*command };
        if command.kind == xp_ui::scene::Kind::Bitmap as u8 {
            unsafe { (this.physical.toolbar_icon)(command.resource, command.rect, clip) };
        } else if let Some(text) = this.rows.text(command.resource) {
            unsafe { (this.rows.services.text_draw)(command, clip, text) };
        }
    }
}
struct Indicator<'a> {
    bytes: &'a mut [u8; 16],
    used: usize,
}
impl Write for Indicator<'_> {
    fn write_str(&mut self, text: &str) -> core::fmt::Result {
        let end = self.used + text.len();
        if end >= self.bytes.len() {
            return Err(core::fmt::Error);
        }
        self.bytes[self.used..end].copy_from_slice(text.as_bytes());
        self.bytes[end] = 0;
        self.used = end;
        Ok(())
    }
}
impl Toolbar {
    /// Caller holds the render lifetime lock and the parent's resource guard.
    pub(super) unsafe fn render(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        content: &Content<'_>,
        fill: &mut dyn FnMut(i32, &mut ToolbarRow) -> bool,
    ) -> Result<u16, Error> {
        let result = unsafe { self.paint(services, physical, content, fill) };
        if result.is_err() {
            self.model.with(|model| {
                model.ready = false;
                model.contact = Default::default();
            });
        }
        result
    }
    unsafe fn paint(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        content: &Content<'_>,
        fill: &mut dyn FnMut(i32, &mut ToolbarRow) -> bool,
    ) -> Result<u16, Error> {
        let count = u16::try_from(content.count.max(0)).map_err(|_| Error::Invalid)?;
        let mut state = self.model.with(|model| model.navigation);
        state.selected = content
            .selected
            .clamp(-1, (content.count.max(0) - 1).max(-1));
        state.active = -1;
        state.follow = u8::from(state.selected >= 0);
        let frame = unsafe { &mut *self.frame.get() };
        let ToolbarFrame {
            commands,
            theme,
            rows,
            first,
            count: window_count,
            page_indicator,
            next,
            next_hits,
        } = frame;
        unsafe { (physical.list_theme)(theme, 1) };
        if !commands.recover(services) {
            return Err(Error::Capacity);
        }
        let model = toolbar::Model {
            panel: u8::from(content.panel),
            dense_rows: u8::from(content.dense),
            active_tool: content.active_tool,
            progress_permille: content.progress_permille,
            chapter_title: if content.chapter.is_some() {
                list::TITLE
            } else {
                0
            },
            page_info: if content.page_info.is_some() {
                list::PROGRESS
            } else {
                0
            },
            panel_title: if content.panel_title.is_some() {
                list::HEADER
            } else {
                0
            },
            ..Default::default()
        };
        let mut rows = Rows {
            services,
            content,
            fill,
            rows,
            first,
            count: window_count,
            indicator: page_indicator,
        };
        'capacity: loop {
            *next = state;
            for pass in 0..4 {
                *next_hits = hits::Layout::new(6, 1);
                let mut arena = Arena::with_hit_sink(commands.as_mut_slice(), next_hits)?;
                *next = match toolbar::view(
                    &mut arena, *next, model, *theme, &mut rows, count, 16, 1,
                ) {
                    Ok(next) => next,
                    Err(Error::Capacity) if commands.grow(services) => continue 'capacity,
                    Err(error) => return Err(error),
                };
                if next.follow != 0 && content.panel && pass < 3 {
                    continue;
                }
                let scene = arena.finish(Refresh::None, 1)?;
                let page_rows = if next.drawn > 0 {
                    next.drawn
                } else {
                    next.visible
                }
                .max(1);
                rows.indicator.fill(0);
                let mut indicator = Indicator {
                    bytes: rows.indicator,
                    used: 0,
                };
                write!(
                    indicator,
                    "{}/{}",
                    next.top / page_rows + 1,
                    (content.count.max(0) + page_rows - 1) / page_rows
                )
                .map_err(|_| Error::Invalid)?;
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
                    rows: &rows,
                    physical,
                };
                let paint = Paint {
                    context: (&mut drawing as *mut Drawing<'_, '_, '_>).cast(),
                    clear: None,
                    text: None,
                    resource_valid: Some(Drawing::valid),
                    resource: Some(Drawing::paint),
                };
                if unsafe { (services.paint_scene)(&view, &paint) } == 0 {
                    return Err(Error::Invalid);
                }
                self.publish(next, *next_hits);
                return Ok(view.command_count);
            }
            return Err(Error::Invalid);
        }
    }
}
