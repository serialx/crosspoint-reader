use super::{
    abi, app_abi,
    book::{with_book_text, with_text},
    EpubSession, ResourceAccess, Text,
};
use core::{ffi::c_void, mem::MaybeUninit};
use xp_core::i18n::ids;

const TEXT_NAMES: [u16; 5] = [
    ids::STR_FONT,
    ids::STR_FONT_SIZE,
    ids::STR_LINE_SPACING,
    ids::STR_PARA_ALIGNMENT,
    ids::STR_FOCUS_READING,
];
const SPACING: [u16; 4] = [
    ids::STR_TIGHT,
    ids::STR_NORMAL,
    ids::STR_WIDE,
    ids::STR_EXTRA_WIDE,
];
const ALIGNMENT: [u16; 5] = [
    ids::STR_JUSTIFY,
    ids::STR_ALIGN_LEFT,
    ids::STR_CENTER,
    ids::STR_ALIGN_RIGHT,
    ids::STR_BOOK_S_STYLE,
];
const ORIENTATION: [u16; 4] = [
    ids::STR_PORTRAIT,
    ids::STR_LANDSCAPE_CW,
    ids::STR_ORIENTATION_INVERTED,
    ids::STR_LANDSCAPE_CCW,
];
const MORE_LABELS: [u16; 19] = [
    ids::STR_SELECT_CHAPTER,
    ids::STR_FOOTNOTES,
    ids::STR_TEXT_SETTINGS,
    ids::STR_NIGHT_MODE,
    ids::STR_FRONTLIGHT,
    ids::STR_BT_PAGE_TURNER,
    ids::STR_GO_TO_PERCENT,
    ids::STR_AUTO_TURN_PAGES_PER_MIN,
    ids::STR_ORIENTATION,
    ids::STR_BOOKMARKS,
    ids::STR_SAVE_CLIPPING,
    ids::STR_VIEW_CLIPPINGS,
    ids::STR_TOGGLE_BOOKMARK,
    ids::STR_SCREENSHOT_BUTTON,
    ids::STR_DISPLAY_QR,
    ids::STR_GO_HOME_BUTTON,
    ids::STR_SYNC_PROGRESS,
    ids::STR_DELETE_CACHE,
    ids::STR_LOOKUP,
];

pub(super) fn more_action_label(action: u8) -> u16 {
    MORE_LABELS[usize::from(action)]
}

// Labels and font family borrow immutable/current settings storage. Numbers fit
// locally, so filling a retained native toolbar row needs no Rust allocation.
pub(super) enum RowText<'a> {
    Borrowed(&'a [u8]),
    Number { bytes: [u8; 16], len: usize },
}
impl RowText<'_> {
    fn number(mut value: u32, suffix: &[u8]) -> Self {
        let mut bytes = [0; 16];
        let mut len = 0;
        loop {
            bytes[len] = b'0' + (value % 10) as u8;
            len += 1;
            value /= 10;
            if value == 0 {
                break;
            }
        }
        bytes[..len].reverse();
        bytes[len..len + suffix.len()].copy_from_slice(suffix);
        len += suffix.len();
        Self::Number { bytes, len }
    }
    pub(super) fn bytes(&self) -> &[u8] {
        match self {
            Self::Borrowed(bytes) => bytes,
            Self::Number { bytes, len } => &bytes[..*len],
        }
    }
    pub(super) fn wire(&self) -> app_abi::Text {
        app_abi::Text::from_bytes(self.bytes())
    }
}

impl EpubSession {
    fn value_label(&self, id: u16) -> app_abi::Text {
        unsafe { (self.resources.translate)(id) }
    }

