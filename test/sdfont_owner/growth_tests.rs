use super::*;
use std::{cell::Cell, sync::atomic::AtomicU32};

#[derive(Default)]
struct Trace {
    requests: Vec<(usize, usize)>,
    freed: Vec<usize>,
    fail_moves: u8,
}
thread_local! { static TRACE: RefCell<Trace> = RefCell::default(); }
// The pool's table callbacks are io::Files over a slot, so this table keeps that outer
// contract: two slots of depth, a failed open at full depth, and a scan at depth zero that
// returns instead of asserting (io.rs Files::close/Files::read). The HAL rule lives at
// Physical below, which is the slot table production's open/read/close land in.
thread_local! { static TABLE_DEPTH: Cell<u8> = const { Cell::new(0) }; }
unsafe extern "C" fn growth_alloc(bytes: usize, _: bool) -> *mut u8 {
    let blocked = TRACE.with(|t| {
        let t = t.borrow();
        let at = t.requests.len();
        (3..6).contains(&at) && t.fail_moves & (1 << (at - 3)) != 0
    });
    let p = if blocked {
        MEMORY.with(|m| m.borrow_mut().attempts += 1);
        ptr::null_mut()
    } else {
        allocate(bytes)
    };
    TRACE.with(|t| t.borrow_mut().requests.push((bytes, p.addr())));
    p
}
unsafe extern "C" fn growth_free(p: *mut u8, domain: bool) {
    if !p.is_null() {
        TRACE.with(|t| t.borrow_mut().freed.push(p.addr()));
    }
    free(p, domain);
}
unsafe extern "C" fn growth_open(_: *mut core::ffi::c_void) -> bool {
    if TABLE_DEPTH.with(Cell::get) == 2 {
        return false; // io::Files::open refuses a third nesting level.
    }
    TABLE_DEPTH.with(|depth| depth.set(depth.get() + 1));
    true
}
unsafe extern "C" fn growth_close(_: *mut core::ffi::c_void, release: bool) {
    if TABLE_DEPTH.with(Cell::get) == 0 {
        return; // io::Files::close returns at depth zero.
    }
    if release {
        TABLE_DEPTH.with(|depth| depth.set(depth.get() - 1));
    }
}
unsafe extern "C" fn growth_read(
    context: *mut core::ffi::c_void,
    offset: u32,
    into: *mut u8,
    count: usize,
    seek: bool,
) -> bool {
    if TABLE_DEPTH.with(Cell::get) == 0 {
        return !seek && count == 0; // read returns zero; a requested seek still fails.
    }
    let state = &*context.cast::<RefCell<State>>();
    let mut state = state.borrow_mut();
    state.calls += 1;
    let Some(data) = state.bytes.get(offset as usize..offset as usize + count) else {
        return false;
    };
    ptr::copy_nonoverlapping(data.as_ptr(), into, count);
    true
}
unsafe extern "C" fn plenty(_: bool) -> u32 {
    200 * 1024
}
fn growth_fixture() -> Vec<u8> {
    // Four glyphs, one kerning pair and AB -> C. Bitmap lengths 1/4/9/16.
    let mut b = vec![0; 185];
    b[..8].copy_from_slice(b"CPFONT\0\0");
    b[8] = 4;
    b[12] = 1;
    b[36] = 1;
    b[40] = 4;
    b[44] = 16;
    b[49] = 1;
    b[51] = 1;
    b[53] = 1;
    b[54] = 1;
    b[55] = 1;
    b[56] = 64;
    b[64] = 65;
    b[68] = 68;
    let mut offset = 0u32;
    for (i, length) in [1u16, 4, 9, 16].into_iter().enumerate() {
        let at = 76 + 16 * i;
        b[at] = 8;
        b[at + 1] = length as u8;
        b[at + 2] = 9 + i as u8;
        b[at + 8..at + 10].copy_from_slice(&length.to_le_bytes());
        b[at + 12..at + 16].copy_from_slice(&offset.to_le_bytes());
        for byte in &mut b[155 + offset as usize..155 + offset as usize + length as usize] {
            *byte = 0x80 >> i;
        }
        offset += u32::from(length);
    }
    b[140..143].copy_from_slice(&[65, 0, 1]);
    b[143..146].copy_from_slice(&[66, 0, 1]);
    b[146] = 255;
    b[147..151].copy_from_slice(&((65u32 << 16) | 66).to_le_bytes());
    b[151..155].copy_from_slice(&67u32.to_le_bytes());
    b
}
struct Outcome {
    attempts: usize,
    moved: [bool; 3],
    requests: Vec<usize>,
    complete: bool,
}
fn growth_run(fail: usize, fail_moves: u8) -> Outcome {
    MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    TRACE.with(|t| *t.borrow_mut() = Trace::default());
    TABLE_DEPTH.with(|depth| depth.set(0));
    let shared = IntervalRegistry::new();
    let state = state(0);
    state.borrow_mut().bytes = growth_fixture();
    let mut files = std::pin::pin!(io::Files::new(Physical(&state)));
    let mut face =
        unsafe { owner::FaceOwner::load(c"growth", files.as_mut(), ALLOCATOR, &shared) }.unwrap();
    let generation = AtomicU32::new(0);
    unsafe { face.bind(&generation, tagged_miss) };
    let slot = face.font_slot(0);
    let services = sdfont::pool::Services {
        context: ptr::from_ref(&state).cast_mut().cast(),
        open: growth_open,
        close: growth_close,
        read: growth_read,
        alloc: growth_alloc,
        free: growth_free,
        heap: plenty,
        clock: now,
        corrupt: no_corrupt,
        diagnostic: None,
    };
    let before = unsafe {
        face.with_cache(&services, |cache| {
            assert_eq!(
                cache.build_advances_from(4, 1, |emit| {
                    for cp in 65..=68 {
                        emit(cp, false);
                    }
                }),
                0
            );
            cache.prewarm_span(b"ABCD", 1, true, false, false);
            let initial = [
                (*cache.advances).addr(),
                (*cache.styles).mini_intervals.addr(),
                (*cache.styles).mini_glyphs.addr(),
            ];
            cache.prewarm_span(b"AB", 1, false, true, false);
            assert_eq!(
                initial,
                [
                    (*cache.advances).addr(),
                    (*cache.styles).mini_intervals.addr(),
                    (*cache.styles).mini_glyphs.addr(),
                ]
            );
            let s = &*cache.styles;
            assert_eq!(s.bitmap_used, 14);
            assert_eq!(s.mini.ligature_pair_count, 1);
            assert_eq!(s.mini.kern_left_entry_count, 1);
            [
                (*cache.advances).addr(),
                s.mini_intervals.addr(),
                s.mini_glyphs.addr(),
            ]
        })
    };
    MEMORY.with(|m| {
        let mut m = m.borrow_mut();
        m.attempts = 0;
        m.fail_at = fail;
    });
    TRACE.with(|t| *t.borrow_mut() = Trace::default());
    TRACE.with(|t| t.borrow_mut().fail_moves = fail_moves);
    let (after, complete) = unsafe {
        face.with_cache(&services, |cache| {
            cache.prewarm_span(b"ABD", 1, false, true, false);
            let s = &*cache.styles;
            for cp in 65..=68 {
                assert_eq!(cache.advance(0, cp), Some((cp - 65 + 9) as u16));
            }
            (
                [
                    (*cache.advances).addr(),
                    s.mini_intervals.addr(),
                    s.mini_glyphs.addr(),
                ],
                s.bitmap_used == 30,
            )
        })
    };
    let attempts = MEMORY.with(|m| m.borrow().attempts);
    let requests = TRACE.with(|t| t.borrow().requests.iter().map(|r| r.0).collect());
    if complete {
        unsafe {
            // Keep the owner's published slot, but reacquire every table after mutation.
            let view = &*slot.read();
            assert_eq!(view.ligature_pair_count, 1);
            if fail == 0 || (4..=6).contains(&fail) {
                assert_eq!(view.kern_left_entry_count, 1);
                assert_eq!(view.kern_right_entry_count, 1);
                assert_eq!(*view.kern_matrix, -1);
            }
            assert_eq!({ (*view.ligature_pairs).ligature_cp }, 67);
            assert_eq!((*view.intervals).first, 65);
            for (i, length) in [1, 4, 9, 16].into_iter().enumerate() {
                let glyph = &*view.glyph.add(i);
                assert_eq!(glyph.advance_x, 9 + i as u16);
                assert_eq!(glyph.data_length, length);
                assert_eq!(*face.bitmap(view, glyph), 0x80 >> i);
            }
            assert!((view.coverage_handler.unwrap())(view.glyph_miss_ctx, 68));
        }
    }
    MEMORY.with(|m| m.borrow_mut().fail_at = 0);
    if complete && fail == 0 {
        TRACE.with(|t| *t.borrow_mut() = Trace::default());
        let warm = unsafe {
            face.with_cache(&services, |cache| {
                cache.prewarm_span(b"ABD", 1, false, true, false);
                let s = &*cache.styles;
                [
                    (*cache.advances).addr(),
                    s.mini_intervals.addr(),
                    s.mini_glyphs.addr(),
                ]
            })
        };
        assert_eq!(warm, after);
        // Existing codepoint collection scratch remains; no resident request is added.
        TRACE.with(|t| {
            assert_eq!(
                t.borrow().requests.iter().map(|r| r.0).collect::<Vec<_>>(),
                [2048]
            )
        });
    }
    drop(face);
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
    Outcome {
        attempts,
        moved: std::array::from_fn(|i| before[i] != after[i]),
        requests,
        complete,
    }
}
// The pool table is io::Files over a slot, so its outer callbacks keep Files' behaviour: a
// scan at depth zero returns instead of asserting, and a third nested open is refused.
#[test]
fn pool_table_scans_at_depth_zero_are_safe() {
    TABLE_DEPTH.with(|depth| depth.set(0));
    let state = state(0);
    let mut bytes = [0u8; 4];
    unsafe {
        assert!(!growth_read(
            ptr::from_ref(&state).cast_mut().cast(),
            0,
            bytes.as_mut_ptr(),
            bytes.len(),
            false,
        ));
        growth_close(ptr::null_mut(), false);
        growth_close(ptr::null_mut(), true);
    }
    assert_eq!(TABLE_DEPTH.with(Cell::get), 0);
}

