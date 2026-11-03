extern crate alloc;

use super::{
    abi, app_abi,
    book::{with_book_text, with_text},
    EpubSession,
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr::NonNull};

// Loop-owned data retained by one Selection result; render never borrows it.
#[derive(Default)]
pub(super) struct SelectionMetadata {
    bytes: Vec<u8>,
    chapter_end: usize,
    title_end: usize,
}

impl SelectionMetadata {
    fn copy(chapter: &[u8], title: &[u8], author: &[u8]) -> Option<Self> {
        let length = chapter
            .len()
            .checked_add(title.len())?
            .checked_add(author.len())?
            .checked_add(3)?;
        let mut bytes = Vec::new();
        // One checked allocation replaces the three native callback-captured strings.
        bytes.try_reserve_exact(length).ok()?;
        bytes.extend_from_slice(chapter);
        bytes.push(0);
        let chapter_end = bytes.len();
        bytes.extend_from_slice(title);
        bytes.push(0);
        let title_end = bytes.len();
        bytes.extend_from_slice(author);
        bytes.push(0);
        Some(Self {
            bytes,
            chapter_end,
            title_end,
        })
    }

    fn from_chapter(
        _owner: &EpubSession,
        book: NonNull<c_void>,
        chapter: app_abi::Text,
    ) -> Option<Self> {
        // Both getters return immutable book metadata. All three views stay inside
        // their synchronous sinks; no native pointer survives this call.
        with_book_text(book, super::session_book::title, |title| {
            with_book_text(book, super::session_book::author, |author| {
                Self::copy(
                    unsafe { chapter.bytes() }?,
                    unsafe { title.bytes() }?,
                    unsafe { author.bytes() }?,
                )
            })
            .flatten()
        })
        .flatten()
    }

    pub(super) fn capture(owner: &EpubSession, book: NonNull<c_void>, toc: i32) -> Option<Self> {
        if toc < 0 {
            return Self::from_chapter(owner, book, app_abi::Text::from_bytes(&b"\0"[..0]));
        }
        struct Sink {
            sink: app_abi::TextSink,
            context: *mut c_void,
        }
        unsafe extern "C" fn title(context: *mut c_void, row: *const abi::XpEpubToc) -> u8 {
            let capture = &*context.cast::<Sink>();
            let Some(row) = row.as_ref() else { return 0 };
            (capture.sink)(capture.context, row.title)
        }
        with_text(
            |sink, context| {
                let mut capture = Sink { sink, context };
                unsafe {
                    (super::session_book::toc)(
                        book.as_ptr(),
                        toc,
                        title,
                        (&mut capture as *mut Sink).cast(),
                    )
                }
            },
            |chapter| Self::from_chapter(owner, book, chapter),
        )
        .flatten()
    }

    fn part(&self, begin: usize, end: usize) -> app_abi::Text {
        if end <= begin {
            return app_abi::Text::from_bytes(&b"\0"[..0]);
        }
        app_abi::Text::from_bytes(&self.bytes[begin..end - 1])
    }

    pub(super) fn chapter(&self) -> app_abi::Text {
        self.part(0, self.chapter_end)
    }
    pub(super) fn title(&self) -> app_abi::Text {
        self.part(self.chapter_end, self.title_end)
    }
    pub(super) fn author(&self) -> app_abi::Text {
        self.part(self.title_end, self.bytes.len())
    }
}
