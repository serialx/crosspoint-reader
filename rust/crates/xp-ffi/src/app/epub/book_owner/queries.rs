extern crate alloc;

use super::{cache, items::Items, log, ReaderBook};
use crate::app::{abi as app_abi, epub::abi};
use core::ffi::c_void;
use xp_core::book::{self, residual, residual_abi::*};

// The session owns this stable allocation; native borrowers may only read it.
unsafe fn owner<'a>(raw: *mut c_void) -> &'a ReaderBook {
    unsafe { &*raw.cast::<ReaderBook>() }
}

pub(crate) unsafe fn facade(raw: *mut c_void) -> *mut c_void {
    unsafe {
        (*raw.cast::<ReaderBook>())
            .facade
            .expect("live book facade")
            .as_ptr()
    }
}

pub(crate) unsafe fn spine_count(raw: *mut c_void) -> i32 {
    i32::from(unsafe { owner(raw) }.cache.spine_count)
}
pub(crate) unsafe fn toc_count(raw: *mut c_void) -> i32 {
    i32::from(unsafe { owner(raw) }.cache.toc_count)
}
pub(crate) unsafe fn size(raw: *mut c_void) -> u32 {
    let count = unsafe { spine_count(raw) };
    if count == 0 {
        0
    } else {
        unsafe { cumulative_size(raw, count - 1) }
    }
}
pub(crate) unsafe extern "C" fn cumulative_size(raw: *mut c_void, index: i32) -> u32 {
    unsafe { owner(raw) }.cache.cumulative(index)
}
pub(crate) unsafe extern "C" fn spine_for_size(raw: *mut c_void, size: u32) -> i32 {
    unsafe { owner(raw) }.cache.spine_for_size(size)
}
pub(crate) unsafe fn progress(raw: *mut c_void, index: i32, fraction: f32) -> f32 {
    let size = unsafe { size(raw) };
    if size == 0 {
        return 0.0;
    }
    let previous = if index >= 1 {
        unsafe { cumulative_size(raw, index - 1) }
    } else {
        0
    };
    book::progress(
        size as usize,
        previous as usize,
        unsafe { cumulative_size(raw, index) } as usize,
        fraction,
    )
}

fn spine_row(book: &ReaderBook, index: i32) -> cache::Spine {
    let plan = residual::lookup(
        book.cache.loaded,
        index,
        i32::from(book.cache.spine_count),
        false,
    );
    match plan.diagnostic {
        XP_BOOK_LOOKUP_UNLOADED => log::message(
            book.cache.file.leaves,
            1,
            b"EBP",
            format_args!("getSpineItem called but cache not loaded"),
        ),
        XP_BOOK_LOOKUP_RANGE => log::message(
            book.cache.file.leaves,
            1,
            b"EBP",
            format_args!("getSpineItem index:{} is out of range", index),
        ),
        _ => (),
    }
    if plan.index >= 0 {
        if let Ok(row) = book.cache.spine(plan.index) {
            return row;
        }
    }
    cache::Spine {
        href: alloc::vec::Vec::new(),
        cumulative: 0,
        toc_index: -1,
    }
}

pub(crate) unsafe extern "C" fn spine(
    raw: *mut c_void,
    index: i32,
    sink: abi::XpEpubSpineSink,
    context: *mut c_void,
) -> u8 {
    // The row owns its text and the cache guard is gone before the sink can reenter.
    let row = spine_row(unsafe { owner(raw) }, index);
    let value = abi::XpEpubSpine {
        href: app_abi::Text::from_bytes(&row.href),
        bytes: row.cumulative,
        toc_index: row.toc_index,
    };
    unsafe { sink(context, &value) }
}

