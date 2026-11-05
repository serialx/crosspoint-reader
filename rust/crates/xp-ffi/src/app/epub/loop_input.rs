use super::{book::with_book_text, projection::RemoteShortcut, state::Overlay, EpubSession, Text};
use xp_ui::reader::input;

impl EpubSession {
    pub(super) fn book_touch_turn(&self) -> input::Turn {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.base_hooks {
            return unsafe { (hooks.touch)(hooks.context) };
        }
        let rtl = self
            .loop_book()
            .and_then(|book| {
                with_book_text(book, super::session_book::language, |language| {
                    input::language_rtl(unsafe { language.bytes() }.unwrap_or_default())
                })
            })
            .unwrap_or(false);
        let geometry = self.overlay_geometry();
        unsafe {
            input::touch(
                *(self.leaves.input_services)(),
                self.input_config(),
                geometry.width,
                geometry.height,
                rtl,
            )
        }
    }

    pub(super) fn touch_menu_gesture(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return unsafe { (hooks.touch_menu)(hooks.context) != 0 };
        }
        let geometry = self.overlay_geometry();
        unsafe {
            input::menu(
                *(self.leaves.input_services)(),
                self.settings(false).show_menu,
                geometry.width,
                geometry.height,
            )
        }
    }

    pub(super) fn screen_point(&self, long: bool) -> Option<(i32, i32)> {
        let (mut x, mut y) = (0, 0);
        let found = unsafe {
            if long {
                (self.leaves.input_screen_long)(&mut x, &mut y)
            } else {
                (self.leaves.input_screen_tap)(&mut x, &mut y)
            }
        } != 0;
        found.then_some((x, y))
    }

    fn loop_link_index(
        &self,
        access: &super::ResourceAccess<'_>,
        x: i32,
        y: i32,
        left: i32,
        top: i32,
    ) -> Option<u16> {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return u16::try_from(unsafe { (hooks.link_hit)(hooks.context, x, y, left, top) }).ok();
        }
        self.link_at_point(access, x, y, left, top)
    }

    pub(super) fn link_href_at_point(&self, x: i32, y: i32) -> Result<Option<Text>, ()> {
        let access = self.lock_resources();
        if self.model(|state| state.scene_gate.failed) {
            return Err(());
        }
        let (left, top) = self.model(|state| state.link_margins);
        let Some(index) = self.loop_link_index(&access, x, y, left, top) else {
            return Ok(None);
        };
        self.metadata_link_href(&access, index).map(Some).ok_or(())
    }

    pub(in crate::app) fn dispatch_remote_shortcut(&self, reader_menu: bool) -> bool {
        let Some(shortcut) = self.remote_shortcut(reader_menu) else {
            return false;
        };
        match shortcut {
            RemoteShortcut::Selection => {
                self.start_clip_selection(-1, -1, super::child::SELECTION_CLIP)
            }
            RemoteShortcut::Toolbar => self.open_overlay(Overlay::Toolbar),
            RemoteShortcut::Menu => self.open_reader_menu(),
        }
        true
    }
}
