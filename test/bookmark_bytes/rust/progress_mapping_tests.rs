use crate::progress_mapping::{self, Chapter, Diagnostic, Source};
use std::cell::Cell;
use xp_core::sync::mapping::{Position, Query};

struct Book {
    xml: &'static [u8],
    fail_read: bool,
    active: Cell<u32>,
    reads: Cell<u32>,
    diagnostics: Cell<u32>,
}
impl Default for Book {
    fn default() -> Self {
        Self {
            xml: b"<html><body><p>hello world</p></body></html>",
            fail_read: false,
            active: Cell::new(0),
            reads: Cell::new(0),
            diagnostics: Cell::new(0),
        }
    }
}
struct Loan<'a>(&'a Book);
impl Drop for Loan<'_> {
    fn drop(&mut self) {
        self.0.active.set(self.0.active.get() - 1);
    }
}
impl Chapter for Loan<'_> {
    fn read(&self, consume: &mut dyn FnMut(&[u8])) -> bool {
        self.0.reads.set(self.0.reads.get() + 1);
        if self.0.fail_read {
            return false;
        }
        for chunk in self.0.xml.chunks(3) {
            consume(chunk);
        }
        true
    }
}
impl Source for Book {
    type Chapter<'a> = Loan<'a>;
    fn spine_count(&self) -> i32 {
        2
    }
    fn chapter(&self, spine: i32) -> Option<Loan<'_>> {
        if !(0..2).contains(&spine) {
            return None;
        }
        self.active.set(self.active.get() + 1);
        Some(Loan(self))
    }
    fn query(&mut self, kind: u32, spine: i32, arg: usize, text: &[u8]) -> i64 {
        match kind {
            x if x == Query::BookSize as u32 => 200,
            x if x == Query::SpineCount as u32 => 2,
            x if x == Query::SpineForSize as u32 => (arg / 100).min(1) as i64,
            x if x == Query::CumulativeSize as u32 => i64::from(spine + 1) * 100,
            x if x == Query::ItemSize as u32 => self.xml.len() as i64,
            x if x == Query::CachedPages as u32 => 10,
            x if x == Query::PageForOffset as u32 => {
                u32::from_ne_bytes(text.try_into().unwrap()) as i64 / 5
            }
            _ => -1,
        }
    }
    fn progress(&self, spine: i32, fraction: f32) -> Option<f32> {
        Some((spine as f32 + fraction) / 2.0)
    }
    fn entity(_: &[u8]) -> i32 {
        -1
    }
    fn log(&self, diagnostic: Diagnostic<'_>) {
        self.diagnostics.set(self.diagnostics.get() + 1);
        match diagnostic {
            Diagnostic::Resolved { path, .. } => {
                assert_eq!(self.active.get(), 1);
                assert!(path.len() < 256);
            }
            Diagnostic::Saved { path, .. } => {
                assert_eq!(self.active.get(), 0);
                assert!(!path.is_empty());
            }
            _ => {}
        }
    }
    fn reverse(&mut self, spine: i32, consume: &mut dyn FnMut(&[u8])) -> bool {
        self.chapter(spine)
            .is_some_and(|chapter| chapter.read(consume))
    }
}
#[test]
fn progress_adapter_saves_owned_xpath_and_ends_every_chapter_loan() {
    let mut book = Book::default();
    let input = Position {
        spine: 0,
        page: 1,
        pages: 2,
        offset: 6,
        has_offset: 1,
        ..Default::default()
    };
    let saved = progress_mapping::save(&mut book, &input).unwrap();
    assert_eq!(saved.percentage, 0.5);
    assert_eq!(
        saved.xpath.bytes(),
        b"/body/DocFragment[1]/body/p[1]/text()[1].6"
    );
    assert_eq!(book.reads.get(), 1);
    assert_eq!(book.active.get(), 0);
    book.xml = b"discarded";
    assert_eq!(book.xml, b"discarded");
    assert!(saved.xpath.bytes().ends_with(b"text()[1].6"));
}
#[test]
fn progress_adapter_retains_both_passes_and_maps_the_borrowed_saved_path() {
    let mut book = Book::default();
    let input = Position {
        spine: 1,
        page: 1,
        pages: 3,
        ..Default::default()
    };
    let saved = progress_mapping::save(&mut book, &input).unwrap();
    assert_eq!(saved.percentage, 0.75);
    assert_eq!(book.reads.get(), 2);
    let output = progress_mapping::map(&mut book, saved.xpath.bytes(), saved.percentage, 0, 10).unwrap();
    assert_eq!(output.spine, 1);
    assert_eq!(output.page, 1);
    assert_eq!(book.active.get(), 0);
}
#[test]
fn progress_adapter_failed_stream_and_invalid_xml_use_the_native_fallback() {
    for (fail_read, xml) in [
        (true, b"<body/>".as_slice()),
        (false, b"<body>broken".as_slice()),
    ] {
        let mut book = Book {
            fail_read,
            xml,
            ..Default::default()
        };
        let input = Position {
            spine: 0,
            page: 1,
            pages: 2,
            offset: 1,
            has_offset: 1,
            ..Default::default()
        };
        let saved = progress_mapping::save(&mut book, &input).unwrap();
        assert!(saved
            .xpath
            .bytes()
            .starts_with(b"/body/DocFragment[1]/body"));
        assert_eq!(book.active.get(), 0);
    }
}

#[test]
fn progress_adapter_releases_partial_parsers_and_paths_on_each_allocation_failure() {
    for has_offset in [0, 1] {
        crate::tests::allocation_failures(|| {
            let mut book = Book::default();
            let input = Position {
                spine: 0,
                page: 1,
                pages: 3,
                offset: 6,
                has_offset,
                ..Default::default()
            };
            drop(progress_mapping::save(&mut book, &input));
            assert_eq!(book.active.get(), 0);
        });
    }
}
