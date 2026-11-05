use xp_core::i18n::ids;
use xp_ui::reader::menu_parent::Action;

#[derive(Clone, Copy)]
pub(crate) struct Flags {
    pub(crate) footnotes: bool,
    pub(crate) bookmarks: bool,
    pub(crate) clippings: bool,
    pub(crate) frontlight: bool,
    pub(crate) bluetooth: bool,
    pub(crate) dictionary: bool,
}

pub(crate) struct Items {
    actions: [Action; 19],
    count: u8,
}
impl Items {
    pub(crate) fn new(flags: Flags) -> Self {
        let mut items = Self {
            actions: [Action::Chapter; 19],
            count: 0,
        };
        for (present, action) in [
            (true, Action::Chapter),
            (flags.footnotes, Action::Footnotes),
            (flags.bookmarks, Action::Bookmarks),
            (true, Action::ToggleBookmark),
            (true, Action::SaveClipping),
            (flags.clippings, Action::ViewClippings),
            (true, Action::Night),
            (flags.frontlight, Action::Frontlight),
            (flags.bluetooth, Action::Bluetooth),
            (flags.dictionary, Action::Dictionary),
            (true, Action::Orientation),
            (true, Action::Auto),
            (true, Action::Percent),
            (true, Action::Screenshot),
            (true, Action::Qr),
            (true, Action::Home),
            (true, Action::Sync),
            (true, Action::DeleteCache),
            (true, Action::Text),
        ] {
            if present {
                items.actions[usize::from(items.count)] = action;
                items.count += 1;
            }
        }
        items
    }

    pub(crate) fn actions(&self) -> &[Action] {
        &self.actions[..usize::from(self.count)]
    }

    pub(crate) fn label(&self, index: usize) -> Option<u16> {
        Some(match self.actions().get(index)? {
            Action::Chapter => ids::STR_SELECT_CHAPTER,
            Action::Footnotes => ids::STR_FOOTNOTES,
            Action::Text => ids::STR_TEXT_SETTINGS,
            Action::Night => ids::STR_NIGHT_MODE,
            Action::Frontlight => ids::STR_FRONTLIGHT,
            Action::Bluetooth => ids::STR_BT_PAGE_TURNER,
            Action::Percent => ids::STR_GO_TO_PERCENT,
            Action::Auto => ids::STR_AUTO_TURN_PAGES_PER_MIN,
            Action::Orientation => ids::STR_ORIENTATION,
            Action::Bookmarks => ids::STR_BOOKMARKS,
            Action::SaveClipping => ids::STR_SAVE_CLIPPING,
            Action::ViewClippings => ids::STR_VIEW_CLIPPINGS,
            Action::ToggleBookmark => ids::STR_TOGGLE_BOOKMARK,
            Action::Screenshot => ids::STR_SCREENSHOT_BUTTON,
            Action::Qr => ids::STR_DISPLAY_QR,
            Action::Home => ids::STR_GO_HOME_BUTTON,
            Action::Sync => ids::STR_SYNC_PROGRESS,
            Action::DeleteCache => ids::STR_DELETE_CACHE,
            Action::Dictionary => ids::STR_LOOKUP,
        })
    }
}
