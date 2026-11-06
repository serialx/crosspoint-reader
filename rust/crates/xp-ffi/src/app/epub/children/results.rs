use crate::app::resources::Text;
use xp_ui::reader::menu_parent::Action;

pub(crate) struct ChapterSelection {
    pub(crate) spine: i32,
    pub(crate) anchor: Text,
}

/// Payloads move out before child destruction and into the resumed parent.
pub(crate) enum ReaderChildResult {
    Menu {
        action: Option<Action>,
        orientation: u8,
        page_turn: u8,
    },
    Chapter(Option<ChapterSelection>),
    Percent(Option<u8>),
    QrClosed,
    Footnote(Option<Text>),
    LookupClosed {
        cancelled: bool,
    },
    DefinitionClosed {
        cancelled: bool,
    },
}
