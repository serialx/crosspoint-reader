extern crate alloc;

#[path = "../../../rust/crates/xp-ffi/src/app/clippings/mod.rs"]
pub mod clippings;
#[path = "../../../rust/crates/xp-ffi/src/app/clippings/service.rs"]
#[cfg(test)]
mod service;

#[cfg(test)]
mod volume;

#[cfg(test)]
mod tests {
    use super::{
        clippings::{
            record::{Index, Record},
            AddResult, Store,
        },
        volume::Volume,
    };
    use std::alloc::{GlobalAlloc, Layout, System};
    use std::cell::Cell;

    thread_local! {
        static FAIL_AFTER: Cell<Option<usize>> = const { Cell::new(None) };
        static LIVE: Cell<isize> = const { Cell::new(0) };
        static SLAB_FAIL_AFTER: Cell<Option<usize>> = const { Cell::new(None) };
    }

    struct Heap;
    #[global_allocator]
    static HEAP: Heap = Heap;
    unsafe impl GlobalAlloc for Heap {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            if layout.size() == 2560
                && SLAB_FAIL_AFTER.with(|left| match left.get() {
                    Some(0) => true,
                    Some(n) => {
                        left.set(Some(n - 1));
                        false
                    }
                    None => false,
                })
            {
                return core::ptr::null_mut();
            }
            if FAIL_AFTER.with(|left| match left.get() {
                Some(0) => true,
                Some(n) => {
                    left.set(Some(n - 1));
                    false
                }
                None => false,
            }) {
                return core::ptr::null_mut();
            }
            let pointer = unsafe { System.alloc(layout) };
            if !pointer.is_null() {
                let _ = LIVE.try_with(|n| n.set(n.get() + 1));
            }
            pointer
        }
        unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
            let _ = LIVE.try_with(|n| n.set(n.get() - 1));
            unsafe {
                System.dealloc(pointer, layout);
            }
        }
    }

    #[test]
    fn compact_record_preserves_defaults_and_raw_pending() {
        assert_eq!(core::mem::size_of::<Record>(), 160);
        let mut record = Record::default();
        assert_eq!(record.page_count, 1);
        assert_eq!(record.paragraph, u16::MAX);
        assert_eq!(record.start_offset, u32::MAX);
        assert_eq!(record.end_offset, u32::MAX);
        assert_eq!(record.pending, 1);
        record.pending = 7;
        assert_eq!(record.pending, 7);
    }

    #[test]
    fn slab_growth_is_fallible_and_does_not_move_records() {
        let before = LIVE.with(Cell::get);
        let mut index = Index::<16>::new();
        assert!(index.reserve(16));
        index.slot_mut(0).timestamp = 42;
        let first = index.slot(0) as *const Record;
        FAIL_AFTER.with(|n| n.set(Some(0)));
        let failed = !index.reserve(17);
        FAIL_AFTER.with(|n| n.set(None));
        assert!(failed);
        assert_eq!(index.capacity(), 16);
        assert_eq!(index.slot(0).timestamp, 42);
        assert!(index.reserve(256));
        assert!(!index.reserve(257));
        assert_eq!(first, index.slot(0) as *const Record);
        assert_eq!(LIVE.with(Cell::get) - before, 16);
        index.clear();
        assert_eq!(index.capacity(), 0);
        assert_eq!(LIVE.with(Cell::get), before);
    }

    #[test]
    fn partial_reserve_can_be_retried_and_s3_limit_is_1024() {
        let mut index = Index::<64>::new();
        FAIL_AFTER.with(|n| n.set(Some(1)));
        let failed = !index.reserve(33);
        FAIL_AFTER.with(|n| n.set(None));
        assert!(failed);
        assert_eq!(index.capacity(), 16);
        assert!(index.reserve(1024));
        assert!(!index.reserve(1025));
    }

    #[test]
    fn owner_allocation_failure_and_move_drop_once() {
        let volume = Volume::new();
        let io = volume.clone();
        FAIL_AFTER.with(|n| n.set(Some(0)));
        let failed = Store::<_, 16>::try_new(io).is_none();
        FAIL_AFTER.with(|n| n.set(None));
        assert!(failed);
        let before = LIVE.with(Cell::get);
        let owner = Store::<_, 16>::try_new(volume).unwrap();
        assert_eq!(LIVE.with(Cell::get), before + 1);
        let moved = Some(owner);
        assert_eq!(moved.as_ref().unwrap().count(), 0);
        drop(moved);
        // The owner and the final Volume Rc are released together.
        assert_eq!(LIVE.with(Cell::get), before - 1);
    }

    #[test]
    fn offset_oom_occurs_before_any_filesystem_mutation() {
        let volume = Volume::new();
        let mut store = Store::<_, 16>::try_new(volume.clone()).unwrap();
        assert!(store.load(b"/a.epub", b"A", b"", b"epub"));
        assert_eq!(store.add(Record::default(), b"C", b"one"), AddResult::Added);
        let original = volume.source();
        let writes = volume.operations();
        FAIL_AFTER.with(|n| n.set(Some(0)));
        let result = store.add(Record::default(), b"C", b"two");
        FAIL_AFTER.with(|n| n.set(None));
        assert_eq!(result, AddResult::SaveFailed);
        assert_eq!(store.count(), 1);
        assert_eq!(volume.operations(), writes);
        assert_eq!(volume.source(), original);
    }

    #[test]
    fn partial_load_oom_does_not_publish_a_prefix_or_overwrite_the_store() {
        let volume = Volume::new();
        let mut store = Store::<_, 16>::try_new(volume.clone()).unwrap();
        assert!(store.load(b"/a.epub", b"", b"", b"epub"));
        for _ in 0..33 {
            assert_eq!(store.add(Record::default(), b"", b"x"), AddResult::Added);
        }
        store.unload();
        let original = volume.source();
        SLAB_FAIL_AFTER.with(|n| n.set(Some(1)));
        let loaded = store.load(b"/a.epub", b"", b"", b"epub");
        SLAB_FAIL_AFTER.with(|n| n.set(None));
        assert!(!loaded);
        assert_eq!(store.count(), 0);
        assert!(!store.save());
        store.unload();
        assert_eq!(volume.source(), original);
        assert!(store.load(b"/a.epub", b"", b"", b"epub"));
        assert_eq!(store.count(), 33);
    }

    #[test]
    fn failed_cross_slab_deletion_preserves_records_and_text_offsets() {
        let volume = Volume::new();
        let mut store = Store::<_, 16>::try_new(volume.clone()).unwrap();
        assert!(store.load(b"/a.epub", b"", b"", b"epub"));
        for i in 0..33 {
            assert_eq!(store.add(Record::default(), b"", &[i]), AddResult::Added);
        }
        let original = volume.source();
        let middle = *store.row(15).unwrap();
        volume.fail_write();
        assert!(!store.remove(15));
        assert_eq!(store.count(), 33);
        assert_eq!(*store.row(15).unwrap(), middle);
        assert_eq!(volume.source(), original);
        assert!(store.remove(15));
        let mut text = Vec::new();
        assert!(store.read_text(15, &mut text));
        assert_eq!(text, [16]);
    }

    #[test]
    fn every_load_allocation_can_fail_without_publishing_or_writing() {
        let volume = Volume::new();
        let mut initial = Store::<_, 16>::try_new(volume.clone()).unwrap();
        assert!(initial.load(b"/a.epub", b"Original", b"Author", b"epub"));
        for _ in 0..33 {
            assert_eq!(initial.add(Record::default(), b"", b"x"), AddResult::Added);
        }
        initial.unload();
        let original = volume.source();
        let mut successes = 0;
        let mut failures = 0;
        for failure in 0..12 {
            let mut store = Store::<_, 16>::try_new(volume.clone()).unwrap();
            FAIL_AFTER.with(|n| n.set(Some(failure)));
            let loaded = store.load(b"/a.epub", b"", b"", b"epub");
            FAIL_AFTER.with(|n| n.set(None));
            if loaded {
                successes += 1;
                assert_eq!(store.count(), 33);
            } else {
                failures += 1;
                assert_eq!(store.count(), 0);
                assert!(!store.save());
            }
            store.unload();
            assert_eq!(volume.source(), original);
        }
        assert!(successes > 0 && failures > 0);
    }

    #[test]
    fn copy_out_text_uses_caller_storage_and_survives_owner_drop() {
        let volume = Volume::new();
        let mut store = Store::<_, 16>::try_new(volume.clone()).unwrap();
        assert!(store.load(b"/a.epub", b"", b"", b"epub"));
        assert_eq!(
            store.add(Record::default(), b"", b"hello"),
            AddResult::Added
        );
        let row = *store.row(0).unwrap();
        let before = volume.operations();
        let mut short = [99; 4];
        assert_eq!(store.read_into(0, &mut short), None);
        assert_eq!(short, [99; 4]);
        assert_eq!(volume.operations(), before);
        let mut output = [99; 7];
        FAIL_AFTER.with(|n| n.set(Some(0)));
        let result = store.read_into(0, &mut output);
        FAIL_AFTER.with(|n| n.set(None));
        assert_eq!(result, Some(5));
        drop(store);
        assert_eq!(row.text_length, 5);
        assert_eq!(output, *b"hellocc");
    }

    #[test]
    fn service_is_dormant_until_installed_and_rejects_reentry() {
        struct Gate(std::cell::RefCell<bool>);
        unsafe impl crate::service::Gate for Gate {
            fn with<R>(&self, action: impl FnOnce(&mut bool) -> R) -> R {
                action(&mut self.0.borrow_mut())
            }
        }
        let service = crate::service::Service::new(Gate(std::cell::RefCell::new(false)));
        assert_eq!(service.with(|_: &mut u32| 1), None);
        assert!(!service.install(|| None));
        assert!(service.install(|| Some(Box::new(42))));
        assert!(service.install(|| panic!("owner must not be replaced")));
        assert_eq!(
            service.with(|owner| {
                assert_eq!(service.with(|_| 0), None);
                assert!(!service.install(|| panic!("busy creation")));
                *owner += 1;
                *owner
            }),
            Some(43)
        );
        assert!(std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            service.with(|_| panic!("release the lease"));
        }))
        .is_err());
        assert_eq!(service.with(|owner| *owner), Some(43));
    }

    #[test]
    fn failure_logs_keep_native_severity_context_and_stage() {
        let volume = Volume::new();
        let mut store = Store::<_, 16>::try_new(volume.clone()).unwrap();
        assert!(!store.load(b"/a.epub", b"", b"", b"bad"));
        assert_eq!(
            volume.last_log(),
            (1, "Invalid clipping book type or path: bad".into())
        );
        assert!(store.load(b"/a.epub", b"", b"", b"epub"));
        volume.fail_write();
        assert_eq!(
            store.add(Record::default(), b"", b"x"),
            AddResult::SaveFailed
        );
        assert_eq!(
            volume.last_log(),
            (
                1,
                "Failed to write clipping header: /.crosspoint/clippings/epub_0.bin.tmp".into()
            )
        );
        let mut preview = [0; 260];
        let mut length = 99;
        assert!(!store.preview(7, &mut preview, &mut length));
        assert_eq!(
            volume.last_log(),
            (1, "Invalid clipping preview index: 7".into())
        );
    }
}
