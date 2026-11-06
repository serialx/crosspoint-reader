use super::{
    clipping_window::{Row, Window, ROWS},
    list::{self as drawing, Rows},
    owned::Clippings,
    physical::Physical,
};
use crate::app::abi;
use xp_core::i18n::ids::{
    STR_CLIPPINGS, STR_HOLD_SELECT_TO_DELETE, STR_LOADING_POPUP, STR_NO_CLIPPINGS,
};
use xp_ui::{
    ffi::{Paint, SceneView},
    reader_chrome::{
        list,
        runtime::{RowSlot, ThemeMode},
    },
    scene::{Command, Error, Kind, Rect, Refresh},
};

pub(super) struct Details {
    pub(super) help: unsafe extern "C" fn(Rect, abi::Text),
    pub(super) bookmark: unsafe extern "C" fn(Rect, Rect, u8),
}
struct ClippingRows<'a> {
    services: &'a abi::Resources,
    details: &'a Details,
    window: &'a mut Window,
    total: u16,
    fill: &'a mut dyn FnMut(i32, &mut Row),
}
impl ClippingRows<'_> {
    fn row_for(&self, handle: u32) -> Option<&Row> {
        if !(1..list::HEADER).contains(&handle) {
            return None;
        }
        let (index, _) = RowSlot::decode(handle)?;
        self.window.row(i32::from(index))
    }
}
impl Rows for ClippingRows<'_> {
    fn text(&self, handle: u32) -> Option<abi::Text> {
        let key = match handle {
            list::HEADER => Some(STR_CLIPPINGS),
            list::EMPTY => Some(STR_NO_CLIPPINGS),
            list::HELP => Some(STR_HOLD_SELECT_TO_DELETE),
            _ => None,
        };
        if let Some(key) = key {
            return Some(unsafe { (self.services.translate)(key) });
        }
        let row = self.row_for(handle)?;
        match RowSlot::decode(handle)?.1 {
            RowSlot::Label => Some(if row.chapter[0] == 0 {
                unsafe { (self.services.translate)(STR_CLIPPINGS) }
            } else {
                let end = row.chapter.iter().position(|byte| *byte == 0)?;
                abi::Text::from_terminated(&row.chapter[..=end])
            }),
            RowSlot::Subtitle => Some(abi::Text::from_terminated(
                &row.preview[..=row.preview_length],
            )),
            _ => None,
        }
    }
    fn valid(&self, handle: u32) -> bool {
        self.text(handle).is_some()
            || self.row_for(handle).is_some()
                && matches!(RowSlot::decode(handle), Some((_, RowSlot::Icon)))
    }
    fn header_list(&self) -> u8 {
        0
    }
    fn paint_special(&self, command: &Command, clip: Rect) -> bool {
        if command.resource == list::HELP {
            unsafe {
                (self.details.help)(
                    command.rect,
                    (self.services.translate)(STR_HOLD_SELECT_TO_DELETE),
                )
            };
            return true;
        }
        if command.kind == Kind::Bitmap as u8 {
            unsafe { (self.details.bookmark)(command.rect, clip, command.paint) };
            return true;
        }
        false
    }
}
impl list::Resources for ClippingRows<'_> {
    fn window(&mut self, first: u16, _: u16) -> Result<(), Error> {
        self.window
            .refresh(i32::from(self.total), i32::from(first), &mut self.fill);
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        self.window.row(i32::from(index)).ok_or(Error::Invalid)?;
        Ok(list::Row {
            label: RowSlot::Label.encode(index),
            subtitle: RowSlot::Subtitle.encode(index),
            icon: RowSlot::Icon.encode(index),
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

// Keep this transient clear scene off the list's render frame.
#[inline(never)]
unsafe fn loading(services: &abi::Resources, screen: Rect) -> Result<(), Error> {
    let command = Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Default::default()
    };
    let scene = SceneView {
        commands: &command,
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: 1,
        text_len: 0,
        interaction_count: 0,
        refresh: Refresh::None as u8,
        reserved: 0,
    };
    let paint = Paint {
        context: core::ptr::null_mut(),
        clear: None,
        text: None,
        resource_valid: None,
        resource: None,
    };
    if unsafe { (services.paint_scene)(&scene, &paint) } == 0 {
        return Err(Error::Invalid);
    }
    unsafe { (services.popup_draw)((services.translate)(STR_LOADING_POPUP)) };
    Ok(())
}
impl Clippings {
    /// The caller holds the render lifetime lock and a store operation lease.
    #[inline(never)]
    pub(super) unsafe fn render(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        details: &Details,
        count: u16,
        fill: &mut dyn FnMut(i32, &mut Row),
    ) -> Result<u16, Error> {
        let frame = unsafe { &mut *self.frame.get() };
        let snapshot = self.model.with(|model| model.state);
        if let Err(error) = unsafe { physical.sample_theme(&mut frame.list.theme, ThemeMode::List) }
        {
            self.model.with(|model| model.painted(&frame.list, false));
            return Err(error);
        }
        if frame.initial && count > 0 {
            if let Err(error) = unsafe { loading(services, frame.list.theme.screen) } {
                self.model.with(|model| model.painted(&frame.list, false));
                return Err(error);
            }
        }
        frame.initial = false;
        let mut rows = ClippingRows {
            services,
            details,
            window: &mut frame.window,
            total: count,
            fill,
        };
        let result = unsafe {
            frame.list.paint(
                snapshot,
                list::Screen::Clippings,
                services,
                physical,
                &mut rows,
                count,
                ROWS as u16,
            )
        };
        self.model
            .with(|model| model.painted(&frame.list, result.is_ok()));
        if let Ok(prepared) = result {
            unsafe { (services.refresh)(prepared.refresh as u8) };
        }
        result.map(|prepared| prepared.command_count)
    }
}
