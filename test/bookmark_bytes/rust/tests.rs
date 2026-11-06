#[allow(dead_code)]
#[path = "../../../rust/crates/xp-ffi/src/app/abi.rs"]
mod abi;
#[allow(dead_code)]
#[path = "../../../rust/crates/xp-ffi/src/app/resources.rs"]
mod resources;
pub use resources::Text;
#[allow(dead_code)]
#[path = "../../../rust/crates/xp-ffi/src/app/result.rs"]
mod result;
mod epub {
    pub mod abi {
        pub enum XpEpubLeaves {}
    }
}
#[path = "../../../rust/crates/xp-ffi/src/app/bookmarks/mod.rs"]
pub mod bookmarks;
#[path = "../../../rust/crates/xp-ffi/src/app/clippings/io.rs"]
pub mod clipping_io;
mod clippings {
    pub use crate::clipping_io as io;
}
#[cfg(test)]
mod export_tests;
#[cfg(test)]
mod persistence_tests;

#[cfg(test)]
mod tests {
    use super::bookmarks::{decode, encode, Error};
    use std::{
        alloc::{GlobalAlloc, Layout, System},
        cell::Cell,
    };

    thread_local! {
        static FAIL_AT: Cell<Option<usize>> = const { Cell::new(None) };
        static ATTEMPTS: Cell<usize> = const { Cell::new(0) };
        static LIVE_BYTES: Cell<isize> = const { Cell::new(0) };
    }
    struct Heap;
    #[global_allocator]
    static HEAP: Heap = Heap;
    unsafe impl GlobalAlloc for Heap {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            let fail = FAIL_AT.with(|fail| {
                ATTEMPTS.with(|count| count.set(count.get() + 1));
                match fail.get() {
                    Some(0) => {
                        fail.set(None);
                        true
                    }
                    Some(n) => {
                        fail.set(Some(n - 1));
                        false
                    }
                    None => false,
                }
            });
            if fail {
                return core::ptr::null_mut();
            }
            let pointer = unsafe { System.alloc(layout) };
            if !pointer.is_null() {
                LIVE_BYTES.with(|live| live.set(live.get() + layout.size() as isize));
            }
            pointer
        }
        unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
            LIVE_BYTES.with(|live| live.set(live.get() - layout.size() as isize));
            unsafe { System.dealloc(pointer, layout) };
        }
    }

    const TWO: &[u8] = br#"{"bookmarks":[{"xpath":"first","summary":"summary","name":"name","vo":0},{"xpath":"second","name":"last","vo":null}]}"#;

    #[test]
    fn record_labels_are_terminated_in_their_final_storage() {
        let rows = decode::<Vec<u8>>(TWO).unwrap();
        for row in &rows {
            for text in [&row.xpath, &row.summary] {
                let wire = text.wire();
                assert_eq!(unsafe { *wire.data.add(wire.len) }, 0);
                assert_eq!(unsafe { wire.bytes() }.unwrap(), text.bytes());
            }
        }
    }

    #[test]
    fn failed_record_and_text_allocations_release_every_partial_record() {
        let rows = decode::<Vec<u8>>(TWO).unwrap();
        assert_eq!(rows.len(), 2);
        drop(rows);
        ATTEMPTS.with(|n| n.set(0));
        drop(decode::<Vec<u8>>(TWO).unwrap());
        let count = ATTEMPTS.with(Cell::get);
        assert!(count > 1);
        for fail in 0..count {
            let before = LIVE_BYTES.with(Cell::get);
            FAIL_AT.with(|n| n.set(Some(fail)));
            let result = decode::<Vec<u8>>(TWO);
            FAIL_AT.with(|n| n.set(None));
            assert!(
                matches!(result, Err(Error::Allocation)),
                "allocation {fail}/{count}"
            );
            drop(result);
            assert_eq!(LIVE_BYTES.with(Cell::get), before);
        }
    }

    #[test]
    fn writer_reserves_once_and_reports_denial_without_changing_records() {
        let rows = decode::<Vec<u8>>(TWO).unwrap();
        let before = LIVE_BYTES.with(Cell::get);
        FAIL_AT.with(|n| n.set(Some(0)));
        let result = encode(&rows);
        FAIL_AT.with(|n| n.set(None));
        assert_eq!(result, Err(Error::Allocation));
        assert_eq!(LIVE_BYTES.with(Cell::get), before);
        ATTEMPTS.with(|n| n.set(0));
        let output = encode(&rows).unwrap();
        assert_eq!(ATTEMPTS.with(Cell::get), 1);
        assert_eq!(rows[0].xpath.bytes(), b"first");
        assert_eq!(rows[1].name, b"last");
        assert!(output.starts_with(b"{\"bookmarks\":["));
    }

    #[test]
    fn malformed_input_is_rejected_before_allocating_records() {
        ATTEMPTS.with(|n| n.set(0));
        let result = decode::<Vec<u8>>(br#"{"bookmarks":[{"name":"valid"},{"name":"truncated"#);
        assert!(matches!(result, Err(Error::IncompleteInput)));
        assert_eq!(ATTEMPTS.with(Cell::get), 0);
    }

    #[test]
    fn invalid_escape_is_reported_before_later_truncation() {
        // JsonDeserializer::parseQuotedString validates escapes as it reads.
        for bytes in [br#""\q"#.as_slice(), br#""\u12""#, br#""\u12x"#] {
            assert!(matches!(decode::<Vec<u8>>(bytes), Err(Error::InvalidInput)));
        }
        assert!(matches!(
            decode::<Vec<u8>>(br#""\u12"#),
            Err(Error::IncompleteInput)
        ));
    }

    #[test]
    fn growth_denial_preserves_existing_rows_then_front_insert_moves_them() {
        use super::bookmarks::mutations;
        let mut rows = decode::<Vec<u8>>(TWO).unwrap();
        rows.shrink_to_fit();
        assert_eq!(rows.len(), rows.capacity());
        let live_before = LIVE_BYTES.with(Cell::get);
        let row = decode::<Vec<u8>>(br#"{"bookmarks":[{"name":"new"}]}"#)
            .unwrap()
            .remove(0);
        FAIL_AT.with(|n| n.set(Some(0)));
        let result = mutations::insert_front(&mut rows, row);
        FAIL_AT.with(|n| n.set(None));
        assert_eq!(result, Err(Error::Allocation));
        assert_eq!(LIVE_BYTES.with(Cell::get), live_before);
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].xpath.bytes(), b"first");
        let row = decode::<Vec<u8>>(br#"{"bookmarks":[{"name":"new"}]}"#)
            .unwrap()
            .remove(0);
        mutations::insert_front(&mut rows, row).unwrap();
        assert_eq!(rows.len(), 3);
        assert_eq!(rows[0].name, b"new");
        assert_eq!(rows[1].xpath.bytes(), b"first");
        assert_eq!(rows[2].xpath.bytes(), b"second");
        assert_eq!(
            mutations::retain(&mut rows, |row| row.xpath.bytes() != b"first"),
            1
        );
        assert_eq!(rows[1].xpath.bytes(), b"second");
    }

    #[test]
    fn export_allocation_denial_has_no_partial_buffer_or_leak() {
        let before = LIVE_BYTES.with(Cell::get);
        FAIL_AT.with(|n| n.set(Some(0)));
        let result =
            super::bookmarks::export::bytes(b"title", b"author", b"chapter", 7, b"text", None);
        FAIL_AT.with(|n| n.set(None));
        assert_eq!(result, Err(Error::Allocation));
        assert_eq!(LIVE_BYTES.with(Cell::get), before);
    }
}
