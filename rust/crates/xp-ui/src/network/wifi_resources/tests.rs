extern crate std;
use super::{Buffer, Error, Memory, SmallZ};
use core::{alloc::Layout, ptr};
use std::{cell::RefCell, collections::BTreeMap, vec::Vec};

#[derive(Default)]
struct Allocations {
    fail: bool,
    requests: Vec<usize>,
    live: BTreeMap<usize, Layout>,
}
std::thread_local! {
    static ALLOCATIONS: RefCell<Allocations> = RefCell::default();
}
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(layout: Layout) -> *mut u8 {
        ALLOCATIONS.with(|state| {
            let mut state = state.borrow_mut();
            state.requests.push(layout.size());
            if core::mem::take(&mut state.fail) {
                return ptr::null_mut();
            }
            let out = unsafe { std::alloc::alloc(layout) };
            assert!(!out.is_null());
            assert!(state.live.insert(out.addr(), layout).is_none());
            out
        })
    }
    unsafe fn release(pointer: *mut u8, layout: Layout) {
        ALLOCATIONS.with(|state| {
            assert_eq!(
                state.borrow_mut().live.remove(&pointer.addr()),
                Some(layout)
            );
        });
        unsafe { std::alloc::dealloc(pointer, layout) };
    }
}
fn requests() -> Vec<usize> {
    ALLOCATIONS.with(|state| state.borrow().requests.clone())
}
fn fail_next() {
    ALLOCATIONS.with(|state| state.borrow_mut().fail = true);
}
fn no_leaks() {
    ALLOCATIONS.with(|state| assert!(state.borrow().live.is_empty()));
}

#[test]
fn inline_clone_clear_and_rewrite_do_not_allocate() {
    {
        let mut text = SmallZ::<Heap>::new();
        text.try_assign(b"123456789012345").unwrap();
        let copy = text.try_clone().unwrap();
        assert_eq!(copy.as_z(), b"123456789012345\0");
        text.clear();
        assert_eq!(text.as_z(), b"\0");
        text.write_joined(&[b"hi", b"\0", b"there"]).unwrap();
        assert_eq!(text.as_bytes(), b"hi\0there");
        assert_eq!(text.as_z(), b"hi\0there\0");
        assert!(requests().is_empty());
    }
    no_leaks();
}

#[test]
fn failed_long_assignment_preserves_the_old_value_and_its_allocation() {
    {
        let mut text = SmallZ::<Heap>::new();
        text.try_assign(b"1234567890123456").unwrap();
        let old = text.as_z().as_ptr();
        fail_next();
        assert_eq!(
            text.try_assign(b"12345678901234567"),
            Err(Error::Allocation(18))
        );
        assert_eq!(text.as_z(), b"1234567890123456\0");
        assert_eq!(text.as_z().as_ptr(), old);
        text.clear();
        text.write_joined(&[b"reused"]).unwrap();
        assert_eq!(text.as_z().as_ptr(), old);
        assert_eq!(requests(), [17, 18]);
    }
    no_leaks();
}

#[test]
fn reserved_text_writes_never_grow_or_publish_partial_bytes() {
    {
        let mut text = SmallZ::<Heap>::new();
        text.try_reserve(50).unwrap();
        text.write_joined(&[b"Network: ", b"long SSID"]).unwrap();
        let before = text.as_z().to_vec();
        let calls = requests();
        assert_eq!(text.write_joined(&[&[b'x'; 51]]), Err(Error::Capacity));
        assert_eq!(text.as_z(), before);
        assert_eq!(text.try_reserve(usize::MAX), Err(Error::Capacity));
        assert_eq!(requests(), calls);
    }
    no_leaks();
}

#[test]
fn long_copy_owns_its_bytes_after_the_source_moves_and_drops() {
    let copy = {
        let mut text = SmallZ::<Heap>::new();
        text.try_assign(b"long non-ASCII SSID: \xe9\x9b\xaa")
            .unwrap();
        let copy = text.try_clone().unwrap();
        assert_ne!(copy.as_z().as_ptr(), text.as_z().as_ptr());
        let moved = text;
        drop(moved);
        copy
    };
    assert_eq!(copy.as_z(), b"long non-ASCII SSID: \xe9\x9b\xaa\0");
    drop(copy);
    no_leaks();
}

