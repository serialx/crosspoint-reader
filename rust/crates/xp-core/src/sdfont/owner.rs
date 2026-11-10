//! Stable SD face storage over the streaming loader and resident pool.
use super::{
    io,
    parser::{Allocator, IntervalRegistry, ParsedFont},
};
use crate::{font::raw::Advance, sdfont::pool};
use core::{
    ffi::{c_void, CStr},
    marker::PhantomData,
    mem::{self, ManuallyDrop, MaybeUninit},
    pin::Pin,
    ptr::{self, NonNull},
};

#[repr(C)]
struct Face<'a> {
    parsed: MaybeUninit<ParsedFont<'a>>,
    path: [u8; 128],
    overflow: [pool::Overflow; 8],
    overflow_count: u32,
    overflow_next: u32,
    advances: [*mut Advance; 4],
    advance_sizes: [u32; 4],
    advance_capacities: [u32; 4],
    stats: pool::Stats,
    file_size: u32,
    loaded: bool,
    free_owner: MaybeUninit<unsafe extern "C" fn(*mut u8, usize)>,
    generation: *const core::sync::atomic::AtomicU32,
    corrupt_logged: u8,
}
// Keep the native allocation request until the switch separately measures shrinkage.
const OWNER_BYTES: usize = if mem::size_of::<Face<'_>>() > 484 {
    mem::size_of::<Face<'_>>()
} else {
    484
};
const _: () = assert!(!cfg!(target_pointer_width = "32") || mem::size_of::<Face<'_>>() <= 484);

#[repr(transparent)]
pub struct FaceOwner<'a> {
    face: NonNull<Face<'a>>,
    registry: PhantomData<&'a IntervalRegistry>,
}
impl<'a> FaceOwner<'a> {
    /// # Safety
    /// Allocator and serialized registry follow ParsedFont's contract. The styles
    /// allocator also accepts OWNER_BYTES and alignment of Face. No loan survives
    /// a mutation or drop; unregister the face before dropping its sole owner.
    pub unsafe fn load<P: io::Physical>(
        path: &CStr,
        mut files: Pin<&mut io::Files<P>>,
        allocator: Allocator,
        shared: &'a IntervalRegistry,
    ) -> Option<Self> {
        let face = NonNull::new((allocator.styles)(OWNER_BYTES).cast::<Face<'a>>())?;
        // Non-zero-valid members are explicitly uninitialized here.
        ptr::write_bytes(face.as_ptr(), 0, 1);
        ptr::addr_of_mut!((*face.as_ptr()).free_owner)
            .write(MaybeUninit::new(allocator.free_styles));
        let mut owner = Self {
            face,
            registry: PhantomData,
        };
        let path_bytes = path.to_bytes();
        if path_bytes.len() >= 128 {
            return None;
        }
        ptr::copy_nonoverlapping(
            path_bytes.as_ptr(),
            ptr::addr_of_mut!((*face.as_ptr()).path).cast(),
            path_bytes.len(),
        );
        let mut reader = files.as_mut().reader(path)?;
        let size = reader.size;
        let parsed = ParsedFont::load(&mut reader, allocator, shared)?;
        let data = owner.face.as_mut();
        data.parsed.write(parsed);
        data.file_size = size;
        data.loaded = true;
        Some(owner)
    }
    pub fn parsed(&self) -> &ParsedFont<'a> {
        unsafe { self.face.as_ref().parsed.assume_init_ref() }
    }
    pub fn path(&self) -> &CStr {
        unsafe { CStr::from_ptr(self.face.as_ref().path.as_ptr().cast()) }
    }
    pub fn address(&self) -> *const c_void {
        self.face.as_ptr().cast()
    }
    /// # Safety
    /// Handler calls are serialized with the owner; generation outlives the face.
    /// A context is borrowed only until the next pool mutation or face removal.
    pub unsafe fn bind(
        &mut self,
        generation: *const core::sync::atomic::AtomicU32,
        miss: unsafe extern "C" fn(*mut c_void, u32) -> *const crate::font::raw::Glyph,
    ) {
        self.face.as_mut().generation = generation;
        self.invalidate();
        for style in 0..4 {
            if self.parsed().slots[style] == 0 {
                continue;
            }
            let parsed = self.face.as_mut().parsed.assume_init_mut();
            let resident = parsed.styles.add(usize::from(parsed.slots[style] - 1));
            ptr::addr_of_mut!((*resident).stub.glyph_miss_handler).write(Some(miss));
            // Four-byte alignment leaves the style bits free without losing provenance.
            ptr::addr_of_mut!((*resident).stub.glyph_miss_ctx).write(
                self.face
                    .as_ptr()
                    .cast::<c_void>()
                    .map_addr(|address| address | style),
            );
            ptr::addr_of_mut!((*resident).stub.coverage_handler).write(Some(coverage));
            ptr::addr_of_mut!((*resident).font).write(ptr::addr_of!((*resident).stub));
        }
    }
    /// # Safety
    /// Context came from bind and its owning face is live and exclusively lent.
    /// The returned wrapper is a loan: it must never be taken out of ManuallyDrop.
    pub unsafe fn from_context(context: *mut c_void) -> (ManuallyDrop<Self>, usize) {
        (
            ManuallyDrop::new(Self {
                face: NonNull::new_unchecked(context.map_addr(|address| address & !3).cast()),
                registry: PhantomData,
            }),
            context.addr() & 3,
        )
    }
    pub fn invalidate(&self) {
        unsafe {
            if let Some(generation) = self.face.as_ref().generation.as_ref() {
                use core::sync::atomic::Ordering::Relaxed;
                generation.store(generation.load(Relaxed).wrapping_add(1), Relaxed);
            }
        }
    }
    pub fn report_corrupt(&mut self) -> (bool, bool) {
        let count = unsafe { &mut *ptr::addr_of_mut!((*self.face.as_ptr()).corrupt_logged) };
        if *count >= 3 {
            return (false, false);
        }
        let first = *count == 0;
        *count += 1;
        (first, true)
    }
    /// # Safety
    /// Family and source loans end before any subsequent face/cache mutation.
    pub unsafe fn metric_view(&mut self, family: &mut crate::textm::native::Family) {
        let resolved = self.with_cache(&unused_services(), |cache| {
            core::array::from_fn::<_, 4, _>(|style| cache.resolve(style))
        });
        let face = self.face.as_ref();
        family.sd = 1;
        family.advance_table = u8::from(face.advance_sizes.iter().any(|&size| size != 0));
        for (style, resolved) in resolved.into_iter().enumerate() {
            family.resolved[style] = resolved as u8;
            let Some(s) = self.parsed().style(resolved as u8) else {
                continue;
            };
            family.styles[style] = crate::font::SdView {
                data: s.font,
                advances: face.advances[resolved],
                advance_count: face.advance_sizes[resolved],
                full_intervals: s.full_intervals,
                bmp_intervals: if s.intervals_are_bmp16 {
                    s.bmp_intervals
                } else {
                    ptr::null()
                },
                coverage_count: s.header.interval_count,
            };
        }
    }
    /// # Safety
    /// Font belongs to this face; the returned bitmap/source loan is synchronous.
    pub unsafe fn bitmap_view(
        &self,
        font: *const crate::font::raw::FontData,
        source: &mut crate::glyph::BitmapSource,
    ) {
        let face = self.face.as_ref();
        source.overflow_first = ptr::addr_of!(face.overflow[0].glyph);
        source.overflow_stride = mem::size_of::<pool::Overflow>() as u32;
        source.overflow_count = face.overflow_count;
        for style in 0..4 {
            let Some(s) = self.parsed().style(style) else {
                continue;
            };
            if ptr::eq(font, ptr::addr_of!(s.mini)) {
                source.glyph_count = s.glyph_count;
                source.bitmap_len = s.bitmap_capacity;
                break;
            }
        }
    }
    /// # Safety
    /// Glyph and font are synchronous loans from this live face.
    pub unsafe fn bitmap(
        &self,
        font: *const crate::font::raw::FontData,
        glyph: *const crate::font::raw::Glyph,
    ) -> *const u8 {
        let face = self.face.as_ref();
        for entry in &face.overflow[..face.overflow_count as usize] {
            if ptr::eq(ptr::addr_of!(entry.glyph), glyph) {
                return entry.bitmap;
            }
        }
        (*font).bitmap.add((*glyph).data_offset as usize)
    }
    pub fn font_slot(&self, style: u8) -> *const *const crate::font::raw::FontData {
        if style >= 4 {
            return ptr::null();
        }
        unsafe {
            let parsed = (*self.face.as_ptr()).parsed.as_ptr();
            let slot = ptr::addr_of!((*parsed).slots)
                .cast::<u8>()
                .add(usize::from(style))
                .read();
            if slot == 0 {
                return ptr::null();
            }
            // Retain allocation provenance across later Resident mutations.
            ptr::addr_of!((*(*parsed).styles.add(usize::from(slot - 1))).font)
        }
    }
    /// # Safety
    /// Physical services remain live for this operation and do not reenter. No
    /// glyph/source loan may survive another pool mutation or owner destruction.
    pub unsafe fn with_cache<R>(
        &mut self,
        services: &pool::Services,
        operation: impl FnOnce(&mut pool::Cache) -> R,
    ) -> R {
        let face = self.face.as_ptr();
        let parsed = ptr::addr_of_mut!((*face).parsed).cast::<ParsedFont<'_>>();
        let mut cache = pool::Cache {
            styles: (*parsed).styles,
            slots: ptr::addr_of!((*parsed).slots).cast(),
            advances: ptr::addr_of_mut!((*face).advances).cast(),
            advance_sizes: ptr::addr_of_mut!((*face).advance_sizes).cast(),
            advance_capacities: ptr::addr_of_mut!((*face).advance_capacities).cast(),
            overflow: ptr::addr_of_mut!((*face).overflow).cast(),
            overflow_count: ptr::addr_of_mut!((*face).overflow_count),
            overflow_next: ptr::addr_of_mut!((*face).overflow_next),
            stats: ptr::addr_of_mut!((*face).stats),
            file_size: (*face).file_size,
            loaded: (*face).loaded,
            services,
        };
        let result = operation(&mut cache);
        // Pool references are scoped to the operation. Publish addresses from
        // the allocation pointer, not a temporary Resident reference's retag.
        for style in 0..4 {
            let slot = *cache.slots.add(style);
            if slot == 0 {
                continue;
            }
            let resident = cache.styles.add(usize::from(slot - 1));
            let stub = ptr::addr_of!((*resident).stub);
            let mini = ptr::addr_of!((*resident).mini);
            let data = (*resident).font;
            if data.addr() == stub.addr() {
                ptr::addr_of_mut!((*resident).font).write(stub);
            } else if data.addr() == mini.addr() {
                ptr::addr_of_mut!((*resident).font).write(mini);
            }
        }
        result
    }
}
unsafe extern "C" fn coverage(context: *mut c_void, cp: u32) -> bool {
    let (mut face, style) = FaceOwner::from_context(context);
    face.with_cache(&unused_services(), |cache| cache.coverage(style, cp))
}
fn unused_services() -> pool::Services {
    pool::Services {
        context: ptr::null_mut(),
        open,
        close,
        read,
        alloc,
        free: unused_free,
        heap,
        clock,
        corrupt,
        diagnostic: None,
    }
}
unsafe extern "C" fn unused_free(_: *mut u8, _: bool) {}
impl Drop for FaceOwner<'_> {
    fn drop(&mut self) {
        unsafe {
            let free_owner = self.face.as_ref().free_owner.assume_init();
            if self.face.as_ref().loaded {
                let free_resident = self
                    .face
                    .as_ref()
                    .parsed
                    .assume_init_ref()
                    .allocator
                    .free_resident;
                let services = pool::Services {
                    context: ptr::null_mut(),
                    open,
                    close,
                    read,
                    alloc,
                    free: free_resident,
                    heap,
                    clock,
                    corrupt,
                    diagnostic: None,
                };
                self.with_cache(&services, |pool| {
                    pool.clear_overflow();
                    pool.clear_advances();
                });
                self.face.as_mut().parsed.assume_init_drop();
            }
            free_owner(self.face.as_ptr().cast(), OWNER_BYTES);
        }
    }
}
unsafe extern "C" fn open(_: *mut c_void) -> bool {
    false
}
unsafe extern "C" fn close(_: *mut c_void, _: bool) {}
unsafe extern "C" fn read(_: *mut c_void, _: u32, _: *mut u8, _: usize, _: bool) -> bool {
    false
}
unsafe extern "C" fn alloc(_: usize, _: bool) -> *mut u8 {
    ptr::null_mut()
}
unsafe extern "C" fn heap(_: bool) -> u32 {
    0
}
unsafe extern "C" fn clock() -> u32 {
    0
}
unsafe extern "C" fn corrupt(_: *mut c_void, _: *const crate::font::raw::Glyph, _: u32) {}

const _: () = assert!(mem::size_of::<FaceOwner<'_>>() == mem::size_of::<usize>());
