//! Stable storage for one reader's render resources; not yet used by the session.
use crate::app::{abi, resources::Buffer};
use core::{
    cell::{Cell, RefCell, UnsafeCell},
    ffi::c_void,
    marker::PhantomPinned,
    mem::MaybeUninit,
    pin::Pin,
    ptr,
};

#[repr(C)]
pub(crate) struct PrewarmStorage {
    pub(crate) words: [usize; 2],
}

/// Physical callbacks must not retain a Rust reference or move scope storage.
pub(crate) trait PhysicalResources {
    unsafe fn prewarm_begin(&self, storage: *mut PrewarmStorage) -> bool;
    unsafe fn prewarm_finish(&self, storage: *mut PrewarmStorage);
    unsafe fn prewarm_drop(&self, storage: *mut PrewarmStorage);
    fn detach_source(&self);
    fn clear_plan(&self);
    fn clear_window(&self);
    fn release_images(&self);
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Scan {
    Closed,
    Scanning,
    Ended,
}

struct Prewarm {
    storage: UnsafeCell<MaybeUninit<PrewarmStorage>>,
    state: Cell<Scan>,
}
impl Prewarm {
    unsafe fn initialize(out: *mut Self) {
        ptr::addr_of_mut!((*out).storage).write(UnsafeCell::new(MaybeUninit::uninit()));
        ptr::addr_of_mut!((*out).state).write(Cell::new(Scan::Closed));
    }
    fn storage(&self) -> *mut PrewarmStorage {
        self.storage.get().cast()
    }
    fn begin(&self, physical: &dyn PhysicalResources) -> bool {
        assert!(self.state.get() == Scan::Closed);
        if !unsafe { physical.prewarm_begin(self.storage()) } {
            return false;
        }
        self.state.set(Scan::Scanning);
        true
    }
    fn finish_scan(&self, physical: &dyn PhysicalResources) {
        assert!(self.state.get() == Scan::Scanning);
        unsafe { physical.prewarm_finish(self.storage()) };
        self.state.set(Scan::Ended);
    }
    fn close(&self, physical: &dyn PhysicalResources) {
        if self.state.get() != Scan::Closed {
            unsafe { physical.prewarm_drop(self.storage()) };
            self.state.set(Scan::Closed);
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) enum PlaneBuffer {
    Lsb,
    Msb,
    Scratch,
}

// A field borrow ends before any allocator or physical callback.
struct Slot(RefCell<Option<Buffer>>);
impl Slot {
    fn empty() -> Self {
        Self(RefCell::new(None))
    }
    fn pointer(&self) -> *mut u8 {
        self.0
            .borrow()
            .as_ref()
            .map_or(ptr::null_mut(), Buffer::as_ptr)
    }
    fn allocate(&self, resources: &'static abi::Resources, bytes: usize) -> bool {
        let buffer = Buffer::allocate(resources, abi::MEMORY_DEFAULT, bytes);
        if let Some(buffer) = &buffer {
            // Native makeUniqueNoThrow<T[]> value-initializes these reservations.
            unsafe { buffer.as_ptr().write_bytes(0, bytes) };
        }
        let success = buffer.is_some();
        let old = self.0.replace(buffer);
        drop(old);
        success
    }
    fn ensure(&self, resources: &'static abi::Resources, bytes: usize) -> bool {
        !self.pointer().is_null() || self.allocate(resources, bytes)
    }
    fn release(&self) {
        let old = self.0.take();
        drop(old);
    }
}

struct Frame {
    lsb: Slot,
    msb: Slot,
    scratch: Slot,
    borrowed_scratch: Cell<*mut u8>,
    prewarm: Prewarm,
    active: Cell<bool>,
    times: Cell<[u32; 10]>,
    page: Cell<PageGeometry>,
    #[cfg(feature = "reader-strips")]
    strips: StripFrame,
}

#[derive(Clone, Copy)]
pub(crate) struct PageGeometry {
    pub(crate) arena: *const u8,
    pub(crate) length: usize,
    pub(crate) visible_offset: u32,
    pub(crate) font: i32,
    pub(crate) left: i32,
    pub(crate) top: i32,
    pub(crate) status_top: i32,
    pub(crate) images: bool,
    pub(crate) inverted: bool,
    pub(crate) context: *mut c_void,
}
impl PageGeometry {
    const EMPTY: Self = Self {
        arena: ptr::null(),
        length: 0,
        visible_offset: 0,
        font: 0,
        left: 0,
        top: 0,
        status_top: 0,
        images: false,
        inverted: false,
        context: ptr::null_mut(),
    };
}

#[cfg(feature = "reader-strips")]
struct StripFrame {
    plan: UnsafeCell<MaybeUninit<crate::page_render::Plan>>,
    source: UnsafeCell<MaybeUninit<crate::display::DisplaySource>>,
    committed_rows: Cell<u16>,
}
#[cfg(feature = "reader-strips")]
impl StripFrame {
    unsafe fn initialize(out: *mut Self) {
        ptr::addr_of_mut!((*out).plan).write(UnsafeCell::new(MaybeUninit::uninit()));
        ptr::addr_of_mut!((*out).source).write(UnsafeCell::new(MaybeUninit::uninit()));
        ptr::addr_of_mut!((*out).committed_rows).write(Cell::new(0));
    }
}
impl Frame {
    unsafe fn initialize(out: *mut Self) {
        ptr::addr_of_mut!((*out).lsb).write(Slot::empty());
        ptr::addr_of_mut!((*out).msb).write(Slot::empty());
        ptr::addr_of_mut!((*out).scratch).write(Slot::empty());
        ptr::addr_of_mut!((*out).borrowed_scratch).write(Cell::new(ptr::null_mut()));
        Prewarm::initialize(ptr::addr_of_mut!((*out).prewarm));
        ptr::addr_of_mut!((*out).active).write(Cell::new(false));
        ptr::addr_of_mut!((*out).times).write(Cell::new([0; 10]));
        ptr::addr_of_mut!((*out).page).write(Cell::new(PageGeometry::EMPTY));
        #[cfg(feature = "reader-strips")]
        StripFrame::initialize(ptr::addr_of_mut!((*out).strips));
    }
    fn buffer(&self, buffer: PlaneBuffer) -> &Slot {
        match buffer {
            PlaneBuffer::Lsb => &self.lsb,
            PlaneBuffer::Msb => &self.msb,
            PlaneBuffer::Scratch => &self.scratch,
        }
    }
}

#[cfg(feature = "reader-strips")]
struct Strips {
    scratch: Slot,
    commands: Slot,
    bounds: Slot,
    image_window: Slot,
    source_loan: Cell<bool>,
    plan_loan: Cell<bool>,
    window_loan: Cell<bool>,
}
#[cfg(feature = "reader-strips")]
impl Strips {
    const ROWS: usize = 80;
    const SCRATCH_BYTES: usize = 100 * Self::ROWS;
    const COMMAND_BYTES: usize = 16 * 1024;
    const MAX_ELEMENTS: usize = 512;
    const BOUNDS_BYTES: usize = Self::MAX_ELEMENTS * 8;
    const IMAGE_BYTES: usize = 4096;

    unsafe fn initialize(out: *mut Self) {
        ptr::addr_of_mut!((*out).scratch).write(Slot::empty());
        ptr::addr_of_mut!((*out).commands).write(Slot::empty());
        ptr::addr_of_mut!((*out).bounds).write(Slot::empty());
        ptr::addr_of_mut!((*out).image_window).write(Slot::empty());
        ptr::addr_of_mut!((*out).source_loan).write(Cell::new(false));
        ptr::addr_of_mut!((*out).plan_loan).write(Cell::new(false));
        ptr::addr_of_mut!((*out).window_loan).write(Cell::new(false));
    }
    fn loaned(&self) -> bool {
        self.source_loan.get() || self.plan_loan.get() || self.window_loan.get()
    }
    fn release_idle(&self) -> bool {
        if self.loaned() {
            return false;
        }
        let held = [
            &self.scratch,
            &self.commands,
            &self.bounds,
            &self.image_window,
        ]
        .iter()
        .any(|slot| !slot.pointer().is_null());
        self.scratch.release();
        self.commands.release();
        self.bounds.release();
        self.image_window.release();
        held
    }
    fn prepare(&self, resources: &'static abi::Resources, stride: usize, images: bool) -> bool {
        if self.loaned() || !matches!(stride, 99 | 100) {
            return false;
        }
        // Native admission tries every missing reservation before testing success.
        let scratch = self.scratch.ensure(resources, Self::SCRATCH_BYTES);
        let commands = self.commands.ensure(resources, Self::COMMAND_BYTES);
        let bounds = self.bounds.ensure(resources, Self::BOUNDS_BYTES);
        let image = !images || self.image_window.ensure(resources, Self::IMAGE_BYTES);
        if scratch && commands && bounds && image {
            return true;
        }
        self.release_idle();
        false
    }
}

pub(crate) struct PageResources {
    frame: Frame,
    #[cfg(feature = "reader-strips")]
    strips: Strips,
    idle_prewarm: Prewarm,
    pinned: PhantomPinned,
}
impl PageResources {
    /// Initialize directly in the session allocation, before publication.
    ///
    /// # Safety
    /// `out` is aligned, writable, uninitialized storage for Self. After this call
    /// it must be pinned before use and dropped before its allocation is released.
    pub(crate) unsafe fn initialize(out: *mut Self) {
        Frame::initialize(ptr::addr_of_mut!((*out).frame));
        #[cfg(feature = "reader-strips")]
        Strips::initialize(ptr::addr_of_mut!((*out).strips));
        Prewarm::initialize(ptr::addr_of_mut!((*out).idle_prewarm));
        ptr::addr_of_mut!((*out).pinned).write(PhantomPinned);
    }
    pub(crate) fn begin_frame<'a>(
        self: Pin<&'a Self>,
        arena: &'a [u8],
        physical: &'a dyn PhysicalResources,
    ) -> FrameLease<'a> {
        assert!(!self.frame.active.get());
        assert!(self.idle_prewarm.state.get() == Scan::Closed);
        self.frame.active.set(true);
        self.frame.times.set([0; 10]);
        self.frame.page.set(PageGeometry {
            arena: arena.as_ptr(),
            length: arena.len(),
            ..PageGeometry::EMPTY
        });
        #[cfg(feature = "reader-strips")]
        self.frame.strips.committed_rows.set(0);
        FrameLease {
            owner: self,
            arena,
            physical,
        }
    }
    pub(crate) fn begin_idle_prewarm<'a>(
        self: Pin<&'a Self>,
        physical: &'a dyn PhysicalResources,
    ) -> Option<PrewarmLease<'a>> {
        assert!(!self.frame.active.get());
        self.idle_prewarm.begin(physical).then_some(PrewarmLease {
            owner: self,
            physical,
        })
    }
    #[cfg(feature = "reader-strips")]
    pub(crate) fn prepare_strips(
        self: Pin<&Self>,
        resources: &'static abi::Resources,
        stride: usize,
        images: bool,
    ) -> bool {
        if !self.frame.borrowed_scratch.get().is_null() {
            return false;
        }
        self.strips.prepare(resources, stride, images)
    }
    #[cfg(feature = "reader-strips")]
    pub(crate) fn release_idle_strips(self: Pin<&Self>) -> bool {
        if !self.frame.borrowed_scratch.get().is_null() {
            return false;
        }
        self.strips.release_idle()
    }
}
impl Drop for PageResources {
    fn drop(&mut self) {
        assert!(!self.frame.active.get());
        assert!(self.frame.prewarm.state.get() == Scan::Closed);
        #[cfg(feature = "reader-strips")]
        {
            assert!(!self.strips.loaned());
            self.strips.release_idle();
        }
        assert!(self.idle_prewarm.state.get() == Scan::Closed);
    }
}

