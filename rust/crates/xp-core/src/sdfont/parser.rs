//! Dormant Rust-owned load result. Not included in the firmware module tree.
//! The existing loader remains the only implementation of .cpfont decoding.
use crate::sdfont::{load, pool, Diagnostic};
use core::{ffi::c_void, mem, ptr};

/// Synchronous file positioned at zero. A read returns the actual byte count.
pub trait Reader {
    fn seek(&mut self, offset: u32) -> bool;
    fn read(&mut self, bytes: &mut [u8]) -> usize;
}

#[derive(Clone, Copy)]
pub struct Allocator {
    pub styles: unsafe extern "C" fn(usize) -> *mut u8,
    pub resident: unsafe extern "C" fn(usize) -> *mut u8,
    pub free_styles: unsafe extern "C" fn(*mut u8, usize),
    pub free_resident: unsafe extern "C" fn(*mut u8, bool),
}

/// Owns load-time tables for one file, including sharing between its styles.
/// Cross-file sharing belongs to the process owner at the integration checkpoint.
pub struct ParsedFont {
    styles: *mut pool::Resident,
    count: u8,
    slots: [u8; 4],
    hash: u32,
    shared: [load::Shared; 16],
    allocator: Allocator,
}
struct Context<'a, R> {
    font: *mut ParsedFont,
    reader: &'a mut R,
}
impl ParsedFont {
    /// # Safety
    /// Allocators return null or unique, writable, suitably aligned storage for
    /// Resident and resident table types, respectively. Paired frees accept null
    /// and release exactly once; callbacks neither unwind nor reenter this owner.
    pub unsafe fn load<R: Reader>(reader: &mut R, allocator: Allocator) -> Option<Self> {
        let mut font = Self {
            styles: ptr::null_mut(),
            count: 0,
            slots: [0; 4],
            hash: 0,
            shared: core::array::from_fn(|_| load::Shared {
                table: ptr::null(),
                count: 0,
                bmp16: false,
                refs: 0,
            }),
            allocator,
        };
        let mut context = Context {
            font: &mut font,
            reader,
        };
        let services = load::Services {
            ctx: (&mut context as *mut Context<'_, R>).cast(),
            read: read::<R>,
            alloc_styles: styles::<R>,
            alloc: allocator.resident,
            cleanup: cleanup::<R>,
            setup,
            diagnostic,
            parse_error,
        };
        // No address into this movable owner escapes the synchronous loader.
        let f = context.font;
        let ok = load::load(
            ptr::addr_of_mut!((*f).slots).cast(),
            ptr::addr_of_mut!((*f).hash),
            ptr::addr_of_mut!((*f).shared).cast(),
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
            load::release(style, self.shared.as_mut_ptr(), &services);
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
impl Drop for ParsedFont {
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
    let ctx = &mut *raw.cast::<Context<'_, R>>();
    if seek && !ctx.reader.seek(offset) {
        return -1;
    }
    if dest.is_null() && len == 0 {
        return 1;
    }
    i32::from(ctx.reader.read(core::slice::from_raw_parts_mut(dest, len)) == len)
}
unsafe extern "C" fn styles<R>(raw: *mut c_void, count: u8) -> *mut pool::Resident {
    let f = (*raw.cast::<Context<'_, R>>()).font;
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
    (*(*raw.cast::<Context<'_, R>>()).font).clear();
}
unsafe extern "C" fn setup(_: *mut c_void, _: u8) {}
unsafe extern "C" fn diagnostic(_: *mut c_void, _: u32, _: u32, _: u32, _: u32) {}
unsafe extern "C" fn parse_error(_: *const Diagnostic, _: u32, _: u32) {}
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
