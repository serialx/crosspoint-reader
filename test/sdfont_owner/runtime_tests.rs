use super::*;
use std::{collections::BTreeMap, ffi::CStr, mem::ManuallyDrop, pin::Pin};

#[derive(Default)]
struct State {
    bytes: Vec<u8>,
    live: BTreeMap<usize, usize>,
    events: Vec<(&'static str, usize)>,
    calls: usize,
    fail_at: usize,
}
struct Physical<'a>(&'a RefCell<State>);
impl Physical<'_> {
    fn call(&self, slot: *mut io::Slot, event: &'static str) -> bool {
        let mut state = self.0.borrow_mut();
        let address = slot as usize;
        assert!(state.live.contains_key(&address), "native slot moved");
        state.events.push((event, address));
        state.calls += 1;
        state.calls != state.fail_at
    }
}
unsafe impl io::Physical for Physical<'_> {
    unsafe fn init(&mut self, slot: *mut io::Slot) {
        let mut state = self.0.borrow_mut();
        assert!(state.live.insert(slot as usize, 0).is_none());
        state.events.push(("init", slot as usize));
    }
    unsafe fn destroy(&mut self, slot: *mut io::Slot) {
        self.call(slot, "destroy");
        assert!(self.0.borrow_mut().live.remove(&(slot as usize)).is_some());
    }
    unsafe fn open(&mut self, slot: *mut io::Slot, _: &CStr) -> Option<u32> {
        if !self.call(slot, "open") {
            return None;
        }
        self.0.borrow_mut().live.insert(slot as usize, 0);
        Some(self.0.borrow().bytes.len() as u32)
    }
    unsafe fn read(&mut self, slot: *mut io::Slot, bytes: &mut [u8]) -> usize {
        if !self.call(slot, "read") {
            return 0;
        }
        let mut state = self.0.borrow_mut();
        let position = state.live[&(slot as usize)];
        let count = bytes.len().min(state.bytes.len().saturating_sub(position));
        bytes[..count].copy_from_slice(&state.bytes[position..position + count]);
        state.live.insert(slot as usize, position + count);
        count
    }
    unsafe fn seek(&mut self, slot: *mut io::Slot, offset: u32) -> bool {
        if !self.call(slot, "seek") {
            return false;
        }
        let mut state = self.0.borrow_mut();
        state.live.insert(slot as usize, offset as usize);
        offset as usize <= state.bytes.len()
    }
    unsafe fn finish(&mut self, slot: *mut io::Slot) {
        self.call(slot, "finish");
    }
}
fn state(fail_at: usize) -> RefCell<State> {
    RefCell::new(State {
        bytes: tests::fixture(),
        fail_at,
        ..State::default()
    })
}

#[test]
fn cross_file_sharing_survives_either_owner_drop() {
    for first in [true, false] {
        let shared = IntervalRegistry::new();
        let bytes = tests::fixture();
        let a = unsafe { ParsedFont::load(&mut input(&bytes, 0), ALLOCATOR, &shared) }.unwrap();
        let b = unsafe { ParsedFont::load(&mut input(&bytes, 0), ALLOCATOR, &shared) }.unwrap();
        let table = a.style(0).unwrap().bmp_intervals;
        assert_eq!(table, b.style(3).unwrap().bmp_intervals);
        let remaining = if first {
            drop(a);
            b
        } else {
            drop(b);
            a
        };
        assert_eq!(
            unsafe { (*remaining.style(0).unwrap().bmp_intervals).first },
            65
        );
        drop(remaining);
        MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
    }
}

#[test]
fn slots_stay_fixed_through_nested_finish_release_and_failures() {
    for fail_at in 0..=6 {
        let state = state(fail_at);
        {
            let mut files = std::pin::pin!(io::Files::new(Physical(&state)));
            if files.as_mut().open(c"font").is_some() {
                files.as_mut().close(false);
                if files.as_mut().open(c"font").is_some() {
                    assert!(files.as_mut().open(c"font").is_none());
                    let sought = files.as_mut().seek(0);
                    assert_eq!(files.as_ref().seek_failed(), !sought);
                    let _ = files.as_mut().read(&mut [0; 4]);
                    files.as_mut().close(true);
                }
                files.as_mut().close(true);
            }
            files.as_mut().close(true);
            assert!(!files.as_ref().seek_failed());
        }
        let state = state.borrow();
        assert!(state.live.is_empty());
        let originals: Vec<_> = state.events.iter().take(2).map(|e| e.1).collect();
        assert_eq!(
            state
                .events
                .iter()
                .rev()
                .take(2)
                .map(|e| e.1)
                .collect::<Vec<_>>(),
            originals
        );
    }
}

