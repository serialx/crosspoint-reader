//! Rust-owned load result with process-wide interval sharing.
//! The existing loader remains the only implementation of .cpfont decoding.
use crate::sdfont::{load, pool, Diagnostic};
use core::{cell::UnsafeCell, ffi::c_void, mem, ptr};

/// Synchronous file positioned at zero. A read returns the actual byte count.
pub trait Reader {
    fn seek(&mut self, offset: u32) -> bool;
    fn read(&mut self, bytes: &mut [u8]) -> usize;
    fn diagnostic(&mut self, _: u32, _: u32, _: u32, _: u32) {}
    fn parse_error(_: &Diagnostic, _: u32, _: u32) {}
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Allocator {
    pub styles: unsafe extern "C" fn(usize) -> *mut u8,
    pub resident: unsafe extern "C" fn(usize) -> *mut u8,
    pub free_styles: unsafe extern "C" fn(*mut u8, usize),
    pub free_resident: unsafe extern "C" fn(*mut u8, bool),
}

/// One process registry; loads and drops must be serialized by its owner.
#[repr(transparent)]
pub struct IntervalRegistry(UnsafeCell<[load::Shared; 16]>);
impl Default for IntervalRegistry {
    fn default() -> Self {
        Self::new()
    }
}
impl IntervalRegistry {
    pub const fn new() -> Self {
        Self(UnsafeCell::new(
            [const {
                load::Shared {
                    table: ptr::null(),
                    count: 0,
                    bmp16: false,
                    refs: 0,
                }
            }; 16],
        ))
    }
    fn pointer(&self) -> *mut load::Shared {
        self.0.get().cast()
    }
}
/// Stable style buffers with borrowed, process-wide interval sharing.
#[repr(C)]
pub struct ParsedFont<'a> {
    pub(crate) styles: *mut pool::Resident,
    count: u8,
    pub(crate) slots: [u8; 4],
    hash: u32,
    shared: &'a IntervalRegistry,
    pub(crate) allocator: Allocator,
}
struct Context<'r, 's, R> {
    font: *mut ParsedFont<'s>,
    reader: &'r mut R,
}
impl<'a> ParsedFont<'a> {
    /// # Safety
    /// Allocators return null or unique, writable, suitably aligned storage for
    /// Resident and resident table types, respectively. Paired frees accept null
    /// and release exactly once; callbacks neither unwind nor reenter. All loads
    /// and drops sharing this registry are serialized; no table loan spans mutation.
    pub unsafe fn load<R: Reader>(
        reader: &mut R,
        allocator: Allocator,
        shared: &'a IntervalRegistry,
    ) -> Option<Self> {
        let mut font = Self {
            styles: ptr::null_mut(),
            count: 0,
            slots: [0; 4],
            hash: 0,
            shared,
            allocator,
        };
        let mut context = Context {
            font: &mut font,
            reader,
        };
        let services = load::Services {
            ctx: (&mut context as *mut Context<'_, '_, R>).cast(),
            read: read::<R>,
            alloc_styles: styles::<R>,
            alloc: allocator.resident,
            cleanup: cleanup::<R>,
            setup,
            diagnostic: diagnostic::<R>,
            parse_error: parse_error::<R>,
        };
        // No address into this movable owner escapes the synchronous loader.
        let f = context.font;
        let ok = load::load(
            ptr::addr_of_mut!((*f).slots).cast(),
            ptr::addr_of_mut!((*f).hash),
            (*f).shared.pointer(),
            &services,
        );
        ok.then_some(font)
    }
    pub fn content_hash(&self) -> u32 {
        self.hash
    }
    pub fn style_count(&self) -> u8 {
        self.count
    }
    pub fn style(&self, id: u8) -> Option<&pool::Resident> {
        let slot = *self.slots.get(usize::from(id))?;
        if slot == 0 {
            None
        } else {
            Some(unsafe { &*self.styles.add(usize::from(slot - 1)) })
        }
    }
    unsafe fn clear(&mut self) {
        let services = pool::Services {
            context: ptr::null_mut(),
            open,
            close,
            read: unused_read,
            alloc: unused_alloc,
            free: self.allocator.free_resident,
            heap,
            clock,
            corrupt,
            diagnostic: None,
        };
        for i in 0..self.count {
            let style = &mut *self.styles.add(usize::from(i));
            style.free_mini(&services);
            load::release(style, self.shared.pointer(), &services);
            style.free_ligatures(&services);
            (self.allocator.free_resident)(style.kern_block_index.cast(), true);
        }
        (self.allocator.free_styles)(
            self.styles.cast(),
            usize::from(self.count) * mem::size_of::<pool::Resident>(),
        );
        self.styles = ptr::null_mut();
        self.count = 0;
        self.slots = [0; 4];
        self.hash = 0;
    }
}
impl Drop for ParsedFont<'_> {
    fn drop(&mut self) {
        unsafe { self.clear() }
    }
}
unsafe extern "C" fn read<R: Reader>(
    raw: *mut c_void,
    offset: u32,
    dest: *mut u8,
    len: usize,
    seek: bool,
) -> i32 {
    let ctx = &mut *raw.cast::<Context<'_, '_, R>>();
    if seek && !ctx.reader.seek(offset) {
        return -1;
    }
    if dest.is_null() && len == 0 {
        return 1;
    }
    i32::from(ctx.reader.read(core::slice::from_raw_parts_mut(dest, len)) == len)
}
unsafe extern "C" fn styles<R>(raw: *mut c_void, count: u8) -> *mut pool::Resident {
    let f = (*raw.cast::<Context<'_, '_, R>>()).font;
    let bytes = usize::from(count) * mem::size_of::<pool::Resident>();
    let data = ((*f).allocator.styles)(bytes).cast::<pool::Resident>();
    if !data.is_null() {
        // Resident contains only integers, raw pointers and booleans.
        ptr::write_bytes(data, 0, usize::from(count));
        (*f).styles = data;
        (*f).count = count;
    }
    data
}
unsafe extern "C" fn cleanup<R>(raw: *mut c_void) {
    (*(*raw.cast::<Context<'_, '_, R>>()).font).clear();
}
unsafe extern "C" fn setup(_: *mut c_void, _: u8) {}
unsafe extern "C" fn diagnostic<R: Reader>(raw: *mut c_void, event: u32, a: u32, b: u32, c: u32) {
    (*raw.cast::<Context<'_, '_, R>>())
        .reader
        .diagnostic(event, a, b, c);
}
unsafe extern "C" fn parse_error<R: Reader>(detail: *const Diagnostic, style: u32, index: u32) {
    R::parse_error(&*detail, style, index);
}
unsafe extern "C" fn open(_: *mut c_void) -> bool {
    false
}
unsafe extern "C" fn close(_: *mut c_void, _: bool) {}
unsafe extern "C" fn unused_read(_: *mut c_void, _: u32, _: *mut u8, _: usize, _: bool) -> bool {
    false
}
unsafe extern "C" fn unused_alloc(_: usize, _: bool) -> *mut u8 {
    ptr::null_mut()
}
unsafe extern "C" fn heap(_: bool) -> u32 {
    0
}
unsafe extern "C" fn clock() -> u32 {
    0
}
unsafe extern "C" fn corrupt(_: *mut c_void, _: *const crate::font::raw::Glyph, _: u32) {}
