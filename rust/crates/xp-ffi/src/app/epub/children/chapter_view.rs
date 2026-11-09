use super::{
    chapters::{Window, ROWS},
    list::{self as drawing, Rows},
    owned::Chapters,
    physical::Physical,
};
use crate::app::{abi, resources::Text};
use core::ffi::c_void;
use xp_core::i18n::ids::{STR_NO_CHAPTERS, STR_SELECT_CHAPTER};
use xp_ui::{
    reader_chrome::{
        list,
        runtime::{RowSlot, ThemeMode},
    },
    scene::Error,
};

// The production implementation borrows the retained session's resource guard.
// False means label storage failed; TOC read failures use the book's normal fallback.
pub(super) trait ChapterBook {
    fn count(&self) -> Option<i32>;
    fn label(&mut self, index: i32, text: &mut Text) -> bool;
}
struct ChapterRows<'a> {
    services: &'a abi::Resources,
    physical: &'a Physical,
    book: &'a mut dyn ChapterBook,
    window: &'a mut Window,
    total: i32,
    present: bool,
    font: i32,
}
unsafe extern "C" fn prewarm_label(context: *const c_void, index: u32) -> abi::Text {
    let window = unsafe { &*context.cast::<Window>() };
    window
        .labels()
        .get(index as usize)
        .map_or(abi::Text::from_terminated(b"\0"), Text::wire)
}
impl Rows for ChapterRows<'_> {
    fn text(&self, handle: u32) -> Option<abi::Text> {
        match handle {
            list::HEADER => Some(unsafe { (self.services.translate)(STR_SELECT_CHAPTER) }),
            list::EMPTY => Some(if self.present {
                unsafe { (self.services.translate)(STR_NO_CHAPTERS) }
            } else {
                abi::Text::from_terminated(b"\0")
            }),
            1..list::HEADER => {
                let (index, slot) = RowSlot::decode(handle)?;
                if slot != RowSlot::Label {
                    return None;
                }
                self.window.label(i32::from(index)).map(Text::wire)
            }
            _ => None,
        }
    }
}
impl list::Resources for ChapterRows<'_> {
    fn window(&mut self, first: u16, _: u16) -> Result<(), Error> {
        let before = (self.window.first(), self.window.labels().len());
        if !self
            .window
            .refresh(self.total, i32::from(first), |index, text| {
                self.book.label(index, text)
            })
        {
            return Err(Error::Invalid);
        }
        if before != (self.window.first(), self.window.labels().len()) {
            unsafe {
                (self.physical.prewarm_rows)(
                    self.font,
                    (self.window as *const Window).cast(),
                    prewarm_label,
                    self.window.labels().len() as u32,
                )
            };
        }
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        self.window.label(i32::from(index)).ok_or(Error::Invalid)?;
        Ok(list::Row {
            label: RowSlot::Label.encode(index),
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
impl Chapters {
    /// Entry has exclusive ownership under the parent's resource guard.
    pub(super) unsafe fn enter(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
    ) -> bool {
        let frame = unsafe { &mut *self.frame.get() };
        if !frame.list.commands.recover(services) {
            return false;
        }
        unsafe { physical.clear_font_cache() };
        true
    }
    /// Caller holds the render lifetime lock and the book's resource guard.
    #[inline(never)]
    pub(super) unsafe fn render(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        book: &mut dyn ChapterBook,
    ) -> Result<u16, Error> {
        let frame = unsafe { &mut *self.frame.get() };
        let total = book.count();
        let Some(count) = u16::try_from(total.unwrap_or(0)).ok() else {
            self.model.with(|model| model.painted(&frame.list, false));
            return Err(Error::Invalid);
        };
        let snapshot = self.model.with(|model| model.state);
        let sampled = unsafe { physical.sample_theme(&mut frame.list.theme, ThemeMode::List) };
        let mut rows = ChapterRows {
            services,
            physical,
            book,
            window: &mut frame.window,
            total: i32::from(count),
            present: total.is_some(),
            font: frame.list.theme.body_font,
        };
        let result = sampled.and_then(|()| unsafe {
            frame.list.paint(
                snapshot,
                list::Screen::Chapters,
                services,
                physical,
                &mut rows,
                count,
                ROWS as u16,
            )
        });
        self.model
            .with(|model| model.painted(&frame.list, result.is_ok()));
        if let Ok(prepared) = result {
            unsafe { (services.refresh)(prepared.refresh as u8) };
        }
        result.map(|prepared| prepared.command_count)
    }
}