#[test]
fn stable_face_load_faults_and_cache_teardown() {
    for fail_at in 0..=10 {
        MEMORY.with(|m| {
            *m.borrow_mut() = Memory {
                fail_at,
                ..Memory::default()
            }
        });
        let shared = IntervalRegistry::new();
        let state = state(0);
        {
            let mut files = std::pin::pin!(io::Files::new(Physical(&state)));
            let face =
                unsafe { owner::FaceOwner::load(c"font", files.as_mut(), ALLOCATOR, &shared) };
            assert_eq!(face.is_none(), (1..=3).contains(&fail_at));
            if let Some(face) = face {
                let address = face.address();
                assert_eq!(face.path(), c"font");
                let moved = Box::new(face);
                assert_eq!(moved.address(), address);
                assert_eq!(moved.parsed().style_count(), 2);
                drop(moved);
            }
        }
        assert!(state.borrow().live.is_empty());
        MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
    }
    MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    let state = state(0);
    let shared = IntervalRegistry::new();
    {
        let mut files = std::pin::pin!(io::Files::new(Physical(&state)));
        drop(
            unsafe { owner::FaceOwner::load(c"font", files.as_mut(), ALLOCATOR, &shared) }.unwrap(),
        );
    }
    let failure_calls: Vec<_> = state
        .borrow()
        .events
        .iter()
        .filter(|(event, _)| *event != "init")
        .enumerate()
        .filter_map(|(index, (event, _))| {
            matches!(*event, "open" | "read" | "seek").then_some(index + 1)
        })
        .collect();
    for fail_at in failure_calls {
        let state = self::state(fail_at);
        {
            let mut files = std::pin::pin!(io::Files::new(Physical(&state)));
            assert!(
                unsafe { owner::FaceOwner::load(c"font", files.as_mut(), ALLOCATOR, &shared) }
                    .is_none()
            );
        }
        assert!(state.borrow().live.is_empty());
        MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
    }
}

#[test]
fn moved_filled_holder_is_rejected_before_foreign_destruction() {
    let state = state(0);
    // Model the old unsafe move, retaining original bytes solely for valid cleanup.
    let mut original = ManuallyDrop::new(io::Files::new(Physical(&state)));
    unsafe { Pin::new_unchecked(&mut *original).open(c"font") }.unwrap();
    let mut moved = ManuallyDrop::new(unsafe { ptr::read(&*original) });
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _ = unsafe { Pin::new_unchecked(&mut *moved).seek(0) };
    }));
    assert!(result.is_err());
    // Never invoke native destruction on moved storage after the guard rejects it.
    unsafe { ManuallyDrop::drop(&mut original) };
    assert!(state.borrow().live.is_empty());
}

#[test]
fn face_releases_cache_buffers_before_shared_tables_and_style_storage() {
    let shared = IntervalRegistry::new();
    let state = state(0);
    let mut files = std::pin::pin!(io::Files::new(Physical(&state)));
    let mut face =
        unsafe { owner::FaceOwner::load(c"font", files.as_mut(), ALLOCATOR, &shared) }.unwrap();
    let services = sdfont::pool::Services {
        context: ptr::null_mut(),
        open: closed,
        close: finish,
        read: no_read,
        alloc: buffer,
        free,
        heap: free_heap,
        clock: now,
        corrupt: no_corrupt,
        diagnostic: None,
    };
    let mut allocated = Vec::new();
    unsafe {
        face.with_cache(&services, |cache| {
            let overflow = allocate(32);
            (*cache.overflow).bitmap = overflow;
            *cache.overflow_count = 1;
            allocated.push(overflow as usize);
            let advance = allocate(32);
            *cache.advances = advance.cast();
            allocated.push(advance as usize);
            for index in 0..2 {
                let resident = &mut *cache.styles.add(index);
                let mini = allocate(32);
                resident.mini_bitmap = mini;
                allocated.push(mini as usize);
            }
        });
    }
    drop(face);
    MEMORY.with(|m| {
        let m = m.borrow();
        assert!(m.live.is_empty());
        let actual: Vec<_> = m
            .freed
            .iter()
            .copied()
            .filter(|p| allocated.contains(p))
            .collect();
        assert_eq!(actual, allocated);
    });
}
unsafe extern "C" fn closed(_: *mut core::ffi::c_void) -> bool {
    false
}
unsafe extern "C" fn finish(_: *mut core::ffi::c_void, _: bool) {}
unsafe extern "C" fn no_read(
    _: *mut core::ffi::c_void,
    _: u32,
    _: *mut u8,
    _: usize,
    _: bool,
) -> bool {
    false
}
unsafe extern "C" fn buffer(n: usize, _: bool) -> *mut u8 {
    allocate(n)
}
unsafe extern "C" fn free_heap(_: bool) -> u32 {
    0
}
unsafe extern "C" fn now() -> u32 {
    0
}
unsafe extern "C" fn no_corrupt(_: *mut core::ffi::c_void, _: *const font::raw::Glyph, _: u32) {}
