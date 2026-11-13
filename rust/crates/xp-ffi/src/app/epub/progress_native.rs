use super::{
    abi, app_abi,
    book_owner::{queries, xml_error},
    progress_mapping::{self, Chapter, Diagnostic, Source},
    section_owner::CachedSection,
    EpubSession, ResourceAccess, Text,
};
use core::{ffi::c_void, ptr::NonNull};
use xp_core::sync::mapping::{Position, Query};

struct Native<'a, 'lock> {
    parent: &'a EpubSession,
    _access: &'a ResourceAccess<'lock>,
    book: NonNull<c_void>,
    section: Option<CachedSection>,
    section_spine: i32,
}
struct Loan<'a, 'parent, 'lock> {
    source: &'a Native<'parent, 'lock>,
    href: Text,
}
unsafe extern "C" fn receive_href(context: *mut c_void, row: *const abi::XpEpubSpine) -> u8 {
    let out = unsafe { &mut *context.cast::<Option<Text>>() };
    *out = unsafe { (*row).href.bytes() }.and_then(|bytes| Text::copy(bytes).ok());
    u8::from(out.is_some())
}

unsafe extern "C" fn consume_chunk(context: *mut c_void, bytes: *const u8, length: usize) -> u8 {
    let consume = unsafe { &mut *context.cast::<&mut dyn FnMut(&[u8])>() };
    let bytes = if length == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(bytes, length) }
    };
    consume(bytes);
    1
}