pub(crate) struct PrewarmLease<'a> {
    owner: Pin<&'a PageResources>,
    physical: &'a dyn PhysicalResources,
}
impl PrewarmLease<'_> {
    pub(crate) fn finish_scan(&self) {
        self.owner.idle_prewarm.finish_scan(self.physical);
    }
}
impl Drop for PrewarmLease<'_> {
    fn drop(&mut self) {
        self.owner.idle_prewarm.close(self.physical);
    }
}

pub(crate) struct FrameLease<'a> {
    owner: Pin<&'a PageResources>,
    arena: &'a [u8],
    physical: &'a dyn PhysicalResources,
}
impl FrameLease<'_> {
    pub(crate) fn allocate(
        &self,
        resources: &'static abi::Resources,
        buffer: PlaneBuffer,
        bytes: usize,
    ) -> bool {
        self.owner.frame.buffer(buffer).allocate(resources, bytes)
    }
    pub(crate) fn buffer(&self, buffer: PlaneBuffer) -> *mut u8 {
        let frame = &self.owner.frame;
        if matches!(buffer, PlaneBuffer::Scratch) && !frame.borrowed_scratch.get().is_null() {
            frame.borrowed_scratch.get()
        } else {
            frame.buffer(buffer).pointer()
        }
    }
    pub(crate) fn begin_prewarm(&self) -> bool {
        self.owner.frame.prewarm.begin(self.physical)
    }
    pub(crate) fn finish_scan(&self) {
        self.owner.frame.prewarm.finish_scan(self.physical);
    }
    #[cfg(feature = "reader-strips")]
    pub(crate) fn borrow_scratch(&self) -> bool {
        let pointer = self.owner.strips.scratch.pointer();
        self.owner.frame.borrowed_scratch.set(pointer);
        !pointer.is_null()
    }
    #[cfg(feature = "reader-strips")]
    pub(crate) fn source_installed(&self, installed: bool) {
        assert!(!self.owner.strips.source_loan.get());
        self.owner.strips.source_loan.set(installed);
    }
    #[cfg(feature = "reader-strips")]
    pub(crate) fn plan_installed(&self) {
        self.owner.strips.plan_loan.set(true);
    }
    #[cfg(feature = "reader-strips")]
    pub(crate) fn window_installed(&self) {
        self.owner.strips.window_loan.set(true);
    }
}
impl Drop for FrameLease<'_> {
    fn drop(&mut self) {
        let frame = &self.owner.frame;
        frame.scratch.release();
        frame.msb.release();
        frame.lsb.release();
        #[cfg(feature = "reader-strips")]
        {
            let strips = &self.owner.strips;
            if strips.source_loan.get() {
                self.physical.detach_source();
            }
            strips.source_loan.set(false);
            frame.borrowed_scratch.set(ptr::null_mut());
            self.physical.clear_plan();
            strips.plan_loan.set(false);
            self.physical.clear_window();
            strips.window_loan.set(false);
        }
        frame.prewarm.close(self.physical);
        self.physical.release_images();
        frame.page.set(PageGeometry::EMPTY);
        frame.active.set(false);
    }
}
