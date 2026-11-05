//! Section preparation reads real book rows after the cache guard has been released.
extern crate alloc;
use super::{
    super::{
        abi::{XpEpubLeaves, XpEpubToc},
        book_owner::{css, queries},
        Text,
    },
    services::Paths,
};
use crate::{app::abi, css::Css};
use alloc::{boxed::Box, vec::Vec};
use core::{ffi::c_void, ptr::NonNull};

#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum CssStatus {
    Complete,
    LowMemory,
    Invalid,
}

pub(super) fn load_css(
    owner: &mut Option<Box<Css>>,
    cache: &Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
) -> CssStatus {
    match css::run(
        owner,
        cache,
        resources,
        leaves,
        css::Action::Load,
        true,
        None,
    ) {
        0 => CssStatus::Complete,
        1 => CssStatus::LowMemory,
        _ => CssStatus::Invalid,
    }
}

pub(super) unsafe fn toc(book: NonNull<c_void>, spine: i32) -> Result<Vec<Text>, ()> {
    struct Row {
        spine: i32,
        nonempty: bool,
    }
    unsafe extern "C" fn count(raw: *mut c_void, row: *const XpEpubToc) -> u8 {
        let out = unsafe { &mut *raw.cast::<Row>() };
        let row = unsafe { &*row };
        out.spine = row.spine;
        out.nonempty = row.anchor.len != 0;
        1
    }
    let first = unsafe { queries::toc_for_spine(book.as_ptr(), spine) };
    let mut result = Vec::new();
    if first < 0 {
        return Ok(result);
    }
    let total = unsafe { queries::toc_count(book.as_ptr()) };
    let mut end = first;
    let mut capacity = 0usize;
    while end < total {
        let mut row = Row {
            spine: -1,
            nonempty: false,
        };
        if unsafe { queries::toc(book.as_ptr(), end, count, (&mut row as *mut Row).cast()) } == 0 {
            return Err(());
        }
        if row.spine != spine {
            break;
        }
        capacity += usize::from(row.nonempty);
        end += 1;
    }
    // Exactly the same-spine upper bound; no doubling or full-book anchor clone.
    result.try_reserve_exact(capacity).map_err(|_| ())?;
    unsafe extern "C" fn copy(raw: *mut c_void, row: *const XpEpubToc) -> u8 {
        let output = unsafe { &mut *raw.cast::<Vec<Text>>() };
        let Some(bytes) = (unsafe { (*row).anchor.bytes() }) else {
            return 0;
        };
        if !bytes.is_empty() {
            let Ok(anchor) = Text::copy(bytes) else {
                return 0;
            };
            if output.len() == output.capacity() {
                return 0;
            }
            output.push(anchor);
        }
        1
    }
    for index in first..end {
        if unsafe {
            queries::toc(
                book.as_ptr(),
                index,
                copy,
                (&mut result as *mut Vec<Text>).cast(),
            )
        } == 0
        {
            return Err(());
        }
    }
    Ok(result)
}

impl Paths {
    pub(super) unsafe fn prepare(book: NonNull<c_void>, spine: i32) -> Result<(Self, Text), ()> {
        unsafe extern "C" fn receive(
            raw: *mut c_void,
            row: *const super::super::abi::XpEpubSpine,
        ) -> u8 {
            let output = unsafe { &mut *raw.cast::<Text>() };
            let Some(bytes) = (unsafe { (*row).href.bytes() }) else {
                return 0;
            };
            output.assign(bytes).is_ok().into()
        }
        let mut href = Text::default();
        if unsafe {
            queries::spine(
                book.as_ptr(),
                spine,
                receive,
                (&mut href as *mut Text).cast(),
            )
        } == 0
        {
            return Err(());
        }
        let cache = unsafe { queries::cache_bytes(book.as_ptr()) };
        let mut digits = [0; 20];
        let decimal = queries::decimal(u64::from(spine.unsigned_abs()), &mut digits);
        let sign = if spine < 0 { &b"-"[..] } else { &b""[..] };
        fn join(parts: &[&[u8]]) -> Result<Text, ()> {
            let mut text = Text::default();
            text.assign_parts(parts).map_err(|_| ())?;
            Ok(text)
        }
        let final_cache = join(&[cache, b"/sections/", sign, decimal, b".bin"])?;
        let temporary_cache = join(&[final_cache.bytes(), b".part"])?;
        let html = join(&[cache, b"/html/", sign, decimal, b".html"])?;
        let temporary_html = join(&[cache, b"/html/.tmp_", sign, decimal, b".html"])?;
        let content_end = href
            .bytes()
            .iter()
            .rposition(|&c| c == b'/')
            .map_or(0, |at| at + 1);
        let content = Text::copy(&href.bytes()[..content_end]).map_err(|_| ())?;
        let image = join(&[cache, b"/img_", sign, decimal, b"_"])?;
        Ok((
            Self {
                parse: Text::default(),
                content,
                image,
                final_cache,
                temporary_cache,
                html,
                temporary_html,
            },
            href,
        ))
    }

    pub(super) fn select_html(&mut self, cached: bool) -> Result<(), ()> {
        self.parse
            .assign(if cached {
                self.html.bytes()
            } else {
                self.temporary_html.bytes()
            })
            .map_err(|_| ())
    }
}
