extern crate alloc;

use super::{
    abi,
    resources::{Book, Identity, Library, Recent, Text},
};
use alloc::vec::Vec;
use core::ffi::c_void;
use xp_core::i18n::ids::{STR_LIBRARY_REBUILDING, STR_LOADING_POPUP};
use xp_ui::scene::Rect;

pub struct Loaded {
    pub identities: Vec<Identity>,
    pub covers: Vec<Text>,
    pub continuing: bool,
    pub progress: i32,
}

struct Collection {
    identities: Vec<Identity>,
    covers: Vec<Text>,
    limit: usize,
    services: &'static abi::Resources,
    failed: bool,
}

impl Collection {
    fn push(&mut self, recent: Recent) {
        // Both arrays were reserved together before visiting native records.
        self.identities.push(recent.identity);
        self.covers.push(recent.cover);
    }

    unsafe extern "C" fn recent(context: *mut c_void, book: *const abi::Book) -> u8 {
        let this = unsafe { &mut *context.cast::<Self>() };
        if this.identities.len() >= this.limit {
            return 0;
        }
        let Some(book) = (unsafe { book.as_ref() }) else {
            this.failed = true;
            return 0;
        };
        if unsafe { (this.services.recent_missing)(book.path) } != 0 {
            return 1;
        }
        let Some(book) = (unsafe { Recent::copy(book) }) else {
            this.failed = true;
            return 0;
        };
        this.push(book);
        1
    }

    fn fill_library(&mut self, metadata: bool) {
        if self.identities.len() >= self.limit {
            return;
        }
        let Some(mut library) = Library::open(self.services) else {
            return;
        };
        if library.count().is_none() {
            drop(library);
            unsafe {
                (self.services.popup_draw)((self.services.translate)(STR_LIBRARY_REBUILDING))
            };
            if unsafe { (self.services.library_rebuild)(u8::from(metadata)) } == 0 {
                return;
            }
            let Some(reopened) = Library::open(self.services) else {
                return;
            };
            library = reopened;
        }
        let Some(count) = library.count() else { return };
        for row in 0..count {
            if self.identities.len() >= self.limit {
                break;
            }
            let path = match library.row(row, false) {
                Ok(Some(book)) => book,
                Ok(None) => continue,
                Err(()) => break,
            };
            if self
                .identities
                .iter()
                .any(|existing| existing.path.bytes() == path.identity.path.bytes())
                || unsafe { (self.services.recent_missing)(path.identity.path.wire()) } != 0
            {
                continue;
            }
            let mut book = match library.row(row, true) {
                Ok(Some(book)) => book,
                Ok(None) => continue,
                Err(()) => break,
            };
            book.identity.path = path.identity.path;
            self.push(book);
        }
    }
}

pub fn load(
    services: &'static abi::Resources,
    settings: abi::HomeSettings,
    limit: usize,
) -> Option<Loaded> {
    let mut collection = Collection {
        identities: Vec::new(),
        covers: Vec::new(),
        limit,
        services,
        failed: false,
    };
    collection.identities.try_reserve_exact(limit).ok()?;
    collection.covers.try_reserve_exact(limit).ok()?;
    unsafe {
        (services.recents_visit)(
            Collection::recent,
            (&mut collection as *mut Collection).cast(),
        )
    };
    if collection.failed {
        return None;
    }
    let continuing = !collection.identities.is_empty();
    if settings.grid != 0 {
        collection.fill_library(settings.library_metadata != 0);
        for (identity, cover) in collection.identities.iter().zip(&mut collection.covers) {
            if !cover.is_empty() {
                continue;
            }
            let Some(kind) = book_kind(identity.path.bytes()) else {
                continue;
            };
            if let Some(book) = Book::open(services, kind, &identity.path) {
                if let Some(base) = book.thumbnail(0) {
                    *cover = base;
                }
            }
        }
    }
    let progress = if settings.grid != 0 && continuing {
        unsafe { (services.book_progress)(collection.identities[0].path.wire()) }
    } else {
        -1
    };
    Some(Loaded {
        identities: collection.identities,
        covers: collection.covers,
        continuing,
        progress,
    })
}

pub fn book_kind(path: &[u8]) -> Option<abi::BookKind> {
    let suffix = |ending: &[u8]| {
        path.get(path.len().saturating_sub(ending.len())..)
            .is_some_and(|tail| tail.eq_ignore_ascii_case(ending))
    };
    if [b".epub".as_slice(), b".txt", b".md"]
        .iter()
        .any(|s| suffix(s))
    {
        Some(abi::BOOK_REFLOWABLE)
    } else if suffix(b".xtc") || suffix(b".xtch") {
        Some(abi::BOOK_XTC)
    } else {
        None
    }
}

pub fn thumbnail(services: &abi::Resources, base: &Text, height: i32) -> Option<Text> {
    Text::receive(|sink, context| unsafe {
        (services.thumbnail_path)(base.wire(), height, sink, context)
    })
}

fn exists(services: &abi::Resources, path: &Text) -> bool {
    unsafe { (services.path_exists)(path.wire()) != 0 }
}

