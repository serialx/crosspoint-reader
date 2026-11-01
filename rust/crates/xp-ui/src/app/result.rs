//! Semantic activity results. Text is borrowed at entry and owned across child exit.
use super::alloc::collections::TryReserveError;
use super::alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    None,
    Wifi,
    Keyboard,
    Menu,
    Chapter,
    Percent,
    Interval,
    Page,
    Clipping,
    Progress,
    NetworkMode,
    Footnote,
    FilePath,
    TransferFailed,
}

#[derive(Debug, PartialEq)]
pub enum Data<T> {
    None,
    Wifi {
        connected: bool,
        ssid: T,
        ip: T,
    },
    Keyboard(T),
    Menu {
        action: i32,
        orientation: u8,
        page_turn: u8,
    },
    Chapter {
        spine: i32,
        anchor: T,
    },
    Percent(i32),
    Interval(u32),
    Page(u32),
    Clipping {
        action: u8,
        text: T,
        start_page: u16,
        end_page: u16,
        start_word: u16,
        end_word: u16,
        word_count: u16,
        start_offset: u32,
        end_offset: u32,
    },
    Progress {
        spine: i32,
        page: i32,
        total_pages: i32,
        xpath: T,
        percentage: f32,
        saved: bool,
        has_visible_offset: bool,
        visible_offset: u32,
    },
    NetworkMode(i32),
    Footnote(T),
    FilePath(T),
    TransferFailed(Kind),
}

impl<T> Data<T> {
    pub fn kind(&self) -> Kind {
        match self {
            Self::None => Kind::None,
            Self::Wifi { .. } => Kind::Wifi,
            Self::Keyboard(_) => Kind::Keyboard,
            Self::Menu { .. } => Kind::Menu,
            Self::Chapter { .. } => Kind::Chapter,
            Self::Percent(_) => Kind::Percent,
            Self::Interval(_) => Kind::Interval,
            Self::Page(_) => Kind::Page,
            Self::Clipping { .. } => Kind::Clipping,
            Self::Progress { .. } => Kind::Progress,
            Self::NetworkMode(_) => Kind::NetworkMode,
            Self::Footnote(_) => Kind::Footnote,
            Self::FilePath(_) => Kind::FilePath,
            Self::TransferFailed(_) => Kind::TransferFailed,
        }
    }

