extern crate std;
use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    boxed::Box,
    cell::Cell as LocalCell,
    collections::BTreeMap,
    vec,
    vec::Vec,
};

std::thread_local! { static ACTIVE: LocalCell<*mut World> = const { LocalCell::new(ptr::null_mut()) }; }
struct World {
    data: Vec<u8>,
    position: usize,
    opened: bool,
    fail: usize,
    calls: usize,
    owner: *mut XpXtcOwnerStorage,
    allocations: BTreeMap<usize, Layout>,
    inspections: usize,
    reads: Vec<usize>,
    chunks: Vec<u8>,
    diagnostics: Vec<u32>,
    inspect_names: bool,
}
impl World {
    unsafe fn inspect(&mut self) {
        let mut view = core::mem::MaybeUninit::uninit();
        xp_xtc_owner_view(self.owner, false, view.as_mut_ptr());
        let view = view.assume_init();
        assert!(!view.state.is_null());
        if self.inspect_names {
            let _header = (*view.state).header.get();
            for (data, length) in [(view.title, view.title_len), (view.author, view.author_len)] {
                if length != 0 {
                    assert_eq!(data.add(length).read(), 0);
                }
            }
            for i in 0..view.chapter_count {
                let row = &*view.chapters.add(i);
                assert_eq!(row.name.add(row.name_len).read(), 0);
            }
        }
        self.inspections += 1;
    }
    unsafe fn step(&mut self) -> bool {
        self.inspect();
        self.calls += 1;
        self.calls != self.fail
    }
}
unsafe extern "C" fn open(p: *mut c_void, name: *const u8) -> bool {
    let w = &mut *p.cast::<World>();
    assert_eq!(name.read(), b'b');
    w.opened = w.step();
    w.position = 0;
    w.opened
}
unsafe extern "C" fn is_open(p: *mut c_void) -> bool {
    let w = &mut *p.cast::<World>();
    w.inspect();
    w.opened
}
unsafe extern "C" fn close(p: *mut c_void) {
    let w = &mut *p.cast::<World>();
    w.inspect();
    w.opened = false;
}
unsafe extern "C" fn read(p: *mut c_void, out: *mut u8, n: usize) -> i32 {
    let w = &mut *p.cast::<World>();
    w.reads.push(n);
    if !w.step() || !w.opened {
        return -1;
    }
    let count = n.min(w.data.len() - w.position);
    if count != 0 {
        ptr::copy_nonoverlapping(w.data.as_ptr().add(w.position), out, count);
    }
    w.position += count;
    count as i32
}
unsafe extern "C" fn seek(p: *mut c_void, at: u64) -> bool {
    let w = &mut *p.cast::<World>();
    if !w.step() || !w.opened || at > w.data.len() as u64 {
        return false;
    }
    w.position = at as usize;
    true
}
unsafe extern "C" fn size(p: *mut c_void) -> u64 {
    let w = &mut *p.cast::<World>();
    w.inspect();
    w.data.len() as u64
}
unsafe extern "C" fn allocate(n: usize, kind: u32) -> *mut u8 {
    // The native C3 build has no long-string shrink allocation with exceptions off.
    if kind == ALLOC_SHRINK {
        return ptr::null_mut();
    }
    let n = if kind == ALLOC_CHAPTERS {
        n * size_of::<XpXtcChapterRow>()
    } else {
        n
    };
    ACTIVE.with(|active| {
        let w = &mut *active.get();
        w.inspect();
        let layout = Layout::from_size_align(n, core::mem::align_of::<usize>()).unwrap();
        let p = alloc(layout);
        assert!(!p.is_null());
        assert!(w.allocations.insert(p.addr(), layout).is_none());
        p
    })
}
unsafe extern "C" fn release(p: *mut u8, n: usize) {
    ACTIVE.with(|active| {
        let w = &mut *active.get();
        w.inspect();
        let layout = w.allocations.remove(&p.addr()).unwrap();
        assert_eq!(layout.size(), n);
        dealloc(p, layout);
    });
}
unsafe extern "C" fn diagnostic(p: *mut c_void, event: u32, _: u32, _: u32, _: u32, _: u32) {
    let w = &mut *p.cast::<World>();
    w.inspect();
    w.diagnostics.push(event);
}
static LEAVES: XpXtcLeaves = XpXtcLeaves {
    open,
    is_open,
    close,
    read,
    seek,
    size,
    allocate,
    release,
    diagnostic,
};
struct WorldHandle(*mut World);
impl core::ops::Deref for WorldHandle {
    type Target = World;
    fn deref(&self) -> &World {
        unsafe { &*self.0 }
    }
}
impl core::ops::DerefMut for WorldHandle {
    fn deref_mut(&mut self) -> &mut World {
        unsafe { &mut *self.0 }
    }
}
impl Drop for WorldHandle {
    fn drop(&mut self) {
        unsafe {
            drop(Box::from_raw(self.0));
        }
    }
}
struct Harness {
    world: WorldHandle,
    owner: *mut XpXtcOwnerStorage,
}
impl Harness {
    fn new(length: usize) -> Self {
        unsafe {
            let owner = alloc(Layout::new::<XpXtcOwnerStorage>()).cast();
            let world = Box::new(World {
                data: fixture(length),
                position: 0,
                opened: false,
                fail: 0,
                calls: 0,
                owner,
                allocations: BTreeMap::new(),
                inspections: 0,
                reads: Vec::new(),
                chunks: Vec::new(),
                diagnostics: Vec::new(),
                inspect_names: false,
            });
            let context = Box::into_raw(world);
            ACTIVE.with(|active| {
                assert!(active.get().is_null());
                active.set(context);
            });
            xp_xtc_owner_init(owner, context.cast(), &LEAVES);
            Self {
                world: WorldHandle(context),
                owner,
            }
        }
    }
    fn open(&self) -> i32 {
        unsafe { xp_xtc_owner_open(self.owner, b"book.xtc".as_ptr(), 8) }
    }
    fn view(&self, lazy: bool) -> XpXtcView {
        unsafe {
            let mut view = core::mem::MaybeUninit::uninit();
            xp_xtc_owner_view(self.owner, lazy, view.as_mut_ptr());
            view.assume_init()
        }
    }
}
impl Drop for Harness {
    fn drop(&mut self) {
        unsafe {
            xp_xtc_owner_drop(self.owner);
            dealloc(self.owner.cast(), Layout::new::<XpXtcOwnerStorage>());
        }
        assert!(self.world.allocations.is_empty());
        ACTIVE.with(|active| active.set(ptr::null_mut()));
    }
}
fn fixture(length: usize) -> Vec<u8> {
    let mut b = vec![0; 392];
    b[..4].copy_from_slice(b"XTC\0");
    b[4] = 1;
    b[6] = 1;
    b[9] = 1;
    b[11] = 1;
    b[24..32].copy_from_slice(&352u64.to_le_bytes());
    b[32..40].copy_from_slice(&368u64.to_le_bytes());
    b[48..52].copy_from_slice(&256u32.to_le_bytes());
    b[56..56 + length.min(127)].fill(b'T');
    b[184..184 + length.min(63)].fill(b'A');
    b[256..256 + length.min(80)].fill(b'C');
    b[336] = 1;
    b[338] = 1;
    b[352..360].copy_from_slice(&368u64.to_le_bytes());
    b[360..364].copy_from_slice(&24u32.to_le_bytes());
    b[364] = 9;
    b[366] = 1;
    b[368..372].copy_from_slice(b"XTG\0");
    b[372] = 9;
    b[374] = 1;
    b[378..382].copy_from_slice(&2u32.to_le_bytes());
    b[390] = 0x5a;
    b[391] = 0xa5;
    b
}
#[test]
fn metadata_chapters_and_callbacks_use_stable_borrowed_storage() {
    for length in [0, 15, 16, 22, 23, 29, 30, 31, 63, 127] {
        let h = Harness::new(length);
        assert_eq!(h.open(), 0);
        let initial = h.view(false);
        assert!(!h.world.opened);
        assert_eq!(initial.chapter_count, 0);
        assert_eq!(initial.title_len, length.min(127));
        assert_eq!(initial.author_len, length.min(63));
        let view = h.view(true);
        assert_eq!(view.chapter_count, 1);
        assert!(!h.world.opened);
        unsafe {
            let row = &*view.chapters;
            assert_eq!(row.name_len, length.min(80));
            assert_eq!((row.start_page, row.end_page), (0, 0));
            assert_eq!(row.name.add(row.name_len).read(), 0);
            assert_eq!(initial.title, view.title);
            assert_eq!(view.title.add(view.title_len).read(), 0);
        }
        assert!(h.world.inspections > 10);
    }
}
#[test]
fn page_fetch_and_stream_preserve_handle_and_chunk_lifetime() {
    unsafe extern "C" fn emit(p: *mut c_void, bytes: *const u8, n: usize, offset: usize) {
        let w = &mut *p.cast::<World>();
        w.inspect();
        assert_eq!(offset, w.chunks.len());
        w.chunks
            .extend_from_slice(core::slice::from_raw_parts(bytes, n));
    }
    let h = Harness::new(31);
    assert_eq!(h.open(), 0);
    let mut page = [0; 2];
    assert_eq!(
        unsafe { xp_xtc_owner_load(h.owner, 0, page.as_mut_ptr(), 2) },
        2
    );
    assert_eq!(page, [0x5a, 0xa5]);
    assert!(h.world.opened);
    let before = h.world.allocations.len();
    let sink = XpXtcStream {
        context: h.world.0.cast(),
        emit,
    };
    assert_eq!(unsafe { xp_xtc_owner_stream(h.owner, 0, 1, &sink) }, 0);
    assert_eq!(h.world.chunks, page);
    assert_eq!(h.world.allocations.len(), before);
    assert!(h.world.opened);
}
#[test]
fn close_reopen_and_all_io_failures_release_resources_once() {
    for failure in 0..32 {
        let mut h = Harness::new(63);
        h.world.fail = failure;
        let _ = h.open();
        let _ = h.view(true);
        let mut bytes = [0; 2];
        unsafe {
            xp_xtc_owner_load(h.owner, 0, bytes.as_mut_ptr(), 2);
            xp_xtc_owner_close(h.owner);
        }
        h.world.fail = 0;
        assert_eq!(h.open(), 0);
        assert_eq!(h.view(true).chapter_count, 1);
    }
}
#[test]
fn truncated_records_keep_the_existing_error_and_partial_write_behavior() {
    for length in [0, 4, 20, 55, 56, 127, 351, 367, 389, 391] {
        let mut h = Harness::new(16);
        h.world.data.truncate(length);
        let error = h.open();
        if error == 0 {
            let mut bytes = [0xee; 2];
            let read = unsafe { xp_xtc_owner_load(h.owner, 0, bytes.as_mut_ptr(), 2) };
            assert_eq!(read, 0);
            if length == 391 {
                assert_eq!(bytes, [0x5a, 0xee]);
            }
        }
    }
}
#[test]
#[ignore = "negative control: Miri must reject a moved inline self-pointer"]
fn negative_moved_short_metadata() {
    let h = core::mem::ManuallyDrop::new(Harness::new(15));
    assert_eq!(h.open(), 0);
    unsafe {
        let moved = alloc(Layout::new::<XpXtcOwnerStorage>()).cast::<XpXtcOwnerStorage>();
        ptr::copy_nonoverlapping(h.owner, moved, 1);
        let mut view = core::mem::MaybeUninit::uninit();
        xp_xtc_owner_view(moved, false, view.as_mut_ptr());
        dealloc(h.owner.cast(), Layout::new::<XpXtcOwnerStorage>());
        let _byte = view.assume_init().title.read();
    }
}
#[test]
#[ignore = "negative control: Miri must reject a chapter loan after close"]
fn negative_chapter_loan_after_close() {
    let h = core::mem::ManuallyDrop::new(Harness::new(31));
    assert_eq!(h.open(), 0);
    unsafe {
        let name = (*h.view(true).chapters).name;
        xp_xtc_owner_close(h.owner);
        let _byte = name.read();
    }
}
#[test]
#[ignore = "negative control: Miri must reject metadata after owner drop"]
fn negative_metadata_loan_after_drop() {
    let h = core::mem::ManuallyDrop::new(Harness::new(31));
    assert_eq!(h.open(), 0);
    unsafe {
        let data = h.view(false).title;
        xp_xtc_owner_drop(h.owner);
        let _byte = data.read();
    }
}

