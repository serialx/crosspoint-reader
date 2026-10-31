//! Read-only bitmap sources. Misses are resolved by the C++ owner after the loan ends.
use crate::font::raw::{FontData, Glyph};
use core::{
    ptr,
    sync::atomic::{AtomicU32, Ordering},
};

/// Mirrors FontDecompressor::PageGlyphEntry, including its unused extraction offset.
#[repr(C)]
pub struct PageEntry {
    pub glyph_index: u32,
    pub buffer_offset: u32,
    pub aligned_offset: u32,
}
/// Mirrors FontDecompressor::PageSlot without copying the page lookup table.
#[repr(C)]
pub struct PageSlot {
    pub buffer: *const u8,
    pub font: *const FontData,
    pub entries: *const PageEntry,
    pub count: u16,
}
/// Rebuild after generation changes, including FontDecompressor prewarm/clear.
/// Existing table-generation ownership is serialized; the atomic is not a lock.
#[repr(C)]
pub struct BitmapSource {
    pub generation: *const AtomicU32,
    pub captured_generation: u32,
    pub font: *const FontData,
    pub glyph_count: u32,
    pub bitmap_len: u32,
    pub slots: *const PageSlot,
    pub slot_count: u32,
    /// Address of overflow_[0].glyph; entries need not be contiguous Glyphs.
    pub overflow_first: *const Glyph,
    pub overflow_stride: u32,
    pub overflow_count: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bitmap {
    pub data: *const u8,
    pub len: u32,
}
impl BitmapSource {
    /// # Safety
    /// The generation pointer is live and aligned. If the generation matches,
    /// all descriptors, font tables, slots and byte ranges must be live and
    /// immutable for the loan. Each slot's offsets and glyph lengths must fit its
    /// buffer (the native PageSlot has no length). Slot entries are sorted.
    /// Every mutation invalidates generation before reuse; no callback, mutation,
    /// or concurrent access is permitted until the returned bytes are consumed.
    /// A stale view is rejected before font/glyph pointers are dereferenced.
    pub unsafe fn resolve(&self, glyph: *const Glyph) -> Option<Bitmap> {
        if self.generation.is_null()
            || unsafe { (*self.generation).load(Ordering::Relaxed) } != self.captured_generation
        {
            return None;
        }
        if self.font.is_null() || glyph.is_null() {
            return None;
        }
        let font = unsafe { &*self.font };
        if font.vector_bitmap_handler.is_some() {
            return None;
        }
        if !self.overflow_first.is_null() && self.overflow_stride != 0 {
            if let Some(offset) = (glyph as usize).checked_sub(self.overflow_first as usize) {
                if offset % self.overflow_stride as usize == 0
                    && offset / (self.overflow_stride as usize) < self.overflow_count as usize
                {
                    return None;
                }
            }
        }
        let offset = (glyph as usize).checked_sub(font.glyph as usize)?;
        if font.glyph.is_null() || offset % core::mem::size_of::<Glyph>() != 0 {
            return None;
        }
        let index = offset / core::mem::size_of::<Glyph>();
        if index >= self.glyph_count as usize {
            return None;
        }
        let g = unsafe { *glyph };
        let len = (u32::from(g.width) * u32::from(g.height) * (if font.is2_bit { 2 } else { 1 }))
            .div_ceil(8);
        if font.groups.is_null() || font.group_count == 0 {
            let end = g.data_offset.checked_add(len)?;
            if end > self.bitmap_len || font.bitmap.is_null() {
                return None;
            }
            return Some(Bitmap {
                data: unsafe { font.bitmap.add(g.data_offset as usize) },
                len,
            });
        }
        if self.slots.is_null() {
            return None;
        }
        let slots = unsafe { core::slice::from_raw_parts(self.slots, self.slot_count as usize) };
        for slot in slots {
            if slot.font != self.font || slot.count == 0 {
                continue;
            }
            if slot.entries.is_null() {
                return None;
            }
            let entries =
                unsafe { core::slice::from_raw_parts(slot.entries, usize::from(slot.count)) };
            let i = entries.partition_point(|e| e.glyph_index < index as u32);
            let e = entries.get(i)?;
            if e.glyph_index != index as u32 || e.buffer_offset == u32::MAX || slot.buffer.is_null()
            {
                return None;
            }
            return Some(Bitmap {
                data: unsafe { slot.buffer.add(e.buffer_offset as usize) },
                len,
            });
        }
        None
    }
}
impl Bitmap {
    pub const MISS: Self = Self {
        data: ptr::null(),
        len: 0,
    };
}