pub(crate) unsafe extern "C" fn toc(
    raw: *mut c_void,
    index: i32,
    sink: abi::XpEpubTocSink,
    context: *mut c_void,
) -> u8 {
    let book = unsafe { owner(raw) };
    let plan = residual::lookup(
        book.cache.loaded,
        index,
        i32::from(book.cache.toc_count),
        true,
    );
    match plan.diagnostic {
        XP_BOOK_LOOKUP_UNLOADED => log::message(
            book.cache.file.leaves,
            0,
            b"EBP",
            format_args!("getTocItem called but cache not loaded"),
        ),
        XP_BOOK_LOOKUP_RANGE => log::message(
            book.cache.file.leaves,
            0,
            b"EBP",
            format_args!("getTocItem index:{} is out of range", index),
        ),
        _ => (),
    }
    let row = if plan.index >= 0 {
        book.cache.toc(plan.index).ok()
    } else {
        None
    };
    let row = row.unwrap_or(cache::Toc {
        title: alloc::vec::Vec::new(),
        href: alloc::vec::Vec::new(),
        anchor: alloc::vec::Vec::new(),
        level: 0,
        spine_index: -1,
    });
    let value = abi::XpEpubToc {
        title: app_abi::Text::from_bytes(&row.title),
        anchor: app_abi::Text::from_bytes(&row.anchor),
        spine: i32::from(row.spine_index),
        level: i32::from(row.level),
        href: app_abi::Text::from_bytes(&row.href),
    };
    unsafe { sink(context, &value) }
}
pub(crate) unsafe fn toc_for_spine(raw: *mut c_void, index: i32) -> i32 {
    i32::from(spine_row(unsafe { owner(raw) }, index).toc_index)
}

pub(crate) unsafe fn path_text<'a>(raw: *mut c_void) -> &'a super::Text {
    unsafe { &(*raw.cast::<ReaderBook>()).path }
}

pub(crate) unsafe fn path(raw: *mut c_void, sink: app_abi::TextSink, context: *mut c_void) -> u8 {
    let book = unsafe { owner(raw) };
    unsafe { sink(context, app_abi::Text::from_bytes(book.path.bytes())) }
}
pub(crate) unsafe fn cache_path(
    raw: *mut c_void,
    sink: app_abi::TextSink,
    context: *mut c_void,
) -> u8 {
    let book = unsafe { owner(raw) };
    unsafe { sink(context, app_abi::Text::from_bytes(book.cache_path.bytes())) }
}
pub(crate) unsafe fn title(raw: *mut c_void, sink: app_abi::TextSink, context: *mut c_void) -> u8 {
    let book = unsafe { owner(raw) };
    unsafe { sink(context, app_abi::Text::from_bytes(&book.metadata.title)) }
}
pub(crate) unsafe fn author(raw: *mut c_void, sink: app_abi::TextSink, context: *mut c_void) -> u8 {
    let book = unsafe { owner(raw) };
    unsafe { sink(context, app_abi::Text::from_bytes(&book.metadata.author)) }
}
pub(crate) unsafe fn language(
    raw: *mut c_void,
    sink: app_abi::TextSink,
    context: *mut c_void,
) -> u8 {
    let book = unsafe { owner(raw) };
    unsafe { sink(context, app_abi::Text::from_bytes(&book.metadata.language)) }
}

fn items(book: &ReaderBook) -> Items<'_> {
    Items {
        path: &book.path,
        protection: &book.protection,
        resources: book.resources,
        leaves: book.cache.file.leaves,
        now: book.now,
    }
}
pub(crate) unsafe extern "C" fn stream(
    raw: *mut c_void,
    href: app_abi::Text,
    chunk: u32,
    early: u8,
    sink: book::io::Sink,
    context: *mut c_void,
) -> u8 {
    let Some(href) = (unsafe { href.bytes() }) else {
        return 0;
    };
    u8::from(unsafe { items(owner(raw)).stream(href, chunk as usize, early != 0, sink, context) })
}
pub(crate) unsafe extern "C" fn item_size(
    raw: *mut c_void,
    href: app_abi::Text,
    output: *mut u32,
) -> u8 {
    let Some(href) = (unsafe { href.bytes() }) else {
        return 0;
    };
    let Some(size) = items(unsafe { owner(raw) }).size(href) else {
        return 0;
    };
    if !output.is_null() {
        unsafe { output.write(size) };
    }
    1
}