#[test]
fn buffer_growth_moves_owned_strings_and_drops_each_allocation_once() {
    {
        let mut rows = Buffer::<SmallZ<Heap>, Heap>::new();
        rows.try_reserve(1).unwrap();
        let mut first = SmallZ::new();
        first.try_assign(b"1234567890123456").unwrap();
        rows.try_push(first).unwrap();
        rows.try_push(SmallZ::new()).unwrap();
        let mut moved = rows;
        assert_eq!(moved.as_slice()[0].as_bytes(), b"1234567890123456");
        assert_eq!(moved.as_slice()[1].as_z(), b"\0");
        fail_next();
        assert!(matches!(moved.try_reserve(10), Err(Error::Allocation(_))));
        assert_eq!(moved.as_slice()[0].as_bytes(), b"1234567890123456");
        let calls = requests().len();
        assert_eq!(moved.try_reserve(usize::MAX), Err(Error::Capacity));
        assert_eq!(requests().len(), calls);
        moved.clear();
        assert!(moved.as_slice().is_empty());
        assert!(moved.capacity() >= 2);
    }
    no_leaks();
}

#[test]
fn row_status_keeps_the_published_saved_marker_after_forget() {
    use super::Row;
    {
        let mut row = Row::<Heap>::scan(b"network", -50, true, true, 7).unwrap();
        assert_eq!(row.status(), [0; 9]);
        row.rebuild_status();
        assert_eq!(&row.status(), b"+ * ||||\0");
        row.set_saved(false);
        assert!(!row.saved());
        assert_eq!(&row.status(), b"+ * ||||\0");
        row.replace_signal(-80, false);
        assert_eq!(row.scan_index(), 7);
        assert_eq!(&row.status(), b"+ * ||||\0");
        row.rebuild_status();
        assert_eq!(&row.status(), b"   |\0\0\0\0\0");
        let mut hidden = Row::<Heap>::hidden();
        hidden.rebuild_status();
        assert!(hidden.is_hidden());
        assert_eq!(hidden.status(), [0; 9]);
        assert!(requests().is_empty());
    }
    no_leaks();
}

#[test]
fn row_signal_thresholds_and_saved_encrypted_prefixes_match_native() {
    use super::Row;
    for (rssi, bars) in [
        (-49, b"||||"),
        (-50, b"||||"),
        (-51, b" |||"),
        (-60, b" |||"),
        (-61, b"  ||"),
        (-70, b"  ||"),
        (-71, b"   |"),
    ] {
        let mut row = Row::<Heap>::scan(b"net", rssi, true, false, 0).unwrap();
        row.rebuild_status();
        assert_eq!(&row.status()[..2], b"* ");
        assert_eq!(&row.status()[2..6], bars);
        assert_eq!(&row.status()[6..], &[0; 3]);
    }
    no_leaks();
}

#[test]
fn saved_and_rssi_ranking_use_first_scan_index_only_for_ties() {
    use super::{sort_rows, Row};
    {
        let mut rows = Buffer::<Row<Heap>, Heap>::new();
        let names: [&[u8]; 5] = [b"zeta", b"zeta_saved", b"alpha_saved", b"first", b"alpha"];
        for (index, saved, rssi) in [
            (4, false, -10),
            (2, true, -80),
            (0, false, -10),
            (3, true, -20),
            (1, true, -80),
        ] {
            rows.try_push(Row::scan(names[usize::from(index)], rssi, false, saved, index).unwrap())
                .unwrap();
        }
        let allocations = requests();
        sort_rows(rows.as_mut_slice());
        assert_eq!(
            rows.as_slice()
                .iter()
                .map(Row::scan_index)
                .collect::<Vec<_>>(),
            [3, 1, 2, 0, 4]
        );
        assert_eq!(requests(), allocations);
        rows.try_push(Row::hidden()).unwrap();
        assert!(rows.as_slice().last().unwrap().is_hidden());
    }
    no_leaks();
}

