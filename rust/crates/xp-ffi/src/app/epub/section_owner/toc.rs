//! TOC resolution keeps the native 64-target batches and 1 KiB disk cursor.
extern crate alloc;
use super::super::{
    abi::{XpEpubLeaves, XpEpubToc},
    book_owner::{log, queries, Buffered, FileSlot},
    Text,
};
use super::{file::CacheFile, owner::ReaderSection, BuildRecords};
use crate::{
    app::abi,
    section_cache::{self, Query, Reader},
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr::NonNull};

struct Target {
    hash: usize,
    index: i32,
}
struct Selection {
    page: i32,
    selected: i32,
    latest: i32,
}
struct Disk {
    file: FileSlot,
    buffered: Buffered,
    start: u32,
    count: u32,
    text: Vec<u8>,
}
impl Disk {
    fn read(&mut self, bytes: &mut [u8]) -> bool {
        unsafe {
            self.buffered
                .read(&mut self.file, bytes.as_mut_ptr(), bytes.len())
                == bytes.len()
        }
    }
    fn anchor(&mut self) -> Option<u16> {
        let mut bytes = [0; 4];
        if !self.read(&mut bytes) {
            return None;
        }
        let length = u32::from_le_bytes(bytes);
        xp_core::section::table_entry(self.buffered.position(), 0, length, self.file.size(), 0)?;
        let length = usize::try_from(length).ok()?;
        self.text
            .try_reserve_exact(length.saturating_add(1).saturating_sub(self.text.len()))
            .ok()?;
        self.text.resize(length + 1, 0);
        if unsafe {
            self.buffered
                .read(&mut self.file, self.text.as_mut_ptr(), length)
        } != length
        {
            return None;
        }
        self.text[length] = 0;
        if !self.read(&mut bytes[..2]) {
            return None;
        }
        Some(u16::from_le_bytes([bytes[0], bytes[1]]))
    }
}