    fn row_label(&self, id: u16) -> RowText<'_> {
        RowText::Borrowed(unsafe { self.value_label(id).bytes() }.unwrap_or(&[]))
    }

    pub(super) fn text_row_name<'a>(
        &'a self,
        access: &'a ResourceAccess<'_>,
        row: i32,
    ) -> RowText<'a> {
        self.check_resources(access);
        TEXT_NAMES
            .get(row as usize)
            .map_or(RowText::Borrowed(&[]), |&id| self.row_label(id))
    }

    pub(super) fn text_row_value<'a>(
        &'a self,
        access: &'a ResourceAccess<'_>,
        row: i32,
    ) -> RowText<'a> {
        self.check_resources(access);
        if !(0..4).contains(&row) {
            return RowText::Borrowed(&[]);
        }
        let settings = self.settings(false);
        match row {
            0 => {
                let family = unsafe { settings.font_family.bytes() }.unwrap_or(&[]);
                if family.first().is_some_and(|&byte| byte != 0) {
                    RowText::Borrowed(family)
                } else {
                    self.row_label(ids::STR_NOTO_SANS)
                }
            }
            1 => RowText::number(u32::from(settings.font_size), b" pt"),
            2 => self.row_label(SPACING[usize::from(settings.line_spacing) % 4]),
            3 => self.row_label(ALIGNMENT[usize::from(settings.alignment) % 5]),
            _ => unreachable!(),
        }
    }

    pub(super) fn more_row_name<'a>(
        &'a self,
        access: &'a ResourceAccess<'_>,
        row: i32,
    ) -> RowText<'a> {
        let Some(action) = self.more_action(access, row) else {
            return RowText::Borrowed(&[]);
        };
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.view_value_hooks {
            return self.row_label(unsafe { (hook.more_label)(hook.context, row) });
        }
        self.row_label(more_action_label(action))
    }

    pub(super) fn more_row_value<'a>(
        &'a self,
        access: &'a ResourceAccess<'_>,
        row: i32,
    ) -> RowText<'a> {
        match self.more_action(access, row) {
            Some(8) => {
                self.row_label(ORIENTATION[usize::from(self.settings(false).orientation) % 4])
            }
            Some(7) => {
                let option = self.model(|state| state.auto_option);
                match option {
                    1..=4 => RowText::number([1, 1, 3, 6, 12][usize::from(option)], &[]),
                    _ => self.row_label(ids::STR_STATE_OFF),
                }
            }
            _ => RowText::Borrowed(&[]),
        }
    }

    fn with_chapter_title<R>(
        &self,
        access: &ResourceAccess<'_>,
        empty_without_book: bool,
        use_text: impl FnOnce(app_abi::Text) -> R,
    ) -> Option<R> {
        let fallback = if empty_without_book {
            app_abi::Text::from_bytes(&[])
        } else {
            self.value_label(ids::STR_UNNAMED)
        };
        let Some(book) = self.book(access) else {
            return Some(use_text(fallback));
        };
        let spine = self.model(|state| state.current.spine);
        let index = unsafe { (self.leaves.book_toc_for_spine)(book.as_ptr(), spine) };
        if index == -1 {
            return Some(use_text(if empty_without_book {
                self.value_label(ids::STR_UNNAMED)
            } else {
                fallback
            }));
        }
        struct TitleSink {
            sink: app_abi::TextSink,
            context: *mut c_void,
        }
        unsafe extern "C" fn title(context: *mut c_void, item: *const abi::XpEpubToc) -> u8 {
            let call = &*context.cast::<TitleSink>();
            (call.sink)(call.context, (*item).title)
        }
        with_text(
            |sink, context| {
                let mut call = TitleSink { sink, context };
                unsafe {
                    (self.leaves.book_toc)(
                        book.as_ptr(),
                        index,
                        title,
                        (&mut call as *mut TitleSink).cast(),
                    )
                }
            },
            use_text,
        )
    }

    pub(super) fn current_chapter_title<R>(
        &self,
        access: &ResourceAccess<'_>,
        use_text: impl FnOnce(app_abi::Text) -> R,
    ) -> Option<R> {
        self.with_chapter_title(access, true, use_text)
    }

    fn paint_status_title(
        &self,
        access: &ResourceAccess<'_>,
        status: &abi::XpEpubStatus,
        title: app_abi::Text,
    ) {
        let value = abi::XpEpubStatus {
            title,
            bookmarked: u8::from(self.model(|state| state.bookmarked)),
            building: u8::from(self.section(access).is_some_and(|section| unsafe {
                (self.leaves.section_building)(section.as_ptr()) != 0
            })),
            ..*status
        };
        unsafe { (self.leaves.paint_status)(&value) };
    }

    // Keep the peer snapshot and the existing 64-byte snprintf result out of the
    // common status frame. Only a connected note needs these local buffers.
    #[inline(never)]
    fn paint_connected_status(&self, access: &ResourceAccess<'_>, status: &abi::XpEpubStatus) {
        let mut flags = MaybeUninit::uninit();
        let mut peer = MaybeUninit::<abi::XpEpubPeer>::uninit();
        unsafe { (self.leaves.radio_status)(flags.as_mut_ptr(), peer.as_mut_ptr()) };
        let peer = unsafe { peer.assume_init() };
        let source = if peer.name[0] != 0 {
            &peer.name[..]
        } else {
            &peer.addr[..]
        };
        let source = &source[..source
            .iter()
            .position(|&byte| byte == 0)
            .unwrap_or(source.len())];
        let format = self.value_label(ids::STR_BT_CONNECTED_TO);
        let format = unsafe { format.bytes() }.unwrap_or(&[]);
        let mut title = [0; 64];
        let mut length = 0;
        let mut at = 0;
        while at < format.len() {
            let (bytes, step) = if format[at..].starts_with(b"%s") {
                (source, 2)
            } else if format[at..].starts_with(b"%%") {
                (&b"%"[..], 2)
            } else {
                (&format[at..at + 1], 1)
            };
            let count = bytes.len().min(title.len() - 1 - length);
            title[length..length + count].copy_from_slice(&bytes[..count]);
            length += count;
            at += step;
        }
        self.paint_status_title(access, status, app_abi::Text::from_bytes(&title[..length]));
    }

    pub(super) fn render_status_bar(&self, access: &ResourceAccess<'_>) {
        let section = self.section(access);
        let page = if section.is_some() {
            self.model(|state| state.current.page).wrapping_add(1)
        } else {
            1
        };
        let count = section.map_or(1, |section| unsafe {
            (self.leaves.section_estimated_pages)(section.as_ptr())
        });
        let fraction = if count > 0 {
            page as f32 / count as f32
        } else {
            0.0
        };
        let book = self.book(access);
        let progress = book.map_or(0.0, |book| {
            let spine = self.model(|state| state.current.spine);
            unsafe { (self.leaves.book_progress)(book.as_ptr(), spine, fraction) * 100.0 }
        });
        let mut geometry = MaybeUninit::uninit();
        unsafe { (self.leaves.render_geometry)(geometry.as_mut_ptr()) };
        let geometry: abi::XpEpubGeometry = unsafe { geometry.assume_init() };
        let raw_note = self.radio_status().note;
        let shown = geometry.title_mode != 2;
        let note = if shown { raw_note } else { 0 };
        self.model(|state| state.link_note_drawn = shown.then_some(note));
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.view_value_hooks {
            unsafe { (hook.link_note_drawn)(hook.context, u8::from(shown), raw_note) };
        }
        let mut status = abi::XpEpubStatus {
            title: app_abi::Text::from_bytes(&[]),
            book_progress: progress,
            page,
            pages: count,
            y_offset: 0,
            bookmarked: 0,
            building: 0,
        };
        match note {
            3 => self.paint_connected_status(access, &status),
            4 => self.paint_status_title(access, &status, self.value_label(ids::STR_BT_LOW_MEMORY)),
            1 => self.paint_status_title(access, &status, self.value_label(ids::STR_BT_CONNECTING)),
            0 => {
                let (automatic, duration) =
                    self.model(|state| (state.auto_turn, state.turn_duration_ms));
                if automatic {
                    if duration == 0 {
                        return;
                    }
                    let rate = RowText::number(60_000 / duration, &[]);
                    let label = self.value_label(ids::STR_AUTO_TURN_ENABLED);
                    // Translated prefixes are unbounded. This one fallible temporary
                    // replaces the former concatenated std::string and dies after paint.
                    let mut title = Text::default();
                    if title
                        .assign_parts(&[unsafe { label.bytes() }.unwrap_or(&[]), rate.bytes()])
                        .is_err()
                    {
                        return;
                    }
                    if geometry.status_height == 0
                        || geometry.status_height == geometry.progress_height
                    {
                        status.y_offset = geometry.status_margin;
                    }
                    self.paint_status_title(access, &status, title.wire());
                } else if geometry.title_mode == 1 {
                    self.with_chapter_title(access, false, |title| {
                        self.paint_status_title(access, &status, title)
                    });
                } else if geometry.title_mode == 0 {
                    if let Some(book) = book {
                        with_book_text(book, self.leaves.book_title, |title| {
                            self.paint_status_title(access, &status, title)
                        });
                    } else {
                        self.paint_status_title(access, &status, status.title);
                    }
                } else {
                    self.paint_status_title(access, &status, status.title);
                }
            }
            _ => self.paint_status_title(access, &status, self.value_label(ids::STR_BT_FAILED)),
        }
    }
}