#[test]
fn tied_rows_keep_scan_order_at_partition_boundaries() {
    use super::{sort_rows, Row};
    let sizes: &[u16] = if cfg!(miri) {
        &[0, 1, 2, 16, 17, 33]
    } else {
        &[0, 1, 2, 15, 16, 17, 31, 32, 33, 64, 128, 1024, 32767]
    };
    for &size in sizes {
        {
            let mut rows = Buffer::<Row<Heap>, Heap>::new();
            rows.try_reserve(usize::from(size) + 1).unwrap();
            for index in (0..size).rev() {
                rows.try_push(Row::scan(b"tie", -60, false, true, index).unwrap())
                    .unwrap();
            }
            let allocations = requests();
            sort_rows(rows.as_mut_slice());
            assert!(rows
                .as_slice()
                .iter()
                .enumerate()
                .all(|(i, row)| usize::from(row.scan_index()) == i));
            assert_eq!(requests(), allocations);
        }
        no_leaks();
    }
}

#[test]
fn row_sort_matches_the_total_key_for_mixed_ranks_and_owned_names() {
    use super::{sort_rows, Row};
    use core::cmp::Reverse;
    let sizes: &[u16] = if cfg!(miri) {
        &[0, 1, 2, 3, 8, 17, 33]
    } else {
        &[
            0, 1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 64, 127, 128, 1024, 32767,
        ]
    };
    for &size in sizes {
        for profile in 0..5 {
            let facts = |index: u16| match profile {
                0 => (true, -60),
                1 => (index.is_multiple_of(3), -30 - i32::from(index % 7)),
                2 => (
                    index.is_multiple_of(2),
                    [i32::MIN, i32::MAX, -127, 0, -30][usize::from(index % 5)],
                ),
                3 => (false, i32::from(index)),
                _ => (false, -i32::from(index)),
            };
            let mut expected: Vec<_> = (0..size).collect();
            expected.sort_by_key(|&index| {
                let (saved, rssi) = facts(index);
                (Reverse(saved), Reverse(rssi), index)
            });
            for order in 0..3 {
                {
                    let mut indices: Vec<_> = (0..size).collect();
                    if order == 1 {
                        indices.reverse();
                    } else if order == 2 {
                        let mut seed = 12345u32;
                        for i in (1..indices.len()).rev() {
                            seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                            indices.swap(i, seed as usize % (i + 1));
                        }
                    }
                    let mut rows = Buffer::<Row<Heap>, Heap>::new();
                    rows.try_reserve(usize::from(size)).unwrap();
                    for index in indices {
                        let (saved, rssi) = facts(index);
                        let name = std::format!("long-owned-network-name-{index}");
                        rows.try_push(
                            Row::scan(name.as_bytes(), rssi, index % 2 != 0, saved, index).unwrap(),
                        )
                        .unwrap();
                    }
                    let before = requests();
                    let (_, allocations) =
                        crate::boot::count_allocations(|| sort_rows(rows.as_mut_slice()));
                    assert_eq!(allocations, 0);
                    assert_eq!(requests(), before);
                    for (row, &index) in rows.as_slice().iter().zip(&expected) {
                        let (saved, rssi) = facts(index);
                        assert_eq!(row.scan_index(), index);
                        assert_eq!((row.saved(), row.rssi()), (saved, rssi));
                        assert_eq!(row.encrypted(), index % 2 != 0);
                        assert_eq!(
                            row.ssid(),
                            std::format!("long-owned-network-name-{index}").as_bytes()
                        );
                    }
                }
                no_leaks();
            }
        }
    }
}

#[test]
fn truncation_uses_native_strict_width_and_preserves_utf8_boundaries_without_growth() {
    use super::text;
    {
        let mut out = SmallZ::<Heap>::new();
        out.try_reserve(64).unwrap();
        let allocations = requests();
        let width = |z: &[u8]| (z.len() - 1) as i32;
        text::truncate(&mut out, b"abc", 3, width).unwrap();
        assert_eq!(out.as_z(), b"abc\0");
        text::truncate(&mut out, b"abcdef", 5, width).unwrap();
        assert_eq!(out.as_z(), b"a\xe2\x80\xa6\0");
        text::truncate(&mut out, "éabc".as_bytes(), 6, width).unwrap();
        assert_eq!(out.as_z(), "éabc\0".as_bytes());
        text::truncate(&mut out, "éabcde".as_bytes(), 6, width).unwrap();
        assert_eq!(out.as_z(), "é…\0".as_bytes());
        text::truncate(&mut out, b"abcdef", 0, |_| {
            panic!("must not measure without space")
        })
        .unwrap();
        assert_eq!(out.as_z(), b"\0");
        text::truncate(&mut out, b"a\0hidden", 2, width).unwrap();
        assert_eq!(out.as_z(), b"a\0");
        assert_eq!(requests(), allocations);
    }
    no_leaks();
}