#[test]
fn snapshot_during_multiple_name_releases_contains_only_live_rows() {
    let mut h = Harness::new(31);
    h.world.data.splice(352..352, [0; 96]);
    h.world.data[24..32].copy_from_slice(&448u64.to_le_bytes());
    h.world.data[32..40].copy_from_slice(&464u64.to_le_bytes());
    h.world.data[448..456].copy_from_slice(&464u64.to_le_bytes());
    h.world.data[352..375].fill(b'D');
    h.world.data[432] = 1;
    h.world.data[434] = 1;
    h.world.inspect_names = true;
    assert_eq!(h.open(), 0);
    assert_eq!(h.view(true).chapter_count, 2);
    unsafe {
        xp_xtc_owner_close(h.owner);
    }
}

#[test]
fn metadata_names_preserve_invalid_utf8_and_stop_at_native_nul() {
    let mut h = Harness::new(31);
    h.world.data[56..60].copy_from_slice(&[0x80, 0xff, 0, 0xaa]);
    h.world.data[256..260].copy_from_slice(&[0xfe, 0xff, 0, 0xaa]);
    assert_eq!(h.open(), 0);
    let view = h.view(true);
    assert_eq!(view.title_len, 2);
    unsafe {
        assert_eq!(core::slice::from_raw_parts(view.title, 2), &[0x80, 0xff]);
        let row = &*view.chapters;
        assert_eq!(row.name_len, 2);
        assert_eq!(core::slice::from_raw_parts(row.name, 2), &[0xfe, 0xff]);
    }
}
