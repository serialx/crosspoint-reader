use super::abi;
use xp_ui::app::result::{ActivityResult, Data, Kind, OwnedResult};

impl abi::Text {
    /// The platform lends these bytes for the synchronous callback only.
    pub unsafe fn bytes<'a>(self) -> Option<&'a [u8]> {
        if self.len == 0 {
            Some(&[])
        } else if self.data.is_null() || self.len > isize::MAX as usize {
            None
        } else {
            Some(unsafe { core::slice::from_raw_parts(self.data, self.len) })
        }
    }

    pub fn from_bytes(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len(),
        }
    }
}

fn kind_tag(kind: Kind) -> u8 {
    match kind {
        Kind::None => abi::RESULT_NONE,
        Kind::Wifi => abi::RESULT_WIFI,
        Kind::Keyboard => abi::RESULT_KEYBOARD,
        Kind::Menu => abi::RESULT_MENU,
        Kind::Chapter => abi::RESULT_CHAPTER,
        Kind::Percent => abi::RESULT_PERCENT,
        Kind::Interval => abi::RESULT_INTERVAL,
        Kind::Page => abi::RESULT_PAGE,
        Kind::Clipping => abi::RESULT_CLIPPING,
        Kind::Progress => abi::RESULT_PROGRESS,
        Kind::NetworkMode => abi::RESULT_NETWORK_MODE,
        Kind::Footnote => abi::RESULT_FOOTNOTE,
        Kind::FilePath => abi::RESULT_FILE_PATH,
        Kind::TransferFailed => abi::RESULT_TRANSFER_FAILED,
    }
}

unsafe fn borrowed<'a>(view: &abi::ResultView) -> Option<ActivityResult<&'a [u8]>> {
    // Select the union member only after checking its integer tag.
    let data = unsafe {
        match view.kind {
            abi::RESULT_NONE => Data::None,
            abi::RESULT_WIFI => {
                let v = view.value.wifi;
                Data::Wifi {
                    connected: v.connected != 0,
                    ssid: v.ssid.bytes()?,
                    ip: v.ip.bytes()?,
                }
            }
            abi::RESULT_KEYBOARD => Data::Keyboard(view.value.keyboard.bytes()?),
            abi::RESULT_MENU => {
                let v = view.value.menu;
                Data::Menu {
                    action: v.action,
                    orientation: v.orientation,
                    page_turn: v.page_turn,
                }
            }
            abi::RESULT_CHAPTER => {
                let v = view.value.chapter;
                Data::Chapter {
                    spine: v.spine,
                    anchor: v.anchor.bytes()?,
                }
            }
            abi::RESULT_PERCENT => Data::Percent(view.value.percent),
            abi::RESULT_INTERVAL => Data::Interval(view.value.interval),
            abi::RESULT_PAGE => Data::Page(view.value.page),
            abi::RESULT_CLIPPING => {
                let v = view.value.clipping;
                Data::Clipping {
                    action: v.action,
                    text: v.text.bytes()?,
                    start_page: v.start_page,
                    end_page: v.end_page,
                    start_word: v.start_word,
                    end_word: v.end_word,
                    word_count: v.word_count,
                    start_offset: v.start_offset,
                    end_offset: v.end_offset,
                }
            }
            abi::RESULT_PROGRESS => {
                let v = view.value.progress;
                Data::Progress {
                    spine: v.spine,
                    page: v.page,
                    total_pages: v.total_pages,
                    xpath: v.xpath.bytes()?,
                    percentage: v.percentage,
                    saved: v.saved != 0,
                    has_visible_offset: v.has_visible_offset != 0,
                    visible_offset: v.visible_offset,
                }
            }
            abi::RESULT_NETWORK_MODE => Data::NetworkMode(view.value.network_mode),
            abi::RESULT_FOOTNOTE => Data::Footnote(view.value.footnote.bytes()?),
            abi::RESULT_FILE_PATH => Data::FilePath(view.value.file_path.bytes()?),
            _ => return None,
        }
    };
    Some(ActivityResult {
        cancelled: view.cancelled != 0,
        data,
    })
}

pub struct Transfer {
    pub result: OwnedResult,
    pub failed_kind: Option<u8>,
    received: bool,
}

