use super::{
    bookmark_owned::Bookmarks,
    bookmark_window::{Toc, Window, ROWS},
    list::{self as drawing, Rows},
    physical::Physical,
};
use crate::app::{abi, bookmarks::Record, resources::Text};
use xp_core::i18n::ids::{
    STR_BACK, STR_BOOKMARKS, STR_DIR_DOWN, STR_DIR_UP, STR_HOLD_OPEN_FOR_ACTIONS, STR_NO_BOOKMARKS,
    STR_SELECT,
};
use xp_ui::{
    reader_chrome::{
        list,
        runtime::{PreparedList, RowSlot, ThemeMode},
    },
    scene::{Command, Error, Kind, Rect},
};

pub(super) const DELETE_ICON: u32 = 0x8000_0008;
// Final name storage lends its terminator along with the text, without copying.
pub(super) trait Label: AsRef<[u8]> {
    fn wire(&self) -> abi::Text;
}
impl Label for Text {
    fn wire(&self) -> abi::Text {
        Text::wire(self)
    }
}
pub(super) struct Details {
    pub(super) help: unsafe extern "C" fn(Rect, abi::Text),
    pub(super) icon: unsafe extern "C" fn(u8, Rect, Rect, u8),
}
impl<M: xp_ui::keyboard::owner::storage::Memory> Label
    for crate::app::bookmarks::keyboard_name::KeyboardName<M>
{
    fn wire(&self) -> abi::Text {
        abi::Text::from_terminated(self.terminated())
    }
}
struct BookmarkRows<'a, N> {
    services: &'a abi::Resources,
    details: &'a Details,
    window: &'a mut Window,
    records: &'a [Record<N>],
    toc: &'a mut dyn Toc,
}
impl<N: Label> Rows for BookmarkRows<'_, N> {
    fn text(&self, handle: u32) -> Option<abi::Text> {
        let key = match handle {
            list::HEADER | list::TITLE => Some(STR_BOOKMARKS),
            list::EMPTY => Some(STR_NO_BOOKMARKS),
            list::HELP => Some(STR_HOLD_OPEN_FOR_ACTIONS),
            _ => None,
        };
        if let Some(key) = key {
            return Some(unsafe { self.services.translate(key) });
        }
        let (index, slot) = RowSlot::decode(handle)?;
        self.window.subtitle(usize::from(index))?;
        let row = self.records.get(usize::from(index))?;
        match slot {
            RowSlot::Label => Some(if row.name.as_ref().is_empty() {
                row.summary.wire()
            } else {
                row.name.wire()
            }),
            RowSlot::Subtitle => self.window.subtitle(usize::from(index)).map(Text::wire),
            _ => None,
        }
    }
    fn valid(&self, handle: u32) -> bool {
        handle == DELETE_ICON
            || self.text(handle).is_some()
            || matches!(RowSlot::decode(handle), Some((index, RowSlot::Icon)) if self.window.subtitle(usize::from(index)).is_some())
    }
    fn header_list(&self) -> u8 {
        0
    }
    fn paint_special(&self, command: &Command, clip: Rect) -> bool {
        if command.resource == list::HELP {
            unsafe {
                (self.details.help)(
                    command.rect,
                    self.services.translate(STR_HOLD_OPEN_FOR_ACTIONS),
                )
            };
            return true;
        }
        if command.resource == list::FOOTER {
            let mut labels = [STR_BACK, STR_SELECT, STR_DIR_UP, STR_DIR_DOWN]
                .map(|key| unsafe { self.services.translate(key) });
            if self.records.is_empty() {
                labels[1] = abi::Text::from_terminated(b"\0");
            }
            unsafe { (self.services.footer_draw)(labels.as_ptr()) };
            return true;
        }
        if command.kind == Kind::Bitmap as u8 {
            unsafe {
                (self.details.icon)(
                    u8::from(command.resource == DELETE_ICON),
                    command.rect,
                    clip,
                    command.paint,
                )
            };
            return true;
        }
        false
    }
}
impl<N: Label> list::Resources for BookmarkRows<'_, N> {
    fn window(&mut self, first: u16, _: u16) -> Result<(), Error> {
        if self
            .window
            .refresh(self.records, usize::from(first), self.toc)
        {
            Ok(())
        } else {
            Err(Error::Capacity)
        }
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        self.window
            .subtitle(usize::from(index))
            .ok_or(Error::Invalid)?;
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
impl Bookmarks {
    /// Entry owns the reader resource guard before loading its shared records.
    pub(super) unsafe fn enter(&self, services: &'static abi::Resources) -> bool {
        unsafe { (*self.frame.get()).list.commands.recover(services) }
    }
    /// Exit excludes render and all popup callbacks before owner destruction.
    pub(super) unsafe fn exit(&self, close: impl FnOnce(*mut core::ffi::c_void)) {
        self.model.with(|model| model.ready = false);
        unsafe { (*self.frame.get()).list.commands.clear() };
        if let Some(handle) = unsafe { (*self.popup.get()).handle.take() } {
            close(handle.as_ptr());
        }
    }
    /// All records, TOC and text loans end before the caller's resource guard.
    #[inline(never)]
    pub(super) unsafe fn paint<N: Label>(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        details: &Details,
        records: &[Record<N>],
        toc: &mut dyn Toc,
    ) -> Result<PreparedList, Error> {
        let frame = unsafe { &mut *self.frame.get() };
        let Ok(count) = u16::try_from(records.len()) else {
            self.model.with(|model| model.painted(&frame.list, false));
            return Err(Error::Invalid);
        };
        let snapshot = self.model.with(|model| model.state);
        let sampled =
            unsafe { physical.sample_theme(services, &mut frame.list.theme, ThemeMode::Bookmarks) };
        let mut rows = BookmarkRows {
            services,
            details,
            window: &mut frame.window,
            records,
            toc,
        };
        let result = sampled.and_then(|()| unsafe {
            frame.list.paint(
                snapshot,
                list::Screen::Bookmarks,
                services,
                physical,
                &mut rows,
                count,
                ROWS as u16,
            )
        });
        self.model
            .with(|model| model.painted(&frame.list, result.is_ok()));
        result
    }
    #[cfg(any(test, feature = "app-test"))]
    pub(super) unsafe fn render<N: Label>(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        details: &Details,
        records: &[Record<N>],
        toc: &mut dyn Toc,
    ) -> Result<u16, Error> {
        let prepared = unsafe { self.paint(services, physical, details, records, toc) }?;
        unsafe { (services.refresh)(prepared.refresh as u8) };
        Ok(prepared.command_count)
    }
}