impl Native<'_, '_> {
    fn href(&self, spine: i32) -> Option<Text> {
        let mut href: Option<Text> = None;
        unsafe {
            queries::spine(
                self.book.as_ptr(),
                spine,
                receive_href,
                (&mut href as *mut Option<Text>).cast(),
            )
        };
        href.filter(|text| !text.is_empty())
    }
    fn read(&self, href: &Text, consume: &mut dyn FnMut(&[u8])) -> bool {
        let mut consume = consume;
        unsafe {
            queries::stream(
                self.book.as_ptr(),
                href.wire(),
                1024,
                0,
                consume_chunk,
                (&mut consume as *mut &mut dyn FnMut(&[u8])).cast(),
            ) != 0
        }
    }
}
impl Chapter for Loan<'_, '_, '_> {
    fn read(&self, consume: &mut dyn FnMut(&[u8])) -> bool {
        self.source.read(&self.href, consume)
    }
}
impl<'parent, 'lock> Source for Native<'parent, 'lock> {
    type Chapter<'a>
        = Loan<'a, 'parent, 'lock>
    where
        Self: 'a;
    fn spine_count(&self) -> i32 {
        unsafe { queries::spine_count(self.book.as_ptr()) }
    }
    fn chapter(&self, spine: i32) -> Option<Self::Chapter<'_>> {
        Some(Loan {
            source: self,
            href: self.href(spine)?,
        })
    }
    #[inline(never)]
    fn query(&mut self, kind: u32, spine: i32, arg: usize, text: &[u8]) -> i64 {
        match kind {
            x if x == Query::BookSize as u32 => {
                return unsafe { queries::size(self.book.as_ptr()).into() }
            }
            x if x == Query::SpineCount as u32 => return self.spine_count().into(),
            x if x == Query::SpineForSize as u32 => {
                return unsafe { queries::spine_for_size(self.book.as_ptr(), arg as u32).into() }
            }
            x if x == Query::CumulativeSize as u32 => {
                return unsafe {
                    queries::cumulative_size(self.book.as_ptr(), spine).map_or(-1, i64::from)
                }
            }
            x if x == Query::ItemSize as u32 => {
                let Some(href) = self.href(spine) else {
                    return -1;
                };
                let mut size = 0;
                return if unsafe { queries::item_size(self.book.as_ptr(), href.wire(), &mut size) }
                    != 0
                {
                    size.into()
                } else {
                    -1
                };
            }
            _ => {}
        }
        if self.section.is_none() || self.section_spine != spine {
            self.section = None;
            self.section = unsafe {
                CachedSection::open(self.book, spine, self.parent.resources, self.parent.leaves)
            };
            self.section_spine = spine;
        }
        let Some(section) = &self.section else {
            return -1;
        };
        use crate::section_cache::Query as Cache;
        let result = match kind {
            x if x == Query::CachedPages as u32 => section.query(Cache::PageCount),
            x if x == Query::PageForOffset as u32 => text.try_into().ok().and_then(|bytes| {
                section.query(Cache::PageForVisible {
                    offset: u32::from_ne_bytes(bytes),
                    first: arg != 0,
                })
            }),
            x if x == Query::PageForParagraph as u32 => {
                section.query(Cache::PageForParagraph(arg as u16 as u32))
            }
            x if x == Query::PageForList as u32 => {
                section.query(Cache::PageForList(arg as u16 as u32))
            }
            x if x == Query::PageForAnchor as u32 => section.anchor(text),
            _ => None,
        };
        result.map_or(-1, i64::from)
    }
    fn progress(&self, spine: i32, fraction: f32) -> Option<f32> {
        let progress = unsafe { queries::progress(self.book.as_ptr(), spine, fraction) };
        if progress.is_none() {
            self.parent
                .loop_log_parts(1, &[b"Unreadable size window while saving mapped progress"]);
        }
        progress
    }
    fn entity(data: &[u8]) -> i32 {
        let Some(entity) = crate::book_policy::entity(data) else {
            return -1;
        };
        let (mut offset, mut count) = (0, 0);
        while xp_core::utf8::next(entity.to_bytes(), &mut offset).is_some() {
            count += 1;
        }
        count
    }
    fn log(&self, diagnostic: Diagnostic<'_>) {
        let severity = u8::from(matches!(
            diagnostic,
            Diagnostic::ParserAllocation | Diagnostic::Xml { .. }
        ));
        if unsafe { (self.parent.leaves.log_enabled)(severity) } == 0 {
            return;
        }
        progress_mapping::diagnostics::emit(
            diagnostic,
            xml_error,
            |severity, origin, message| unsafe {
                (self.parent.leaves.log)(
                    severity,
                    app_abi::Text::from_bytes(origin),
                    app_abi::Text::from_bytes(message),
                );
            },
        );
    }
    fn reverse(&mut self, spine: i32, consume: &mut dyn FnMut(&[u8])) -> bool {
        let href = self.href(spine);
        self.section = None;
        href.is_some_and(|href| self.read(&href, consume))
    }
}
fn input(value: &abi::XpEpubPosition) -> Position {
    Position {
        spine: value.spine,
        page: value.page,
        pages: value.count,
        offset: value.visible_offset,
        has_offset: value.has_visible_offset,
        paragraph: value.paragraph,
        has_paragraph: value.has_paragraph,
        li: value.list_item,
        has_li: value.has_list_item,
        anchor: value.anchor,
        resolved: value.resolved_spine,
        mapped: value.mapped_page,
    }
}
impl EpubSession {
    /// The session retains the Rust book and excludes render for the complete mapping loan.
    #[inline(never)]
    pub(super) unsafe fn saved_progress(
        &self,
        access: &ResourceAccess<'_>,
        book: NonNull<c_void>,
        position: &abi::XpEpubPosition,
    ) -> Option<progress_mapping::Saved> {
        self.check_resources(access);
        progress_mapping::save(
            &mut Native {
                parent: self,
                _access: access,
                book,
                section: None,
                section_spine: -1,
            },
            &input(position),
        )
    }
    /// All section, chapter and XPath loans end before the resource guard is released.
    #[inline(never)]
    pub(super) unsafe fn mapped_progress(
        &self,
        access: &ResourceAccess<'_>,
        book: NonNull<c_void>,
        saved: &abi::XpEpubSavedProgress,
        current: i32,
        pages: i32,
    ) -> Option<Position> {
        self.check_resources(access);
        progress_mapping::map(
            &mut Native {
                parent: self,
                _access: access,
                book,
                section: None,
                section_spine: -1,
            },
            unsafe { saved.xpath.bytes() }.unwrap_or_default(),
            saved.percentage,
            current,
            pages,
        )
    }
}

#[cfg(feature = "app-test")]
#[path = "progress_native_tests.rs"]
mod tests;