#[test]
fn pool_table_nests_two_files_and_keeps_depth_on_finish() {
    TABLE_DEPTH.with(|depth| depth.set(0));
    unsafe {
        assert!(growth_open(ptr::null_mut()));
        assert!(growth_open(ptr::null_mut()));
        assert!(!growth_open(ptr::null_mut())); // io::Files has two slots.
        growth_close(ptr::null_mut(), false); // finish keeps the depth, as Files::close does.
        assert!(!growth_open(ptr::null_mut()));
        growth_close(ptr::null_mut(), true);
        growth_close(ptr::null_mut(), true);
    }
    assert_eq!(TABLE_DEPTH.with(Cell::get), 0);
}

// Native asserts on a read that reaches a slot with no HAL handle, so the fixture must too.
// The pool table callbacks are extern "C", where a panic would abort instead of unwinding, so
// that guard is only exercised through the growth runs below.
#[test]
#[should_panic(expected = "read on a slot with no HAL handle")]
fn slot_read_without_an_open_is_rejected() {
    let state = state(0);
    let mut physical = Physical(&state);
    let mut slot = core::mem::MaybeUninit::<io::Slot>::zeroed();
    unsafe {
        io::Physical::init(&mut physical, slot.as_mut_ptr());
        let mut bytes = [0u8; 4];
        io::Physical::read(&mut physical, slot.as_mut_ptr(), &mut bytes);
    }
}

