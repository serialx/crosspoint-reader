use super::{
    list::{self as drawing, Rows},
    menu::Items,
    owned::Menu,
    physical::Physical,
};
use crate::app::{abi, resources::Text};
use xp_core::i18n::ids::*;
use xp_ui::{
    reader::menu_parent::Action,
    reader_chrome::{list, menu},
    scene::{Error, Refresh},
};

#[derive(Clone, Copy)]
pub(super) struct Toggles {
    pub(super) night: bool,
    pub(super) bluetooth: bool,
    pub(super) frontlight: bool,
}
struct MenuRows<'a> {
    services: &'a abi::Resources,
    items: &'a Items,
    title: &'a Text,
    progress: &'a Text,
    state: menu::State,
    toggles: Toggles,
}
impl Rows for MenuRows<'_> {
    fn text(&self, handle: u32) -> Option<abi::Text> {
        match handle {
            list::HEADER => Some(self.title.wire()),
            list::PROGRESS => Some(self.progress.wire()),
            list::EMPTY => Some(abi::Text::from_terminated(b"\0")),
            1..list::HEADER => {
                let index = ((handle - 1) / 4) as usize;
                let action = self.items.actions().get(index)?;
                match (handle - 1) % 4 {
                    0 => Some(unsafe { (self.services.translate)(self.items.label(index)?) }),
                    2 => match action {
                        Action::Orientation => [
                            STR_PORTRAIT,
                            STR_LANDSCAPE_CW,
                            STR_INVERTED,
                            STR_LANDSCAPE_CCW,
                        ]
                        .get(usize::from(self.state.orientation))
                        .map(|key| unsafe { (self.services.translate)(*key) }),
                        Action::Auto => match self.state.page_turn {
                            0 => Some(unsafe { (self.services.translate)(STR_STATE_OFF) }),
                            1 => Some(abi::Text::from_terminated(b"1\0")),
                            2 => Some(abi::Text::from_terminated(b"3\0")),
                            3 => Some(abi::Text::from_terminated(b"6\0")),
                            4 => Some(abi::Text::from_terminated(b"12\0")),
                            _ => None,
                        },
                        _ => None,
                    },
                    _ => None,
                }
            }
            _ => None,
        }
    }
}
impl list::Resources for MenuRows<'_> {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        let action = self
            .items
            .actions()
            .get(usize::from(index))
            .ok_or(Error::Invalid)?;
        let base = u32::from(index) * 4 + 1;
        let checked = match action {
            Action::Night => Some(self.toggles.night),
            Action::Bluetooth => Some(self.toggles.bluetooth),
            Action::Frontlight => Some(self.toggles.frontlight),
            _ => None,
        };
        Ok(list::Row {
            label: base,
            value: if matches!(action, Action::Auto | Action::Orientation) {
                base + 2
            } else {
                0
            },
            toggle: u8::from(checked.is_some()),
            checked: u8::from(checked.unwrap_or(false)),
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
impl Menu {
    /// Caller holds the render lifetime lock and parent's resource guard.
    pub(super) unsafe fn render(
        &self,
        services: &'static abi::Resources,
        physical: &Physical,
        toggles: Toggles,
    ) -> Result<u16, Error> {
        let (snapshot, menu) = self.model.with(|model| (model.list.state, model.menu));
        let frame = unsafe { &mut *self.frame.get() };
        frame.menu = menu;
        unsafe { (physical.list_theme)(&mut frame.list.theme, 0) };
        let mut rows = MenuRows {
            services,
            items: &self.items,
            title: &self.title,
            progress: &self.progress,
            state: frame.menu,
            toggles,
        };
        let result = unsafe {
            frame.list.paint(
                snapshot,
                list::Screen::Menu,
                services,
                physical,
                &mut rows,
                self.items.actions().len() as u16,
                19,
            )
        };
        self.model
            .with(|model| model.list.painted(&frame.list, result.is_ok()));
        if result.is_ok() {
            unsafe { (services.refresh)(Refresh::Fast as u8) };
        }
        result
    }
}
