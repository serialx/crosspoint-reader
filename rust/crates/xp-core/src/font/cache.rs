//! Built-in bitmap cache. Buffers retain the font allocator's placement and lifetime.
use super::{
    raw::{FontData, Glyph},
    Font,
};
use core::{ffi::c_void, mem::MaybeUninit, ptr};

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct Stats {
    pub hits: u32,
    pub misses: u32,
    pub decompress_ms: u32,
    pub groups: u16,
    pub page_bytes: u32,
    pub entry_bytes: u32,
    pub hot_bytes: u32,
    pub peak_temp: u32,
    pub bitmap_us: u32,
    pub calls: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Entry {
    pub index: u32,
    pub offset: u32,
    pub aligned: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Slot {
    pub buffer: *mut u8,
    pub font: *const FontData,
    pub entries: *mut Entry,
    pub count: u16,
}
impl Default for Slot {
    fn default() -> Self {
        Self {
            buffer: ptr::null_mut(),
            font: ptr::null(),
            entries: ptr::null_mut(),
            count: 0,
        }
    }
}
#[repr(C)]
pub struct Cache {
    pub stats: Stats,
    pub slots: [Slot; 8],
    pub count: u8,
    pub hot_font: *const FontData,
    pub hot_index: u16,
    pub hot: *mut u8,
    pub hot_capacity: u32,
    pub scratch: *mut u8,
    pub scratch_capacity: u32,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub alloc: unsafe extern "C" fn(usize) -> *mut u8,
    pub free: unsafe extern "C" fn(*mut u8),
    pub inflate: unsafe extern "C" fn(*mut c_void, *const u8, u32, *mut u8, u32) -> i32,
    pub clock: unsafe extern "C" fn(bool) -> u32,
    pub generation: unsafe extern "C" fn(),
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32)>,
}
impl Services {
    fn log(&self, event: u32, a: u32, b: u32, c: u32, d: u32) {
        if let Some(log) = self.diagnostic {
            unsafe { log(self.ctx, event, a, b, c, d) };
        }
    }
}
unsafe fn group_index(font: &FontData, index: u32) -> u16 {
    if !font.glyph_to_group.is_null() {
        return *font.glyph_to_group.add(index as usize);
    }
    for i in 0..font.group_count {
        let g = &*font.groups.add(i as usize);
        if index >= g.first_glyph_index && index < g.first_glyph_index + u32::from(g.glyph_count) {
            return i;
        }
    }
    font.group_count
}
fn aligned_size(g: &Glyph) -> u32 {
    u32::from(g.width).div_ceil(4) * u32::from(g.height)
}
unsafe fn aligned_offset(font: &FontData, group: u16, index: u32) -> u32 {
    let mut offset = 0;
    let start = if font.glyph_to_group.is_null() {
        (*font.groups.add(group as usize)).first_glyph_index
    } else {
        0
    };
    for i in start..index {
        if font.glyph_to_group.is_null() || *font.glyph_to_group.add(i as usize) == group {
            offset += aligned_size(&*font.glyph.add(i as usize));
        }
    }
    offset
}
unsafe fn compact(src: *const u8, dst: *mut u8, width: u8, height: u8) {
    if width == 0 || height == 0 {
        return;
    }
    let stride = usize::from(width).div_ceil(4);
    if width.is_multiple_of(4) {
        ptr::copy_nonoverlapping(src, dst, stride * usize::from(height));
        return;
    }
    let (mut byte, mut bits, mut written) = (0u8, 0u8, 0usize);
    for y in 0..usize::from(height) {
        for x in 0..usize::from(width) {
            byte = (byte << 2) | ((*src.add(y * stride + x / 4) >> ((3 - x % 4) * 2)) & 3);
            bits += 2;
            if bits == 8 {
                dst.add(written).write(byte);
                written += 1;
                byte = 0;
                bits = 0;
            }
        }
    }
    if bits != 0 {
        dst.add(written).write(byte << (8 - bits));
    }
}
unsafe fn ensure(buf: &mut *mut u8, cap: &mut u32, needed: u32, io: &Services) -> bool {
    if *cap >= needed {
        return true;
    }
    (io.free)(*buf);
    *buf = (io.alloc)(needed as usize);
    *cap = if buf.is_null() { 0 } else { needed };
    !buf.is_null()
}
unsafe fn decompress(
    stats: &mut Stats,
    font: &FontData,
    index: u16,
    out: *mut u8,
    io: &Services,
) -> bool {
    let g = &*font.groups.add(index as usize);
    let start = (io.clock)(false);
    let result = (io.inflate)(
        io.ctx,
        font.bitmap.add(g.compressed_offset as usize),
        g.compressed_size,
        out,
        g.uncompressed_size,
    );
    if result < 0 {
        return false;
    } // initialization failed before the timed read
    stats.decompress_ms = stats
        .decompress_ms
        .wrapping_add((io.clock)(false).wrapping_sub(start));
    if result == 0 {
        io.log(1, u32::from(index), 0, 0, 0);
    }
    result != 0
}
unsafe fn find_entry(slot: &Slot, index: u32) -> Option<usize> {
    let (mut left, mut right) = (0i32, i32::from(slot.count) - 1);
    while left <= right {
        let mid = left + (right - left) / 2;
        let found = (*slot.entries.add(mid as usize)).index;
        if found == index {
            return Some(mid as usize);
        }
        if found < index {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    None
}
impl Cache {
    /// # Safety
    /// All state buffers belong to this cache and were allocated by `io.alloc`.
    pub unsafe fn clear(&mut self, io: &Services) {
        (io.generation)();
        for i in 0..usize::from(self.count) {
            let slot = self.slots.get_unchecked_mut(i);
            (io.free)(slot.buffer);
            (io.free)(slot.entries.cast());
            *slot = Slot::default();
        }
        self.count = 0;
        (io.free)(self.hot);
        self.hot = ptr::null_mut();
        self.hot_capacity = 0;
        self.hot_font = ptr::null();
        self.hot_index = u16::MAX;
        (io.free)(self.scratch);
        self.scratch = ptr::null_mut();
        self.scratch_capacity = 0;
    }
    /// # Safety
    /// Font tables and glyph index are validated resident tables; returned bytes
    /// expire on the next cache mutation. Services must not reenter this cache.
    pub unsafe fn bitmap(
        &mut self,
        font: &FontData,
        glyph: &Glyph,
        index: u32,
        io: &Services,
    ) -> *const u8 {
        let start = (io.clock)(true);
        self.stats.calls = self.stats.calls.wrapping_add(1);
        let result = self.bitmap_inner(font, glyph, index, io);
        self.stats.bitmap_us = self
            .stats
            .bitmap_us
            .wrapping_add((io.clock)(true).wrapping_sub(start));
        result
    }
    unsafe fn bitmap_inner(
        &mut self,
        font: &FontData,
        glyph: &Glyph,
        index: u32,
        io: &Services,
    ) -> *const u8 {
        if font.groups.is_null() || font.group_count == 0 {
            return font.bitmap.add(glyph.data_offset as usize);
        }
        for i in 0..usize::from(self.count) {
            let slot = self.slots.get_unchecked(i);
            if !ptr::eq(slot.font, font) || slot.count == 0 {
                continue;
            }
            if let Some(e) = find_entry(slot, index) {
                let offset = (*slot.entries.add(e)).offset;
                if offset != u32::MAX {
                    self.stats.hits = self.stats.hits.wrapping_add(1);
                    return slot.buffer.add(offset as usize);
                }
            }
            break;
        }
        let group = group_index(font, index);
        if group >= font.group_count {
            io.log(2, index, 0, 0, 0);
            return ptr::null();
        }
        if self.hot.is_null() || !ptr::eq(self.hot_font, font) || self.hot_index != group {
            self.stats.misses = self.stats.misses.wrapping_add(1);
            let size = (*font.groups.add(group as usize)).uncompressed_size;
            self.hot_font = ptr::null();
            self.hot_index = u16::MAX;
            if !ensure(&mut self.hot, &mut self.hot_capacity, size, io) {
                io.log(3, size, u32::from(group), 0, 0);
                return ptr::null();
            }
            if !decompress(&mut self.stats, font, group, self.hot, io) {
                return ptr::null();
            }
            self.hot_font = font;
            self.hot_index = group;
            self.stats.hot_bytes = size;
        } else {
            self.stats.hits = self.stats.hits.wrapping_add(1);
        }
        if !ensure(
            &mut self.scratch,
            &mut self.scratch_capacity,
            u32::from(glyph.data_length),
            io,
        ) {
            io.log(4, u32::from(glyph.data_length), 0, 0, 0);
            return ptr::null();
        }
        compact(
            self.hot.add(aligned_offset(font, group, index) as usize),
            self.scratch,
            glyph.width,
            glyph.height,
        );
        self.scratch
    }
    /// # Safety
    /// Same table and service contract as bitmap. At most eight prewarmed slots;
    /// each allocation is released through the same services on clear.
    pub unsafe fn prewarm(&mut self, font: &FontData, text: &[u8], io: &Services) -> i32 {
        if font.groups.is_null() {
            return 0;
        }
        if self.count >= 8 {
            io.log(5, 8, 0, 0, 0);
            return -1;
        }
        (io.generation)();
        // Only the initialized prefixes are read; avoid clearing unused scratch entries.
        let mut needed_storage = [MaybeUninit::<u32>::uninit(); 512];
        let needed = needed_storage.as_mut_ptr().cast::<u32>();
        let mut count = 0usize;
        let mut warned = false;
        let mut pos = 0;
        let view = Font::new(font);
        while let Some(cp) = crate::utf8::next(text, &mut pos) {
            if let Some(index) = view.glyph_index(cp) {
                if !core::slice::from_raw_parts(needed, count).contains(&(index as u32)) {
                    if count < 512 {
                        needed.add(count).write(index as u32);
                        count += 1;
                    } else if !warned {
                        io.log(6, 512, 0, 0, 0);
                        warned = true;
                    }
                }
            }
        }
        if !font.ligature_pairs.is_null() {
            for i in 0..font.ligature_pair_count {
                if count >= 512 {
                    break;
                }
                let lig = &*font.ligature_pairs.add(i as usize);
                if let (Some(left), Some(right)) = (
                    view.glyph_index(lig.pair >> 16),
                    view.glyph_index(lig.pair & 0xffff),
                ) {
                    let values = core::slice::from_raw_parts(needed, count);
                    if !values.contains(&(left as u32)) || !values.contains(&(right as u32)) {
                        continue;
                    }
                    if let Some(out) = view.glyph_index(lig.ligature_cp) {
                        if !values.contains(&(out as u32)) {
                            needed.add(count).write(out as u32);
                            count += 1;
                        }
                    }
                }
            }
        }
        if count == 0 {
            return 0;
        }
        let mut group_storage = [MaybeUninit::<u16>::uninit(); 128];
        let groups = group_storage.as_mut_ptr().cast::<u16>();
        let (mut group_count, mut bytes, mut group_warned) = (0usize, 0u32, false);
        for &index in core::slice::from_raw_parts(needed, count) {
            bytes += u32::from((*font.glyph.add(index as usize)).data_length);
            let group = group_index(font, index);
            if !core::slice::from_raw_parts(groups, group_count).contains(&group) {
                if group_count < 128 {
                    groups.add(group_count).write(group);
                    group_count += 1;
                } else if !group_warned {
                    io.log(7, 0, 0, 0, 0);
                    group_warned = true;
                }
            }
        }
        self.stats.groups = group_count as u16;
        let slot = self.slots.get_unchecked_mut(usize::from(self.count));
        slot.buffer = (io.alloc)(bytes as usize);
        slot.entries = (io.alloc)(count * core::mem::size_of::<Entry>()).cast();
        if slot.buffer.is_null() || slot.entries.is_null() {
            io.log(8, bytes, count as u32, 0, 0);
            (io.free)(slot.buffer);
            (io.free)(slot.entries.cast());
            *slot = Slot::default();
            return count as i32;
        }
        self.stats.page_bytes += bytes;
        self.stats.entry_bytes += (count * core::mem::size_of::<Entry>()) as u32;
        slot.font = font;
        slot.count = count as u16;
        self.count += 1;
        for (i, &index) in core::slice::from_raw_parts(needed, count)
            .iter()
            .enumerate()
        {
            slot.entries.add(i).write(Entry {
                index,
                offset: u32::MAX,
                aligned: 0,
            });
        }
        // Keep entries sorted for binary lookup without another scratch allocation.
        for i in 1..count {
            let key = *slot.entries.add(i);
            let mut j = i;
            while j > 0 && (*slot.entries.add(j - 1)).index > key.index {
                slot.entries.add(j).write(*slot.entries.add(j - 1));
                j -= 1;
            }
            slot.entries.add(j).write(key);
        }
        let mut trackers = [0u32; 128];
        if !font.glyph_to_group.is_null() {
            let last = &*font.intervals.add(font.interval_count as usize - 1);
            let total = last.offset + last.last - last.first + 1;
            for i in 0..total {
                let group = *font.glyph_to_group.add(i as usize);
                let Some(gp) = core::slice::from_raw_parts(groups, group_count)
                    .iter()
                    .position(|&g| g == group)
                else {
                    continue;
                };
                let offset = trackers.get_unchecked_mut(gp);
                if let Some(e) = find_entry(slot, i) {
                    (*slot.entries.add(e)).aligned = *offset;
                }
                *offset += aligned_size(&*font.glyph.add(i as usize));
            }
        } else {
            for &group in core::slice::from_raw_parts(groups, group_count) {
                let g = &*font.groups.add(group as usize);
                let mut offset = 0;
                for j in 0..u32::from(g.glyph_count) {
                    let i = g.first_glyph_index + j;
                    if let Some(e) = find_entry(slot, i) {
                        (*slot.entries.add(e)).aligned = offset;
                    }
                    offset += aligned_size(&*font.glyph.add(i as usize));
                }
            }
        }
        let (mut written, mut missed) = (0u32, 0i32);
        for &group in core::slice::from_raw_parts(groups, group_count) {
            let g = &*font.groups.add(group as usize);
            let temp = (io.alloc)(g.uncompressed_size as usize);
            if temp.is_null() {
                io.log(9, g.uncompressed_size, u32::from(group), 0, 0);
                missed += 1;
                continue;
            }
            self.stats.peak_temp = self.stats.peak_temp.max(g.uncompressed_size);
            if !decompress(&mut self.stats, font, group, temp, io) {
                (io.free)(temp);
                missed += 1;
                continue;
            }
            for i in 0..count {
                let e = &mut *slot.entries.add(i);
                if e.offset != u32::MAX || group_index(font, e.index) != group {
                    continue;
                }
                let glyph = &*font.glyph.add(e.index as usize);
                compact(
                    temp.add(e.aligned as usize),
                    slot.buffer.add(written as usize),
                    glyph.width,
                    glyph.height,
                );
                e.offset = written;
                written += u32::from(glyph.data_length);
            }
            (io.free)(temp);
        }
        io.log(10, count as u32, written, group_count as u32, missed as u32);
        missed
    }
}