fn find_spine(book: &ReaderBook, target: &[u8], filename: bool) -> i32 {
    struct Context<'a> {
        book: &'a ReaderBook,
        row: Option<cache::Spine>,
    }
    unsafe extern "C" fn get(raw: *mut c_void, index: usize) -> residual::Span {
        let context = unsafe { &mut *raw.cast::<Context<'_>>() };
        context.row = Some(spine_row(context.book, index as i32));
        residual::Span::new(&context.row.as_ref().unwrap().href)
    }
    let mut context = Context { book, row: None };
    unsafe {
        residual::find_spine(
            target,
            usize::from(book.cache.spine_count),
            filename,
            (&mut context as *mut Context<'_>).cast(),
            get,
        )
    }
}
pub(crate) unsafe fn resolve_href(raw: *mut c_void, href: app_abi::Text) -> i32 {
    let Some(href) = (unsafe { href.bytes() }) else {
        return -1;
    };
    let book = unsafe { owner(raw) };
    if !book.cache.loaded {
        return -1;
    }
    let href = &href[..href.iter().position(|&b| b == b'#').unwrap_or(href.len())];
    let mut decoded = alloc::vec::Vec::new();
    if xp_core::epub::decode_href(&mut decoded, href).is_err() {
        return -1;
    }
    let mut target = alloc::vec::Vec::new();
    if xp_core::epub::normalize_href(&mut target, &decoded).is_err() {
        return -1;
    }
    find_spine(book, &target, true)
}
pub(crate) unsafe fn text_reference(raw: *mut c_void) -> i32 {
    let book = unsafe { owner(raw) };
    let leaves = book.cache.file.leaves;
    if !book.cache.loaded {
        log::message(
            leaves,
            1,
            b"EBP",
            format_args!("getSpineIndexForTextReference called but cache not loaded"),
        );
        return 0;
    }
    let mut number = [0; 20];
    let digits = decimal(book.metadata.text_reference.len() as u64, &mut number);
    let cover = book
        .metadata
        .cover
        .split(|&b| b == 0)
        .next()
        .unwrap_or_default();
    let reference = book
        .metadata
        .text_reference
        .split(|&b| b == 0)
        .next()
        .unwrap_or_default();
    log::with_tail(
        leaves,
        0,
        b"EBP",
        format_args!("Core Metadata: cover({})=", book.metadata.cover.len()),
        &[cover, b", textReference(", digits, b")=", reference],
    );
    let index = find_spine(book, &book.metadata.text_reference, false);
    if index >= 0 {
        log::with_bytes(
            leaves,
            0,
            b"EBP",
            &[b"Text reference ", &book.metadata.text_reference],
            format_args!(" found at index {}", index),
        );
        index
    } else {
        if index == -1 {
            log::message(
                leaves,
                0,
                b"EBP",
                format_args!("Section not found for text reference"),
            );
        }
        0
    }
}

fn cache_services(book: &ReaderBook) -> book::metadata::Services {
    use book::metadata::{Diagnostic as D, Operation as O};
    unsafe extern "C" fn operation(raw: *mut c_void, operation: u32) -> u8 {
        let book = unsafe { owner(raw) };
        let path = book.cache_path.wire();
        unsafe {
            match operation {
                x if x == O::CacheExists as u32 => (book.resources.path_exists)(path),
                x if x == O::CacheRemove as u32 => (book.cache.file.leaves.path_remove_tree)(path),
                x if x == O::CacheMkdir as u32 => (book.cache.file.leaves.path_mkdir)(path),
                _ => 0,
            }
        }
    }
    unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32) {
        let leaves = unsafe { owner(raw) }.cache.file.leaves;
        match event {
            x if x == D::CacheAbsent as u32 => log::message(
                leaves,
                0,
                b"EPB",
                format_args!("Cache does not exist, no action needed"),
            ),
            x if x == D::CacheClearFailed as u32 => {
                log::message(leaves, 1, b"EPB", format_args!("Failed to clear cache"))
            }
            x if x == D::CacheCleared as u32 => log::message(
                leaves,
                0,
                b"EPB",
                format_args!("Cache cleared successfully"),
            ),
            _ => (),
        }
    }
    book::metadata::Services {
        ctx: (book as *const ReaderBook).cast_mut().cast(),
        operation,
        diagnostic: Some(diagnostic),
    }
}
pub(crate) unsafe fn setup_cache(raw: *mut c_void) {
    unsafe { book::metadata::setup_cache(&cache_services(owner(raw))) };
}
pub(crate) unsafe fn clear_cache(raw: *mut c_void) {
    unsafe { book::metadata::clear_cache(&cache_services(owner(raw))) };
}

pub(crate) fn decimal(mut value: u64, bytes: &mut [u8; 20]) -> &[u8] {
    let mut start = bytes.len();
    loop {
        start -= 1;
        bytes[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            return &bytes[start..];
        }
    }
}

pub(crate) unsafe fn book_view(raw: *mut c_void) -> app_abi::Book {
    let book = unsafe { owner(raw) };
    app_abi::Book {
        path: book.path.wire(),
        title: app_abi::Text::from_bytes(&book.metadata.title),
        author: app_abi::Text::from_bytes(&book.metadata.author),
        cover: app_abi::Text::from_bytes(&[]),
    }
}
pub(crate) unsafe fn cache_bytes<'a>(raw: *mut c_void) -> &'a [u8] {
    unsafe { owner(raw) }.cache_path.bytes()
}