impl Transfer {
    pub fn new() -> Self {
        Self {
            result: ActivityResult::transfer_failed(Kind::None),
            failed_kind: None,
            received: false,
        }
    }

    pub unsafe extern "C" fn receive(
        context: *mut core::ffi::c_void,
        view: *const abi::ResultView,
    ) -> u8 {
        let Some(transfer) = (unsafe { context.cast::<Self>().as_mut() }) else {
            return 0;
        };
        let Some(view) = (unsafe { view.as_ref() }) else {
            return 0;
        };
        if transfer.received {
            return 0;
        }
        transfer.received = true;
        let Some(borrowed) = (unsafe { borrowed(view) }) else {
            transfer.failed_kind = Some(view.kind);
            return 0;
        };
        let (owned, error) = borrowed.into_owned();
        transfer.result = owned;
        if error.is_some() {
            transfer.failed_kind = Some(view.kind);
        }
        u8::from(error.is_none())
    }

    pub fn finish(mut self, accepted: bool) -> (OwnedResult, Option<u8>) {
        if !accepted || !self.received {
            self.failed_kind = Some(
                self.failed_kind
                    .unwrap_or(kind_tag(self.result.data.kind())),
            );
            if !matches!(self.result.data, Data::TransferFailed(_)) {
                self.result = ActivityResult::transfer_failed(self.result.data.kind());
            }
        }
        (self.result, self.failed_kind)
    }
}

pub fn view(result: &OwnedResult) -> abi::ResultView {
    let value = match &result.data {
        Data::None => abi::ResultViewValue { page: 0 },
        Data::Wifi {
            connected,
            ssid,
            ip,
        } => abi::ResultViewValue {
            wifi: abi::WifiResult {
                connected: u8::from(*connected),
                ssid: abi::Text::from_bytes(ssid),
                ip: abi::Text::from_bytes(ip),
            },
        },
        Data::Keyboard(text) => abi::ResultViewValue {
            keyboard: abi::Text::from_bytes(text),
        },
        Data::Menu {
            action,
            orientation,
            page_turn,
        } => abi::ResultViewValue {
            menu: abi::MenuResult {
                action: *action,
                orientation: *orientation,
                page_turn: *page_turn,
            },
        },
        Data::Chapter { spine, anchor } => abi::ResultViewValue {
            chapter: abi::ChapterResult {
                spine: *spine,
                anchor: abi::Text::from_bytes(anchor),
            },
        },
        Data::Percent(percent) => abi::ResultViewValue { percent: *percent },
        Data::Interval(interval) => abi::ResultViewValue {
            interval: *interval,
        },
        Data::Page(page) => abi::ResultViewValue { page: *page },
        Data::Clipping {
            action,
            text,
            start_page,
            end_page,
            start_word,
            end_word,
            word_count,
            start_offset,
            end_offset,
        } => abi::ResultViewValue {
            clipping: abi::ClippingResult {
                action: *action,
                text: abi::Text::from_bytes(text),
                start_page: *start_page,
                end_page: *end_page,
                start_word: *start_word,
                end_word: *end_word,
                word_count: *word_count,
                start_offset: *start_offset,
                end_offset: *end_offset,
            },
        },
        Data::Progress {
            spine,
            page,
            total_pages,
            xpath,
            percentage,
            saved,
            has_visible_offset,
            visible_offset,
        } => abi::ResultViewValue {
            progress: abi::ProgressResult {
                spine: *spine,
                page: *page,
                total_pages: *total_pages,
                xpath: abi::Text::from_bytes(xpath),
                percentage: *percentage,
                saved: u8::from(*saved),
                has_visible_offset: u8::from(*has_visible_offset),
                visible_offset: *visible_offset,
            },
        },
        Data::NetworkMode(network_mode) => abi::ResultViewValue {
            network_mode: *network_mode,
        },
        Data::Footnote(text) => abi::ResultViewValue {
            footnote: abi::Text::from_bytes(text),
        },
        Data::FilePath(text) => abi::ResultViewValue {
            file_path: abi::Text::from_bytes(text),
        },
        Data::TransferFailed(kind) => abi::ResultViewValue {
            failed_kind: kind_tag(*kind),
        },
    };
    abi::ResultView {
        kind: kind_tag(result.data.kind()),
        cancelled: u8::from(result.cancelled),
        value,
    }
}