    fn try_map<U, E>(self, mut text: impl FnMut(T) -> Result<U, E>) -> Result<Data<U>, E> {
        Ok(match self {
            Self::None => Data::None,
            Self::Wifi {
                connected,
                ssid,
                ip,
            } => Data::Wifi {
                connected,
                ssid: text(ssid)?,
                ip: text(ip)?,
            },
            Self::Keyboard(value) => Data::Keyboard(text(value)?),
            Self::Menu {
                action,
                orientation,
                page_turn,
            } => Data::Menu {
                action,
                orientation,
                page_turn,
            },
            Self::Chapter { spine, anchor } => Data::Chapter {
                spine,
                anchor: text(anchor)?,
            },
            Self::Percent(value) => Data::Percent(value),
            Self::Interval(value) => Data::Interval(value),
            Self::Page(value) => Data::Page(value),
            Self::Clipping {
                action,
                text: value,
                start_page,
                end_page,
                start_word,
                end_word,
                word_count,
                start_offset,
                end_offset,
            } => Data::Clipping {
                action,
                text: text(value)?,
                start_page,
                end_page,
                start_word,
                end_word,
                word_count,
                start_offset,
                end_offset,
            },
            Self::Progress {
                spine,
                page,
                total_pages,
                xpath,
                percentage,
                saved,
                has_visible_offset,
                visible_offset,
            } => Data::Progress {
                spine,
                page,
                total_pages,
                xpath: text(xpath)?,
                percentage,
                saved,
                has_visible_offset,
                visible_offset,
            },
            Self::NetworkMode(value) => Data::NetworkMode(value),
            Self::Footnote(value) => Data::Footnote(text(value)?),
            Self::FilePath(value) => Data::FilePath(text(value)?),
            Self::TransferFailed(kind) => Data::TransferFailed(kind),
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct ActivityResult<T> {
    pub cancelled: bool,
    pub data: Data<T>,
}

impl<T> ActivityResult<T> {
    pub fn transfer_failed(kind: Kind) -> Self {
        Self {
            cancelled: true,
            data: Data::TransferFailed(kind),
        }
    }

    /// Failure cancels the result, never the pop or the child's destruction.
    pub fn transfer_with<U, E>(
        self,
        copy: impl FnMut(T) -> Result<U, E>,
    ) -> (ActivityResult<U>, Option<E>) {
        let kind = self.data.kind();
        match self.data.try_map(copy) {
            Ok(data) => (
                ActivityResult {
                    cancelled: self.cancelled,
                    data,
                },
                None,
            ),
            Err(error) => (ActivityResult::transfer_failed(kind), Some(error)),
        }
    }
}

pub type OwnedResult = ActivityResult<Vec<u8>>;

impl ActivityResult<&[u8]> {
    pub fn into_owned(self) -> (OwnedResult, Option<TryReserveError>) {
        self.transfer_with(|bytes| {
            let mut owned = Vec::new();
            owned.try_reserve_exact(bytes.len())?;
            owned.extend_from_slice(bytes);
            Ok(owned)
        })
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use core::cell::Cell;

    #[test]
    fn transfer_keeps_exact_bytes_after_child_storage_is_gone() {
        let mut source = Vec::from(&b"path\xFF\0tail"[..]);
        let result = ActivityResult {
            cancelled: false,
            data: Data::FilePath(source.as_slice()),
        };
        let (owned, error) = result.into_owned();
        assert!(error.is_none());
        source.fill(0);
        drop(source);
        assert_eq!(
            owned,
            ActivityResult {
                cancelled: false,
                data: Data::FilePath(Vec::from(&b"path\xFF\0tail"[..]))
            }
        );
    }

    #[test]
    fn failed_second_copy_drops_partial_result_and_cancels() {
        struct Tracked<'a>(&'a Cell<usize>);
        impl Drop for Tracked<'_> {
            fn drop(&mut self) {
                self.0.set(self.0.get() + 1);
            }
        }
        let drops = Cell::new(0);
        let mut copies = 0;
        let result = ActivityResult {
            cancelled: false,
            data: Data::Wifi {
                connected: true,
                ssid: b"ssid".as_slice(),
                ip: b"ip".as_slice(),
            },
        };
        let (owned, error) = result.transfer_with(|_| {
            copies += 1;
            if copies == 2 {
                Err(())
            } else {
                Ok(Tracked(&drops))
            }
        });
        assert!(owned.cancelled);
        assert!(matches!(owned.data, Data::TransferFailed(Kind::Wifi)));
        assert_eq!(error, Some(()));
        assert_eq!(copies, 2);
        assert_eq!(drops.get(), 1);
        drop(owned);
        assert_eq!(drops.get(), 1);
    }

    #[test]
    fn scalar_and_empty_results_do_not_allocate_text() {
        for data in [
            Data::None,
            Data::Percent(-7),
            Data::Interval(u32::MAX),
            Data::Page(0),
            Data::NetworkMode(9),
        ] {
            let result = ActivityResult::<&[u8]> {
                cancelled: true,
                data,
            };
            let kind = result.data.kind();
            let (owned, error) = result
                .transfer_with::<Vec<u8>, ()>(|_| panic!("scalar result requested text storage"));
            assert!(owned.cancelled);
            assert_eq!(owned.data.kind(), kind);
            assert_eq!(error, None);
        }
        let (owned, error) = ActivityResult {
            cancelled: false,
            data: Data::Keyboard(b"".as_slice()),
        }
        .into_owned();
        assert!(error.is_none());
        let Data::Keyboard(text) = owned.data else {
            panic!("wrong result kind")
        };
        assert_eq!(text.capacity(), 0);
    }
}
