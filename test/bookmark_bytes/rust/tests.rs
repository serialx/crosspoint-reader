#[path = "../../../rust/crates/xp-ffi/src/app/bookmarks/mod.rs"]
pub mod bookmarks;

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
        assert_eq!(rows[0].xpath, b"first");
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
}
