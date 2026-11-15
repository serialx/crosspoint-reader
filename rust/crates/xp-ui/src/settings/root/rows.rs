use super::{Host, Resources};
use crate::{
    reader_chrome::runtime::RowSlot,
    scene::{Command, Error, Rect},
    settings::view,
};
use core::ffi::CStr;
use xp_core::i18n::ids::*;

pub(super) const TITLES: [u16; 6] = [
    STR_SETTINGS_GENERAL,
    STR_CAT_DISPLAY,
    STR_CAT_READER,
    STR_CAT_CONTROLS,
    STR_LIBRARY,
    STR_SETTINGS_NETWORK,
];
pub(super) struct Rows<'a, H: Host> {
    pub host: &'a H,
    pub labels: &'a [&'static CStr; 6],
}
impl<H: Host> Rows<'_, H> {
    fn label(&self, handle: u32) -> Result<&CStr, Error> {
        let Some((index, RowSlot::Label)) = RowSlot::decode(handle) else {
            return Err(Error::Invalid);
        };
        self.labels
            .get(usize::from(index))
            .copied()
            .ok_or(Error::Invalid)
    }
}
impl<H: Host> view::Resources for Rows<'_, H> {
    fn window(&mut self, first: u16, _: u16) -> Result<(), Error> {
        if first < 6 {
            Ok(())
        } else {
            Err(Error::Invalid)
        }
    }
    fn row(&mut self, index: u16) -> Result<view::Row, Error> {
        if index >= 6 {
            return Err(Error::Invalid);
        }
        Ok(view::Row {
            label: RowSlot::Label.encode(index),
            icon: RowSlot::Icon.encode(index),
            enabled: 1,
            ..view::Row::default()
        })
    }
    fn width(&mut self, h: u32, font: i32, bold: u8) -> Result<i32, Error> {
        metric(self.host.width(self.label(h)?, font, bold))
    }
    fn height(&mut self, h: u32, font: i32, bold: u8, lines: u8, width: i16) -> Result<i32, Error> {
        metric(self.host.height(self.label(h)?, font, bold, lines, width))
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        metric(self.host.line(font))
    }
}
fn metric(value: i32) -> Result<i32, Error> {
    if (0..=i16::MAX.into()).contains(&value) {
        Ok(value)
    } else {
        Err(Error::Invalid)
    }
}
impl<H: Host> Resources for Rows<'_, H> {
    fn valid(&self, handle: u32, generation: u32) -> bool {
        generation == 1
            && (matches!(handle, view::HEADER | view::FOOTER)
                || matches!(
                    RowSlot::decode(handle),
                    Some((0..=5, RowSlot::Label | RowSlot::Icon))
                ))
    }
    fn paint(&mut self, command: &Command, clip: Rect) {
        match command.resource {
            view::HEADER => {
                self.host
                    .chrome(command, clip, self.host.translate(STR_SETTINGS_TITLE), 0)
            }
            view::FOOTER => self.host.footer(),
            handle => match RowSlot::decode(handle) {
                Some((i @ 0..=5, RowSlot::Icon)) => {
                    self.host.chrome(command, clip, c"", i as u8 + 1)
                }
                Some((i @ 0..=5, RowSlot::Label)) => {
                    self.host.text(command, clip, self.labels[usize::from(i)])
                }
                _ => {}
            },
        }
    }
}
