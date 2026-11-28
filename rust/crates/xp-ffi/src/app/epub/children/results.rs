use crate::app::resources::Text;
use xp_ui::reader::menu_parent::Action;

pub(crate) struct ChapterSelection {
    pub(crate) spine: i32,
    pub(crate) anchor: Text,
}
pub(crate) struct ClippingPosition {
    pub(crate) spine: i32,
    pub(crate) page: i32,
    pub(crate) offset: u32,
}

/// Payloads move out before child destruction and into the resumed parent.
pub(crate) enum ReaderChildResult {
    Menu {
        action: Option<Action>,
        selected: Option<Action>,
        orientation: u8,
        page_turn: u8,
    },
    Chapter(Option<ChapterSelection>),
    Clippings(Option<ClippingPosition>),
    Bookmarks(Option<super::bookmark_owned::Position>),
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

#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<ReaderChildResult>() == 36);