fn grid_cover(
    services: &'static abi::Resources,
    identity: &Identity,
    cover: &mut Text,
    height: i32,
    popup: &mut Option<Rect>,
) {
    if !cover.is_empty()
        && thumbnail(services, cover, height).is_some_and(|path| exists(services, &path))
    {
        return;
    }
    if let Some(kind) = book_kind(identity.path.bytes()) {
        let Some(mut book) = Book::open(services, kind, &identity.path) else {
            return;
        };
        let Some(base) = book.thumbnail(0) else {
            return;
        };
        *cover = base;
        if book
            .thumbnail(height)
            .is_some_and(|path| exists(services, &path))
        {
            return;
        }
        if popup.is_none() {
            let rect = unsafe { (services.popup_draw)((services.translate)(STR_LOADING_POPUP)) };
            unsafe { (services.popup_progress)(rect, 0) };
            *popup = Some(rect);
        }
        let success = if kind == abi::BOOK_REFLOWABLE {
            book.generate(height, true)
        } else {
            book.load(false, false) && book.generate(height, false)
        };
        if success {
            return;
        }
    }
    cover.clear();
}

pub fn load_covers(
    services: &'static abi::Resources,
    identities: &[Identity],
    covers: &mut [Text],
    height: i32,
    grid: bool,
    mut invalidate: impl FnMut(),
) {
    let mut popup = None;
    for (index, (identity, cover)) in identities.iter().zip(covers).enumerate() {
        if grid {
            grid_cover(services, identity, cover, height, &mut popup);
            if let Some(rect) = popup {
                unsafe {
                    (services.popup_progress)(rect, ((index + 1) * 100 / identities.len()) as i32)
                };
            }
            continue;
        }
        if cover.is_empty() {
            continue;
        }
        let Some(path) = thumbnail(services, cover, height) else {
            continue;
        };
        if exists(services, &path) {
            continue;
        }
        let Some(kind) = book_kind(identity.path.bytes()) else {
            continue;
        };
        let Some(mut book) = Book::open(services, kind, &identity.path) else {
            continue;
        };
        let loaded = book.load(false, kind == abi::BOOK_REFLOWABLE);
        if kind == abi::BOOK_XTC && !loaded {
            continue;
        }
        let rect = *popup.get_or_insert_with(|| unsafe {
            (services.popup_draw)((services.translate)(STR_LOADING_POPUP))
        });
        unsafe { (services.popup_progress)(rect, (10 + index * (90 / identities.len())) as i32) };
        if !book.generate(height, false) {
            cover.clear();
            let recent = abi::Book {
                path: identity.path.wire(),
                title: identity.title.wire(),
                author: identity.author.wire(),
                cover: cover.wire(),
            };
            unsafe { (services.recent_update)(&recent) };
        }
        invalidate();
    }
}

#[cfg(test)]
pub(super) mod tests {
    use super::*;
    use alloc::{boxed::Box, vec};
    use core::sync::atomic::{AtomicBool, AtomicU8, Ordering};

    static IO_FAILED: AtomicBool = AtomicBool::new(false);
    static METADATA_READS: AtomicU8 = AtomicU8::new(0);

    pub unsafe extern "C" fn library_open() -> *mut c_void {
        core::ptr::NonNull::<u8>::dangling().as_ptr().cast()
    }
    pub unsafe extern "C" fn library_count(_: *mut c_void) -> u32 {
        3
    }
    pub unsafe extern "C" fn library_close(_: *mut c_void) {}
    pub unsafe extern "C" fn recent_missing(path: abi::Text) -> u8 {
        u8::from(unsafe { path.bytes() } == Some(b"/missing".as_slice()))
    }
    pub unsafe extern "C" fn library_row(
        _: *mut c_void,
        row: u32,
        metadata: u8,
        sink: abi::BookSink,
        context: *mut c_void,
    ) -> u8 {
        let empty = abi::Text::from_bytes(b"");
        if metadata != 0 {
            METADATA_READS.fetch_or(1 << row, Ordering::Relaxed);
            if row < 2 {
                IO_FAILED.store(true, Ordering::Relaxed);
            }
        }
        if IO_FAILED.load(Ordering::Relaxed) {
            return 2;
        }
        let path = [b"/duplicate".as_slice(), b"/missing", b"/valid"][row as usize];
        let book = abi::Book {
            path: if metadata == 0 {
                abi::Text::from_bytes(path)
            } else {
                empty
            },
            title: if metadata != 0 {
                abi::Text::from_bytes(b"Valid title")
            } else {
                empty
            },
            author: empty,
            cover: empty,
        };
        unsafe { sink(context, &book) }
    }

    #[test]
    fn duplicate_and_missing_rows_do_not_read_corrupt_metadata() {
        IO_FAILED.store(false, Ordering::Relaxed);
        METADATA_READS.store(0, Ordering::Relaxed);
        let services = Box::leak(Box::new(super::super::test_resources::resources()));
        let mut collection = Collection {
            identities: vec![Identity {
                path: Text::copy(b"/duplicate").unwrap(),
                title: Text::default(),
                author: Text::default(),
            }],
            covers: vec![Text::default()],
            limit: 3,
            services,
            failed: false,
        };
        collection.identities.reserve_exact(2);
        collection.covers.reserve_exact(2);
        collection.fill_library(true);
        assert!(!IO_FAILED.load(Ordering::Relaxed));
        assert_eq!(METADATA_READS.load(Ordering::Relaxed), 1 << 2);
        assert_eq!(collection.identities.len(), 2);
        assert_eq!(collection.identities[1].path.bytes(), b"/valid");
        assert_eq!(collection.identities[1].title.bytes(), b"Valid title");
        assert_eq!(collection.covers.len(), 2);
    }
}
