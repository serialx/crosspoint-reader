//! Dormant stable face storage over the existing streaming loader and pool.
use super::{
    io,
    parser::{Allocator, IntervalRegistry, ParsedFont},
};
use crate::{font::raw::Advance, sdfont::pool};
use core::{
    ffi::{c_void, CStr},
    marker::PhantomData,
    mem::{self, MaybeUninit},
    pin::Pin,
    ptr::{self, NonNull},
};

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
}
// Keep the native allocation request until the switch separately measures shrinkage.
const OWNER_BYTES: usize = if mem::size_of::<Face<'_>>() > 484 {
    mem::size_of::<Face<'_>>()
} else {
    484
};
const _: () = assert!(!cfg!(target_pointer_width = "32") || mem::size_of::<Face<'_>>() <= 484);

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
        let face = self.face.as_mut();
        let parsed = face.parsed.assume_init_mut();
        let mut cache = pool::Cache {
            styles: parsed.styles,
            slots: parsed.slots.as_ptr(),
            advances: face.advances.as_mut_ptr(),
            advance_sizes: face.advance_sizes.as_mut_ptr(),
            advance_capacities: face.advance_capacities.as_mut_ptr(),
            overflow: face.overflow.as_mut_ptr(),
            overflow_count: &mut face.overflow_count,
            overflow_next: &mut face.overflow_next,
            stats: &mut face.stats,
            file_size: face.file_size,
            loaded: face.loaded,
            services,
        };
        operation(&mut cache)
    }
}
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