#[test]
fn insufficient_truncation_scratch_does_not_publish_partial_text() {
    use super::text;
    let mut out = SmallZ::<Heap>::new();
    out.try_assign(b"old").unwrap();
    assert_eq!(
        text::truncate(&mut out, b"this requires a larger scratch", 5, |_| 99),
        Err(Error::Capacity)
    );
    assert_eq!(out.as_z(), b"old\0");
    assert!(requests().is_empty());
    no_leaks();
}

#[test]
fn sort_and_prepared_text_do_not_use_the_global_allocator() {
    use super::{sort_rows, text, Row};
    let (control, calls) = crate::boot::count_allocations(|| Vec::<u8>::with_capacity(32));
    assert_eq!(calls, 1);
    drop(control);
    {
        let mut rows = Buffer::<Row<Heap>, Heap>::new();
        rows.try_reserve(128).unwrap();
        for index in (0..128).rev() {
            rows.try_push(Row::scan(b"same rank", -50, true, true, index).unwrap())
                .unwrap();
        }
        let mut out = SmallZ::<Heap>::new();
        out.try_reserve(50).unwrap();
        let (result, calls) = crate::boot::count_allocations(|| {
            sort_rows(rows.as_mut_slice());
            for row in rows.as_mut_slice() {
                row.rebuild_status();
            }
            text::truncate_joined(&mut out, b"Network: ", b"a long network name", 18, |z| {
                (z.len() - 1) as i32
            })
        });
        assert_eq!(result, Ok(()));
        assert_eq!(calls, 0);
    }
    no_leaks();
}

#[test]
fn failed_push_drops_the_rejected_value_without_initializing_a_slot() {
    use std::cell::Cell;
    struct Value<'a>(&'a Cell<u8>);
    impl Drop for Value<'_> {
        fn drop(&mut self) {
            self.0.set(self.0.get() + 1);
        }
    }
    let drops = Cell::new(0);
    {
        let mut buffer = Buffer::<Value<'_>, Heap>::new();
        fail_next();
        assert!(matches!(
            buffer.try_push(Value(&drops)),
            Err(Error::Allocation(_))
        ));
        assert_eq!(drops.get(), 1);
        assert!(buffer.as_slice().is_empty());
        assert_eq!(buffer.capacity(), 0);
    }
    assert_eq!(drops.get(), 1);
    no_leaks();
}

#[test]
fn failed_clone_leaves_an_unbounded_stored_name_owned_by_its_source() {
    {
        let mut source = SmallZ::<Heap>::new();
        source.try_assign(&[b'w'; 1024]).unwrap();
        fail_next();
        assert!(matches!(source.try_clone(), Err(Error::Allocation(1025))));
        assert_eq!(source.as_bytes(), &[b'w'; 1024]);
        assert_eq!(source.as_z()[1024], 0);
    }
    no_leaks();
}

#[test]
#[ignore = "strict-Miri negative control; deliberately reads released text"]
fn negative_released_text_loan() {
    let mut text = SmallZ::<Heap>::new();
    text.try_assign(b"1234567890123456").unwrap();
    let pointer = text.as_z().as_ptr();
    let loan = unsafe { core::slice::from_raw_parts(pointer, 17) };
    drop(text);
    std::hint::black_box(loan[0]);
}

#[test]
#[ignore = "strict-Miri negative control; deliberately writes through a shared text loan"]
fn negative_mutated_text_loan() {
    let mut text = SmallZ::<Heap>::new();
    text.try_assign(b"1234567890123456").unwrap();
    let loan = text.as_z();
    unsafe { loan.as_ptr().cast_mut().write(b'X') };
    std::hint::black_box(loan[0]);
}