struct Row {
    spine: i32,
    hash: usize,
    empty: bool,
    leaves: &'static XpEpubLeaves,
}
unsafe extern "C" fn row(raw: *mut c_void, item: *const XpEpubToc) -> u8 {
    let out = unsafe { &mut *raw.cast::<Row>() };
    let item = unsafe { &*item };
    out.spine = item.spine;
    out.empty = item.anchor.len == 0;
    out.hash = if out.empty {
        0
    } else {
        unsafe { (out.leaves.native_path_hash)(item.anchor) as usize }
    };
    1
}
struct Match<'a> {
    text: &'a [u8],
    matches: bool,
}
unsafe extern "C" fn matches(raw: *mut c_void, item: *const XpEpubToc) -> u8 {
    let out = unsafe { &mut *raw.cast::<Match<'_>>() };
    out.matches = unsafe { (*item).anchor.bytes() }.is_some_and(|bytes| bytes == out.text);
    1
}

struct TocQuery<'a> {
    book: NonNull<c_void>,
    spine: i32,
    path: &'a Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    records: Option<&'a BuildRecords>,
}
impl TocQuery<'_> {
    fn build_anchor(&self, text: &[u8]) -> Option<u16> {
        let records = self.records?;
        (0..records.anchors.len()).find_map(|index| {
            let anchor = records.anchors.get(index).unwrap();
            (anchor.text.bytes() == text).then_some(anchor.page)
        })
    }
}
unsafe fn consider(
    query: &TocQuery<'_>,
    targets: &[Target],
    selection: &mut Selection,
    text: abi::Text,
    page: u16,
    disk: bool,
) {
    let leaves = query.leaves;
    let hash = unsafe { (leaves.native_path_hash)(text) as usize };
    let text = unsafe { text.bytes() }.expect("owned anchor");
    for target in targets {
        if target.hash != hash {
            continue;
        }
        let mut check = Match {
            text,
            matches: false,
        };
        unsafe {
            queries::toc(
                query.book.as_ptr(),
                target.index,
                matches,
                (&mut check as *mut Match<'_>).cast(),
            )
        };
        if !check.matches {
            continue;
        }
        let page = i32::from(if disk {
            query.build_anchor(text).unwrap_or(page)
        } else {
            page
        });
        if page <= selection.page
            && (page > selection.latest
                || (page == selection.latest && target.index > selection.selected))
        {
            selection.selected = target.index;
            selection.latest = page;
        }
    }
}
unsafe fn resolve(
    query: &TocQuery<'_>,
    targets: &[Target],
    selection: &mut Selection,
    disk: &mut Option<Disk>,
) {
    if let Some(records) = query.records {
        for index in 0..records.anchors.len() {
            let anchor = records.anchors.get(index).unwrap();
            unsafe {
                consider(
                    query,
                    targets,
                    selection,
                    anchor.text.wire(),
                    anchor.page,
                    false,
                )
            };
        }
    }
    if let Some(disk) = disk {
        if disk.buffered.seek(&mut disk.file, disk.start) {
            for _ in 0..disk.count {
                let Some(page) = disk.anchor() else {
                    break;
                };
                unsafe {
                    consider(
                        query,
                        targets,
                        selection,
                        abi::Text::from_terminated(&disk.text),
                        page,
                        true,
                    )
                };
            }
        }
    }
}
impl TocQuery<'_> {
    unsafe fn select(&self, page: i32) -> i32 {
        let (book, spine, leaves, resources) = (self.book, self.spine, self.leaves, self.resources);
        let first = unsafe { queries::toc_for_spine(book.as_ptr(), spine) };
        let mut targets = Vec::new();
        if targets.try_reserve_exact(64).is_err() {
            log::message(leaves, 1, b"SCT", format_args!("OOM: TOC anchor lookup"));
            return first;
        }
        let mut io = CacheFile::new(self.path, resources, leaves);
        let mut output = [0; 3];
        let mut disk = if section_cache::query(&mut io, Query::AnchorMap, &mut output).is_some() {
            let start = io.position();
            let file = io.into_file();
            let buffered = Buffered::with_capacity(resources, &file, false, 1024);
            Some(Disk {
                file,
                buffered,
                start,
                count: output[0],
                text: Vec::new(),
            })
        } else {
            None
        };
        let mut selection = Selection {
            page,
            selected: first,
            latest: -1,
        };
        for index in first.max(0)..unsafe { queries::toc_count(book.as_ptr()) } {
            let mut item = Row {
                spine: -1,
                hash: 0,
                empty: true,
                leaves,
            };
            unsafe { queries::toc(book.as_ptr(), index, row, (&mut item as *mut Row).cast()) };
            if item.spine != spine {
                continue;
            }
            if item.empty {
                if page >= 0 && selection.latest <= 0 {
                    selection.selected = index;
                    selection.latest = 0;
                }
            } else {
                targets.push(Target {
                    hash: item.hash,
                    index,
                });
                if targets.len() == 64 {
                    unsafe { resolve(self, &targets, &mut selection, &mut disk) };
                    targets.clear();
                }
            }
        }
        if !targets.is_empty() {
            unsafe { resolve(self, &targets, &mut selection, &mut disk) };
        }
        selection.selected
    }
}

impl ReaderSection {
    pub(in crate::app::epub) unsafe fn select_toc(
        section: Option<NonNull<Self>>,
        book: NonNull<c_void>,
        spine: i32,
        page: i32,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> i32 {
        unsafe {
            section.map_or_else(
                || Self::cached_toc(book, spine, page, resources, leaves),
                |section| Self::toc_for_page(section, page),
            )
        }
    }

    pub(in crate::app::epub) unsafe fn toc_for_page(raw: NonNull<Self>, page: i32) -> i32 {
        let owner = raw.as_ptr();
        let query = TocQuery {
            book: unsafe { (*owner).book },
            spine: unsafe { (*owner).spine },
            path: unsafe { &(*owner).path },
            resources: unsafe { (*owner).resources },
            leaves: unsafe { (*owner).leaves },
            records: unsafe { (*owner).build.as_ref() }.map(|build| build.services().records()),
        };
        unsafe { query.select(page) }
    }
    pub(in crate::app::epub) unsafe fn cached_toc(
        book: NonNull<c_void>,
        spine: i32,
        page: i32,
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
    ) -> i32 {
        let Some(path) = (unsafe { Self::cache_path(book, spine) }) else {
            return unsafe { queries::toc_for_spine(book.as_ptr(), spine) };
        };
        unsafe {
            TocQuery {
                book,
                spine,
                path: &path,
                resources,
                leaves,
                records: None,
            }
            .select(page)
        }
    }
}
