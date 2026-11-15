//! Stable registration cells; movable tables retain only pointers to this owner.
use super::{Family, Memory, Pressure, Rasterizer, Source};
use crate::font::{
    loaded::Entry,
    raw::{FontData, Glyph},
};
use core::{cell::UnsafeCell, ffi::c_void, pin::Pin, ptr};

struct Context<'a, P: Rasterizer, M: Memory, E: Pressure> {
    owner: *const Registered<'a, P, M, E>,
    style: usize,
}
pub struct Registered<'a, P: Rasterizer, M: Memory, E: Pressure> {
    family: UnsafeCell<Family<'a, P, M>>,
    pressure: UnsafeCell<E>,
    contexts: [Context<'a, P, M, E>; 4],
    data: [UnsafeCell<FontData>; 4],
    slots: [*const FontData; 4],
}
impl<'a, P: Rasterizer, M: Memory, E: Pressure> Registered<'a, P, M, E> {
    /// # Safety
    /// Out is exclusive uninitialized storage, stable until destruction. Sources
    /// outlive all registrations; pressure excludes this owner. Calls are serialized
    /// and no registry/glyph loan survives mutation or unregistration/destruction.
    pub unsafe fn initialize(out: *mut Self, sources: [Source<'a>; 4], memory: M, pressure: E) {
        Family::<P, M>::initialize(ptr::addr_of_mut!((*out).family).cast(), sources, memory);
        ptr::addr_of_mut!((*out).pressure).write(UnsafeCell::new(pressure));
        for style in 0..4 {
            let context = ptr::addr_of_mut!((*out).contexts)
                .cast::<Context<'a, P, M, E>>()
                .add(style);
            context.write(Context { owner: out, style });
            let data = ptr::addr_of_mut!((*out).data)
                .cast::<UnsafeCell<FontData>>()
                .add(style);
            let mut value: FontData = core::mem::zeroed();
            value.is2_bit = true;
            value.glyph_miss_ctx = context.cast();
            value.glyph_miss_handler = Some(Self::glyph);
            value.vector_bitmap_handler = Some(Self::bitmap);
            value.coverage_handler = Some(Self::coverage);
            value.kern_handler = Some(Self::kern);
            data.write(UnsafeCell::new(value));
            ptr::addr_of_mut!((*out).slots)
                .cast::<*const FontData>()
                .add(style)
                .write((*data).get());
        }
    }
    /// # Safety
    /// The initialized owner obeys initialize's pinned, serialized loan contract.
    pub unsafe fn entry(owner: *const Self, id: i32) -> Entry {
        Entry {
            id,
            slots: core::array::from_fn(|i| {
                ptr::addr_of!((*owner).slots)
                    .cast::<*const FontData>()
                    .add(i)
            }),
            fallback: 0,
        }
    }
    unsafe fn publish(owner: *const Self) {
        let family = &*(*owner).family.get();
        for style in 0..4 {
            let data = (*owner).data[style].get();
            let metrics = family.metrics(style);
            (*data).ascender = metrics.ascent;
            (*data).advance_y = metrics.height;
            (*data).descender = metrics.descent;
            let ligatures = family.ligatures(style);
            (*data).ligature_pairs = if ligatures.is_empty() {
                ptr::null()
            } else {
                ligatures.as_ptr()
            };
            (*data).ligature_pair_count = ligatures.len() as u32;
        }
    }
    /// # Safety
    /// Same owner/loan contract as initialize; previous registry loans have ended.
    pub unsafe fn load(owner: *const Self, points: u16, cap: usize, maximum: u16) -> bool {
        let loaded =
            Pin::new_unchecked(&mut *(*owner).family.get()).load(points, true, cap, maximum);
        Self::publish(owner);
        loaded
    }
    /// # Safety
    /// Same owner/loan contract as initialize; registry invalidates before this call.
    pub unsafe fn clear(owner: *const Self, resident: bool) {
        Pin::new_unchecked(&mut *(*owner).family.get()).clear(resident);
        Self::publish(owner);
    }
    /// # Safety
    /// Same owner/loan contract as initialize; text is disjoint from owned caches.
    pub unsafe fn prewarm(owner: *const Self, text: Option<&[u8]>, reset: bool) -> bool {
        let result = Pin::new_unchecked(&mut *(*owner).family.get()).prewarm(
            text,
            reset,
            &mut *(*owner).pressure.get(),
        );
        Self::publish(owner);
        result
    }
    unsafe extern "C" fn glyph(context: *mut c_void, cp: u32) -> *const Glyph {
        let context = &*context.cast::<Context<'a, P, M, E>>();
        let owner = context.owner;
        let result = Pin::new_unchecked(&mut *(*owner).family.get())
            .glyph(context.style, cp, &mut *(*owner).pressure.get())
            .map_or(ptr::null(), |loan| loan.glyph as *const Glyph);
        Self::publish(owner);
        result
    }
    unsafe extern "C" fn bitmap(context: *mut c_void, glyph: *const Glyph) -> *const u8 {
        if glyph.is_null() || (*glyph).data_length == 0 {
            return ptr::null();
        }
        let context = &*context.cast::<Context<'a, P, M, E>>();
        (&*(*context.owner).family.get())
            .bitmap(context.style, &*glyph)
            .map_or(ptr::null(), |bytes| bytes.as_ptr())
    }
    unsafe extern "C" fn coverage(context: *mut c_void, cp: u32) -> bool {
        let owner = (*context.cast::<Context<'a, P, M, E>>()).owner;
        Pin::new_unchecked(&mut *(*owner).family.get()).coverage(cp)
    }
    unsafe extern "C" fn kern(context: *mut c_void, left: u32, right: u32) -> i8 {
        let context = &*context.cast::<Context<'a, P, M, E>>();
        let value = Pin::new_unchecked(&mut *(*context.owner).family.get()).kern(
            context.style,
            left,
            right,
        );
        Self::publish(context.owner);
        value
    }
}