#[test]
fn bitmap_growth_relocates_tables_and_republishes_views() {
    let run = growth_run(0, 0);
    assert!(run.complete);
    assert_eq!(run.moved, [true; 3]);
    assert_eq!(&run.requests[3..6], &[512, 48, 64]);
}
#[test]
fn every_growth_allocation_failure_preserves_owned_storage() {
    let baseline = growth_run(0, 0);
    for failure in 1..=baseline.attempts {
        let run = growth_run(failure, 0);
        if (4..=6).contains(&failure) {
            assert!(run.complete);
            assert!(!run.moved[failure - 4]);
        }
    }
}

#[test]
fn failed_moves_do_not_prevent_later_moves_or_bitmap_rebuild() {
    for mask in 1..8 {
        let run = growth_run(0, mask);
        assert!(run.complete);
        assert_eq!(run.moved, std::array::from_fn(|i| mask & (1 << i) == 0));
    }
}

#[test]
fn zero_depth_empty_read_succeeds_only_without_seek() {
    TABLE_DEPTH.with(|depth| depth.set(0));
    unsafe {
        assert!(growth_read(ptr::null_mut(), 0, ptr::null_mut(), 0, false));
        assert!(!growth_read(ptr::null_mut(), 0, ptr::null_mut(), 0, true));
    }
}
