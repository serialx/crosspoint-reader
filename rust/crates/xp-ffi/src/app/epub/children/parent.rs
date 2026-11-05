use super::{
    chapter_book::GuardedBook,
    chapter_view::ChapterBook,
    owned::{Toolbar, ToolbarRow},
    physical::Physical,
    toolbar_view::Content,
};
use crate::app::epub::{state::Overlay, EpubSession, ResourceAccess};
use xp_ui::scene::Error;

impl EpubSession {
    fn fill_owned_toolbar_row(
        &self,
        access: &ResourceAccess<'_>,
        mode: Overlay,
        index: i32,
        row: &mut ToolbarRow,
    ) -> bool {
        self.check_resources(access);
        if matches!(mode, Overlay::Contents) {
            row.value.clear();
            row.toggle = false;
            row.checked = false;
            return GuardedBook(access).label(index, &mut row.label);
        }
        let (name, value) = match mode {
            Overlay::Text => (
                self.text_row_name(access, index),
                self.text_row_value(access, index),
            ),
            Overlay::Page | Overlay::More => (
                self.more_row_name(access, index),
                self.more_row_value(access, index),
            ),
            Overlay::Toolbar | Overlay::Contents => return false,
        };
        if row.label.assign(name.bytes()).is_err() || row.value.assign(value.bytes()).is_err() {
            return false;
        }
        let checked = match mode {
            Overlay::Text if index == 4 => Some(self.settings(false).focus_reading != 0),
            Overlay::Page | Overlay::More => match self.more_action(access, index) {
                Some(3) => Some(self.settings(false).screen_inverted != 0),
                Some(5) => Some(self.settings(false).ble_enabled != 0),
                Some(4) => Some(unsafe { (self.runtime.frontlight_state)() } != 0),
                _ => None,
            },
            _ => None,
        };
        row.toggle = checked.is_some();
        row.checked = checked.unwrap_or(false);
        true
    }

    pub(super) unsafe fn paint_owned_toolbar(
        &self,
        access: &ResourceAccess<'_>,
        toolbar: &Toolbar,
        physical: &Physical,
        mode: Overlay,
        content: &Content<'_>,
    ) -> Result<u16, Error> {
        self.check_resources(access);
        unsafe {
            toolbar.render(self.resources, physical, content, &mut |index, row| {
                self.fill_owned_toolbar_row(access, mode, index, row)
            })
        }
    }
}
