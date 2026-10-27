//! SD font cache state. The facade lends stable slots; this module owns their buffers.
use super::Header;
use crate::font::raw::{Advance, BmpInterval, FontData, Glyph, Interval, KernClass, Ligature};
use core::{ffi::c_void, mem, ptr, slice};

const PAGE_LIMIT: usize = 512;
const ADVANCE_LIMIT: usize = 768;
const RETAIN_HEAP: u32 = 40 * 1024;
const ARENA_TOO_LARGE: i32 = -2;

// These records mirror SdCardFont's stable facade slots, checked by the pool ABI test.
#[repr(C)]
pub struct Resident {
    pub header: Header,
    pub offsets: [u32; 7],
    pub full_intervals: *mut Interval,
    pub bmp_intervals: *mut BmpInterval,
    pub intervals_are_bmp16: bool,
    pub ligature_pairs: *mut Ligature,
    pub ligatures_loaded: bool,
    pub kern_block_index: *mut u16,
    pub kern_block_index_ready: bool,
    pub stub: FontData,
    pub mini: FontData,
    pub mini_intervals: *mut Interval,
    pub mini_glyphs: *mut Glyph,
    pub mini_bitmap: *mut u8,
    pub interval_count: u32,
    pub glyph_count: u32,
    pub interval_capacity: u32,
    pub glyph_capacity: u32,
    pub bitmap_capacity: u32,
    pub bitmap_used: u32,
    pub measured_bytes_per_glyph: u32,
    pub underuse_runs: u8,
    pub metadata_only: bool,
    pub hysteresis_pending: bool,
    pub kern_left: *mut KernClass,
    pub kern_right: *mut KernClass,
    pub kern_left_entries: u16,
    pub kern_right_entries: u16,
    pub kern_left_count: u8,
    pub kern_right_count: u8,
    pub kern_matrix: *mut i8,
    pub kern_left_capacity: u16,
    pub kern_right_capacity: u16,
    pub kern_matrix_capacity: u32,
    pub kern_built: bool,
    pub font: *const FontData,
}
#[repr(C)]
pub struct Overflow {
    pub glyph: Glyph,
    pub bitmap: *mut u8,
    pub codepoint: u32,
    pub style: u8,
}
#[repr(C)]
#[derive(Default)]
pub struct Stats {
    pub total_ms: u32,
    pub read_ms: u32,
    pub seeks: u32,
    pub glyphs: u32,
    pub bitmap_bytes: u32,
}
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub open: unsafe extern "C" fn(*mut c_void) -> bool,
    pub close: unsafe extern "C" fn(*mut c_void, bool),
    pub read: unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize, bool) -> bool,
    pub alloc: unsafe extern "C" fn(usize, bool) -> *mut u8,
    pub free: unsafe extern "C" fn(*mut u8, bool),
    pub heap: unsafe extern "C" fn(bool) -> u32,
    pub clock: unsafe extern "C" fn() -> u32,
    pub corrupt: unsafe extern "C" fn(*mut c_void, *const Glyph, u32),
}
#[repr(C)]
pub struct Cache {
    pub styles: *mut Resident,
    pub slots: *const u8,
    pub advances: *mut *mut Advance,
    pub advance_sizes: *mut u32,
    pub advance_capacities: *mut u32,
    pub overflow: *mut Overflow,
    pub overflow_count: *mut u32,
    pub overflow_next: *mut u32,
    pub stats: *mut Stats,
    pub file_size: u32,
    pub loaded: bool,
    pub services: *const Services,
}

// Bounded, allocation-free sorting avoids the general sort's firmware footprint.
fn sort_by_key<T>(values: &mut [T], key: impl Fn(&T) -> u32) {
    if values.len() <= 16 {
        for i in 1..values.len() {
            let mut j = i;
            while j > 0 && key(&values[j]) < key(&values[j - 1]) {
                values.swap(j, j - 1);
                j -= 1;
            }
        }
        return;
    }
    fn sift<T>(values: &mut [T], mut root: usize, key: &impl Fn(&T) -> u32) {
        while root < values.len() / 2 {
            let mut child = root * 2 + 1;
            if child + 1 < values.len() && key(&values[child]) < key(&values[child + 1]) {
                child += 1;
            }
            if key(&values[root]) >= key(&values[child]) {
                break;
            }
            values.swap(root, child);
            root = child;
        }
    }
    for root in (0..values.len() / 2).rev() {
        sift(values, root, &key);
    }
    for end in (1..values.len()).rev() {
        values.swap(0, end);
        sift(&mut values[..end], 0, &key);
    }
}

struct Buffer<'a, T> {
    data: *mut T,
    count: usize,
    services: &'a Services,
}
impl<'a, T> Buffer<'a, T> {
    fn new(count: usize, services: &'a Services) -> Self {
        let data = count
            .checked_mul(mem::size_of::<T>())
            .map_or(ptr::null_mut(), |n| unsafe {
                (services.alloc)(n, false).cast()
            });
        Self {
            data,
            count,
            services,
        }
    }
    fn ok(&self) -> bool {
        !self.data.is_null()
    }
    unsafe fn values(&mut self) -> &mut [T] {
        unsafe { slice::from_raw_parts_mut(self.data, self.count) }
    }
}
impl<T> Drop for Buffer<'_, T> {
    fn drop(&mut self) {
        unsafe { (self.services.free)(self.data.cast(), false) }
    }
}
struct File<'a>(&'a Services);
impl<'a> File<'a> {
    fn open(s: &'a Services) -> Option<Self> {
        if unsafe { (s.open)(s.context) } {
            Some(Self(s))
        } else {
            None
        }
    }
    fn finish(&self) {
        unsafe { (self.0.close)(self.0.context, false) }
    }
    fn read<T>(&self, offset: u32, into: *mut T, count: usize) -> bool {
        self.read_sequential(offset, into, count, true)
    }
    fn read_sequential<T>(&self, offset: u32, into: *mut T, count: usize, seek: bool) -> bool {
        unsafe {
            (self.0.read)(
                self.0.context,
                offset,
                into.cast(),
                count * mem::size_of::<T>(),
                seek,
            )
        }
    }
}
impl Drop for File<'_> {
    fn drop(&mut self) {
        unsafe { (self.0.close)(self.0.context, true) }
    }
}
unsafe fn zero<T>() -> T {
    unsafe { mem::zeroed() }
}
unsafe fn table<'a, T>(p: *const T, n: usize) -> &'a [T] {
    if p.is_null() || n == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(p, n) }
    }
}
unsafe fn ensure<T>(p: &mut *mut T, capacity: &mut u32, needed: u32, io: &Services) -> bool {
    if !p.is_null() && *capacity >= needed {
        return true;
    }
    unsafe { (io.free)((*p).cast(), true) };
    *p = unsafe { (io.alloc)(needed.max(1) as usize * mem::size_of::<T>(), true) }.cast();
    *capacity = if p.is_null() { 0 } else { needed };
    !p.is_null()
}
unsafe fn free<T>(p: &mut *mut T, io: &Services) {
    unsafe { (io.free)((*p).cast(), true) };
    *p = ptr::null_mut();
}
impl Resident {
    unsafe fn index(&self, cp: u32) -> Option<u32> {
        let mut lo = 0usize;
        let mut hi = self.header.interval_count as usize;
        while lo < hi {
            let mid = lo + (hi - lo) / 2;
            let (first, last, offset) = if self.intervals_are_bmp16 {
                let iv = unsafe { *self.bmp_intervals.add(mid) };
                (
                    u32::from(iv.first),
                    u32::from(iv.last),
                    u32::from(iv.offset),
                )
            } else {
                let iv = unsafe { *self.full_intervals.add(mid) };
                (iv.first, iv.last, iv.offset)
            };
            if cp < first {
                hi = mid;
            } else if cp > last {
                lo = mid + 1;
            } else {
                return Some(offset.wrapping_add(cp - first));
            }
        }
        None
    }
    unsafe fn free_kern(&mut self, io: &Services) {
        unsafe {
            free(&mut self.kern_left, io);
            free(&mut self.kern_right, io);
            free(&mut self.kern_matrix, io);
        }
        self.kern_left_entries = 0;
        self.kern_right_entries = 0;
        self.kern_left_count = 0;
        self.kern_right_count = 0;
        self.kern_left_capacity = 0;
        self.kern_right_capacity = 0;
        self.kern_matrix_capacity = 0;
        self.kern_built = false;
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn free_mini(&mut self, io: &Services) {
        unsafe {
            free(&mut self.mini_intervals, io);
            free(&mut self.mini_glyphs, io);
            free(&mut self.mini_bitmap, io);
        }
        self.interval_count = 0;
        self.glyph_count = 0;
        self.interval_capacity = 0;
        self.glyph_capacity = 0;
        self.bitmap_capacity = 0;
        self.bitmap_used = 0;
        self.underuse_runs = 0;
        unsafe {
            self.free_kern(io);
            self.mini = zero();
        }
        self.font = &self.stub;
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn free_ligatures(&mut self, io: &Services) {
        self.stub.ligature_pairs = ptr::null();
        self.stub.ligature_pair_count = 0;
        self.mini.ligature_pairs = ptr::null();
        self.mini.ligature_pair_count = 0;
        unsafe {
            free(&mut self.ligature_pairs, io);
        }
        self.ligatures_loaded = false;
    }
    unsafe fn load_ligatures(&mut self, io: &Services) -> bool {
        if self.ligatures_loaded {
            return true;
        }
        if self.header.ligature_pair_count == 0 {
            self.ligatures_loaded = true;
            return true;
        }
        let Some(file) = File::open(io) else {
            return false;
        };
        self.ligature_pairs =
            unsafe { (io.alloc)(usize::from(self.header.ligature_pair_count) * 8, true) }.cast();
        if self.ligature_pairs.is_null() {
            return false;
        }
        if !file.read(
            self.offsets[5],
            self.ligature_pairs,
            usize::from(self.header.ligature_pair_count),
        ) {
            unsafe {
                self.free_ligatures(io);
            }
            return false;
        }
        self.ligatures_loaded = true;
        self.stub.ligature_pairs = self.ligature_pairs;
        self.stub.ligature_pair_count = u32::from(self.header.ligature_pair_count);
        true
    }
    fn apply_kern(&mut self) {
        let d = &mut self.mini;
        d.kern_left_classes = self.kern_left;
        d.kern_right_classes = self.kern_right;
        d.kern_left_codepoints = ptr::null();
        d.kern_right_codepoints = ptr::null();
        d.kern_left_class_ids = ptr::null();
        d.kern_right_class_ids = ptr::null();
        d.kern_row_offsets = ptr::null();
        d.kern_sparse_cols = ptr::null();
        d.kern_sparse_values = ptr::null();
        d.kern_matrix = self.kern_matrix;
        d.kern_left_entry_count = self.kern_left_entries;
        d.kern_right_entry_count = self.kern_right_entries;
        d.kern_left_class_count = self.kern_left_count;
        d.kern_right_class_count = self.kern_right_count;
        d.ligature_pairs = self.ligature_pairs;
        d.ligature_pair_count = u32::from(self.header.ligature_pair_count);
    }
}
impl Cache {
    unsafe fn io(&self) -> &Services {
        unsafe { &*self.services }
    }
    unsafe fn has(&self, style: usize) -> bool {
        unsafe { *self.slots.add(style & 3) != 0 }
    }
    unsafe fn resident(&self, style: usize) -> *mut Resident {
        unsafe { self.styles.add(usize::from(*self.slots.add(style) - 1)) }
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn resolve(&self, style: usize) -> usize {
        const FALLBACK: [[usize; 4]; 4] = [[0, 1, 2, 3], [1, 0, 3, 2], [2, 0, 3, 1], [3, 1, 2, 0]];
        for &i in &FALLBACK[style & 3] {
            if unsafe { self.has(i) } {
                return i;
            }
        }
        0
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn mask(&self, requested: u8) -> u8 {
        let mut result = 0;
        for i in 0..4 {
            if requested & (1 << i) != 0 {
                result |= 1 << unsafe { self.resolve(i) };
            }
        }
        result
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn advance(&self, style: usize, cp: u32) -> Option<u16> {
        let values = unsafe {
            table(
                *self.advances.add(style),
                *self.advance_sizes.add(style) as usize,
            )
        };
        let i = values.partition_point(|v| v.codepoint < cp);
        values
            .get(i)
            .filter(|v| v.codepoint == cp)
            .map(|v| v.advance_x)
    }
    unsafe fn grow(&self, style: usize, needed: u32) -> bool {
        let capacity = unsafe { &mut *self.advance_capacities.add(style) };
        if needed <= *capacity {
            return true;
        }
        let io = unsafe { self.io() };
        let mut cap = needed.max(*capacity * 2).max(64).min(ADVANCE_LIMIT as u32);
        let mut grown = unsafe { (io.alloc)(cap as usize * 8, true) }.cast::<Advance>();
        if grown.is_null() && cap > needed {
            cap = needed;
            grown = unsafe { (io.alloc)(cap as usize * 8, true) }.cast();
        }
        if grown.is_null() {
            return false;
        }
        let p = unsafe { &mut *self.advances.add(style) };
        let count = unsafe { *self.advance_sizes.add(style) } as usize;
        if count > 0 {
            unsafe { ptr::copy_nonoverlapping(*p, grown, count) };
        }
        unsafe { (io.free)((*p).cast(), true) };
        *p = grown;
        *capacity = cap;
        true
    }
    unsafe fn merge(&self, style: usize, new: &[Advance]) {
        let old = unsafe { *self.advance_sizes.add(style) } as usize;
        if new.is_empty() || old >= ADVANCE_LIMIT {
            return;
        }
        let cap = (old + new.len()).min(ADVANCE_LIMIT);
        if !unsafe { self.grow(style, cap as u32) } {
            return;
        }
        let p = unsafe { *self.advances.add(style) };
        let (mut i, mut j, mut k) = (old, new.len(), old + new.len());
        let mut drop = k - cap;
        while j > 0 {
            let v = if i == 0 || new[j - 1].codepoint >= unsafe { (*p.add(i - 1)).codepoint } {
                j -= 1;
                new[j]
            } else {
                i -= 1;
                unsafe { *p.add(i) }
            };
            k -= 1;
            if drop > 0 {
                drop -= 1;
            } else {
                unsafe { p.add(k).write(v) };
            }
        }
        unsafe { *self.advance_sizes.add(style) = cap as u32 };
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn clear_advances(&mut self) {
        for i in 0..4 {
            unsafe {
                free(&mut *self.advances.add(i), self.io());
                *self.advance_sizes.add(i) = 0;
                *self.advance_capacities.add(i) = 0;
            }
        }
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn clear_overflow(&mut self) {
        for i in 0..unsafe { *self.overflow_count } as usize {
            let e = unsafe { &mut *self.overflow.add(i) };
            unsafe { free(&mut e.bitmap, self.io()) };
            e.codepoint = 0;
        }
        unsafe {
            *self.overflow_count = 0;
            *self.overflow_next = 0;
        }
    }
    unsafe fn valid(&self, s: &Resident, g: &Glyph, cp: u32) -> bool {
        let pixels = u32::from(g.width) * u32::from(g.height);
        let packed = pixels.div_ceil(if s.header.is2_bit { 4 } else { 8 });
        if u32::from(g.data_length) == packed
            && u64::from(s.offsets[6]) + u64::from(g.data_offset) + u64::from(g.data_length)
                <= u64::from(self.file_size)
        {
            return true;
        }
        let io = unsafe { &*self.services };
        unsafe { (io.corrupt)(io.context, g, cp) };
        false
    }
    /// # Safety
    /// The facade lends valid, exclusively owned cache slots and paired services.
    /// All referenced arrays must remain live for the complete operation.
    pub unsafe fn coverage(&self, style: usize, cp: u32) -> bool {
        if !unsafe { self.has(style) } {
            return false;
        }
        let s = unsafe { &*self.resident(style) };
        if s.full_intervals.is_null() && s.bmp_intervals.is_null() {
            return false;
        }
        unsafe { s.index(cp).is_some() }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Mapping {
    cp: u32,
    index: u32,
}
struct Lowest<'a> {
    values: &'a mut [u32],
    limit: usize,
    count: usize,
    largest: usize,
}
impl Lowest<'_> {
    fn add(&mut self, cp: u32, reserved: bool) {
        if !reserved
            && self.count == self.limit
            && (self.limit == 0 || cp >= self.values[self.largest])
        {
            return;
        }
        if self.values[..self.count].contains(&cp) {
            return;
        }
        if reserved || self.count < self.limit {
            self.values[self.count] = cp;
            self.count += 1;
            if reserved || self.count < self.limit {
                return;
            }
        } else {
            self.values[self.largest] = cp;
        }
        self.largest = self.values[..self.limit]
            .iter()
            .enumerate()
            .max_by_key(|(_, cp)| *cp)
            .map_or(0, |(i, _)| i);
    }
}
unsafe fn ctext<'a>(p: *const u8) -> &'a [u8] {
    if p.is_null() {
        return &[];
    }
    unsafe { core::ffi::CStr::from_ptr(p.cast()).to_bytes() }
}
#[repr(C)]
pub struct Texts {
    pub segments: *const *const u8,
    pub lengths: *const usize,
    pub count: usize,
    pub extra: *const u8,
    pub space: bool,
    pub hyphen: bool,
    pub small_caps: bool,
}
impl Texts {
    unsafe fn collect(&self, output: &mut Lowest<'_>, mut wanted: impl FnMut(u32) -> bool) {
        let mut scan = |text: &[u8]| {
            let mut at = 0;
            while at < text.len() {
                if text[at] == 0 {
                    at += 1;
                    continue;
                }
                let Some(cp) = crate::utf8::next(text, &mut at) else {
                    break;
                };
                if wanted(cp) {
                    output.add(cp, false);
                }
                if self.small_caps {
                    let upper = crate::text::small_caps_upper(cp);
                    if upper != cp && wanted(upper) {
                        output.add(upper, false);
                    }
                }
            }
        };
        for i in 0..self.count {
            let p = unsafe { *self.segments.add(i) };
            let len = unsafe { *self.lengths.add(i) };
            scan(unsafe { table(p, len) });
        }
        scan(unsafe { ctext(self.extra) });
        if self.space && wanted(32) {
            output.add(32, true);
        }
        if self.hyphen && wanted(45) {
            output.add(45, true);
        }
    }
}
impl Cache {
    unsafe fn fetch_advances(&mut self, cps: &[u32], style: usize) -> i32 {
        if unsafe { *self.advance_sizes.add(style) } >= ADVANCE_LIMIT as u32
            || cps
                .iter()
                .all(|&cp| unsafe { self.advance(style, cp).is_some() })
        {
            return 0;
        }
        let io = unsafe { &*self.services };
        let s = unsafe { &*self.resident(style) };
        let mappings = Buffer::<Mapping>::new(cps.len(), io);
        if !mappings.ok() {
            return cps.len() as i32;
        }
        let mut need = 0;
        let mut missed = 0;
        let replacement = unsafe { s.index(0xfffd) };
        for &cp in cps {
            if unsafe { self.advance(style, cp).is_some() } {
                continue;
            }
            let Some(index) = unsafe { s.index(cp) }.or(replacement) else {
                missed += 1;
                continue;
            };
            unsafe { mappings.data.add(need).write(Mapping { cp, index }) };
            need += 1;
        }
        if need == 0 {
            return missed;
        }
        sort_by_key(
            unsafe { slice::from_raw_parts_mut(mappings.data, need) },
            |m| m.index,
        );
        let Some(file) = File::open(io) else {
            return missed;
        };
        let staged = Buffer::<Advance>::new(need, io);
        if !staged.ok() {
            return missed;
        }
        let mut fetched = 0;
        let mut last = i32::MIN;
        for i in 0..need {
            let m = unsafe { *mappings.data.add(i) };
            let mut glyph: Glyph = unsafe { zero() };
            if !file.read_sequential(
                s.offsets[1].wrapping_add(m.index.wrapping_mul(16)),
                &mut glyph,
                1,
                m.index as i32 != last.wrapping_add(1),
            ) {
                break;
            }
            last = m.index as i32;
            let advance_x = if unsafe { self.valid(s, &glyph, m.cp) } {
                glyph.advance_x
            } else {
                0
            };
            unsafe {
                staged.data.add(fetched).write(Advance {
                    codepoint: m.cp,
                    advance_x,
                })
            };
            fetched += 1;
        }
        file.finish();
        let staged = unsafe { slice::from_raw_parts_mut(staged.data, fetched) };
        sort_by_key(staged, |a| a.codepoint);
        unsafe { self.merge(style, staged) };
        missed
    }
    /// # Safety
    /// Cache slots and services are exclusively lent for this call. Input segments
    /// are readable NUL-separated words; callbacks may not reenter cache methods.
    pub unsafe fn build_advances(&mut self, texts: &Texts, mask: u8) -> i32 {
        if !self.loaded {
            return -1;
        }
        let mask = unsafe { self.mask(mask) };
        if mask == 0 {
            return 0;
        }
        let io = unsafe { &*self.services };
        let start = unsafe { (io.clock)() };
        for style in 0..4 {
            if mask & (1 << style) == 0 || !unsafe { self.has(style) } {
                continue;
            }
            let size = unsafe { *self.advance_sizes.add(style) };
            if size < ADVANCE_LIMIT as u32
                && unsafe { *self.advance_capacities.add(style) } - size < 64
            {
                unsafe { self.grow(style, (size + 64).min(ADVANCE_LIMIT as u32)) };
            }
        }
        let mut open = 0;
        for style in 0..4 {
            if mask & (1 << style) != 0
                && unsafe { self.has(style) }
                && unsafe { *self.advance_sizes.add(style) } < ADVANCE_LIMIT as u32
            {
                open |= 1 << style;
            }
        }
        if open == 0 {
            unsafe { (*self.stats).total_ms = (io.clock)().wrapping_sub(start) };
            return 0;
        }
        let mut limit = unsafe { ctext(texts.extra) }.len();
        for i in 0..texts.count {
            limit = limit.saturating_add(unsafe { *texts.lengths.add(i) });
        }
        if texts.small_caps {
            limit = limit.saturating_mul(2);
        }
        limit = limit.min(ADVANCE_LIMIT);
        let mut inline = [0u32; 34];
        let mut heap = if limit > 32 {
            Some(Buffer::<u32>::new(limit + 2, io))
        } else {
            None
        };
        let values = if let Some(b) = heap.as_mut() {
            if !b.ok() {
                return -1;
            }
            unsafe {
                ptr::write_bytes(b.data, 0, b.count);
                b.values()
            }
        } else {
            &mut inline
        };
        let mut missed = 0;
        for style in 0..4 {
            if open & (1 << style) == 0 {
                continue;
            }
            let s = unsafe { &*self.resident(style) };
            let replacement = unsafe { s.index(0xfffd).is_some() };
            let resolves = |cp| replacement || unsafe { s.index(cp).is_some() };
            let mut lacking = false;
            let mut collected = Lowest {
                values,
                limit,
                count: 0,
                largest: 0,
            };
            unsafe {
                texts.collect(&mut collected, |cp| {
                    if self.advance(style, cp).is_some() {
                        false
                    } else if resolves(cp) {
                        true
                    } else {
                        lacking = true;
                        false
                    }
                })
            };
            let cps = &mut collected.values[..collected.count];
            sort_by_key(cps, |cp| *cp);
            missed += unsafe { self.fetch_advances(cps, style) };
            if lacking {
                let mut collected = Lowest {
                    values,
                    limit,
                    count: 0,
                    largest: 0,
                };
                unsafe { texts.collect(&mut collected, |cp| !resolves(cp)) };
                missed += collected.count as i32;
            }
        }
        unsafe { (*self.stats).total_ms = (io.clock)().wrapping_sub(start) };
        missed
    }
    /// # Safety
    /// Same exclusive cache loan as build_advances; returned glyph is invalidated
    /// when its overflow slot is evicted or the cache is cleared.
    pub unsafe fn miss(&mut self, style: usize, cp: u32) -> *const Glyph {
        if !self.loaded || style >= 4 || !unsafe { self.has(style) } {
            return ptr::null();
        }
        let s = unsafe { &*self.resident(style) };
        if s.full_intervals.is_null() && s.bmp_intervals.is_null() {
            return ptr::null();
        }
        for i in 0..unsafe { *self.overflow_count } as usize {
            let e = unsafe { &*self.overflow.add(i) };
            if e.codepoint == cp && usize::from(e.style) == style {
                return &e.glyph;
            }
        }
        let Some(index) = (unsafe { s.index(cp) }) else {
            return ptr::null();
        };
        let io = unsafe { &*self.services };
        let Some(file) = File::open(io) else {
            return ptr::null();
        };
        let mut glyph: Glyph = unsafe { zero() };
        if !file.read(
            s.offsets[1].wrapping_add(index.wrapping_mul(16)),
            &mut glyph,
            1,
        ) {
            return ptr::null();
        }
        if !unsafe { self.valid(s, &glyph, cp) } {
            glyph = unsafe { zero() };
        }
        let mut bitmap = ptr::null_mut();
        if glyph.data_length > 0 {
            bitmap = unsafe { (io.alloc)(usize::from(glyph.data_length), true) };
            if bitmap.is_null() {
                return ptr::null();
            }
            if !file.read(
                s.offsets[6].wrapping_add(glyph.data_offset),
                bitmap,
                usize::from(glyph.data_length),
            ) {
                unsafe { (io.free)(bitmap, true) };
                return ptr::null();
            }
        }
        let slot = unsafe { *self.overflow_next } as usize;
        let e = unsafe { &mut *self.overflow.add(slot) };
        if unsafe { *self.overflow_count } == 8 {
            unsafe { (io.free)(e.bitmap, true) }
        } else {
            unsafe { *self.overflow_count += 1 };
        }
        unsafe { *self.overflow_next = ((slot + 1) % 8) as u32 };
        e.glyph = glyph;
        e.bitmap = bitmap;
        e.codepoint = cp;
        e.style = style as u8;
        &e.glyph
    }
}

fn blocks(count: u16) -> usize {
    usize::from(count).div_ceil(64)
}
#[allow(clippy::too_many_arguments)]
unsafe fn read_classes(
    file: &File<'_>,
    offset: u32,
    entries: u16,
    classes: u8,
    cps: &[u32],
    out: &mut [u8],
    scratch: *mut u8,
    index: *mut u16,
    ready: bool,
) -> bool {
    out.fill(0);
    let blocks = blocks(entries);
    let mut next = 0;
    for block in 0..blocks {
        if next == cps.len() && (index.is_null() || ready) {
            break;
        }
        if !index.is_null() && ready {
            while next < cps.len() && cps[next] < u32::from(unsafe { *index.add(block) }) {
                next += 1;
            }
            let end = if block + 1 < blocks {
                u32::from(unsafe { *index.add(block + 1) })
            } else {
                u32::from(unsafe { *index.add(blocks) }) + 1
            };
            if next == cps.len() || cps[next] > u32::from(unsafe { *index.add(blocks) }) {
                break;
            }
            if cps[next] >= end {
                continue;
            }
        }
        let count = (usize::from(entries) - block * 64).min(64);
        if !file.read(
            offset.wrapping_add((block * 192) as u32),
            scratch,
            count * 3,
        ) {
            return false;
        }
        let data = unsafe { slice::from_raw_parts(scratch, count * 3) };
        if !index.is_null() && !ready {
            unsafe { *index.add(block) = u16::from_le_bytes([data[0], data[1]]) };
            if block + 1 == blocks {
                unsafe {
                    *index.add(blocks) =
                        u16::from_le_bytes([data[(count - 1) * 3], data[(count - 1) * 3 + 1]])
                };
            }
        }
        for record in data.as_chunks::<3>().0 {
            if next == cps.len() {
                break;
            }
            let cp = u32::from(u16::from_le_bytes([record[0], record[1]]));
            let id = if record[2] <= classes { record[2] } else { 0 };
            while next < cps.len() && cps[next] < cp {
                next += 1;
            }
            let mut j = next;
            while j < cps.len() && cps[j] == cp {
                out[j] = id;
                j += 1;
            }
        }
    }
    true
}
impl Resident {
    unsafe fn build_kern(&mut self, cps: &[u32], io: &Services) -> bool {
        if self.header.kern_left_entry_count == 0
            || self.header.kern_right_entry_count == 0
            || cps.is_empty()
        {
            self.kern_left_entries = 0;
            self.kern_right_entries = 0;
            self.kern_left_count = 0;
            self.kern_right_count = 0;
            return true;
        }
        let Some(file) = File::open(io) else {
            unsafe { self.free_kern(io) };
            return false;
        };
        let scratch = Buffer::<u8>::new(
            cps.len() * 2 + 192usize.max(usize::from(self.header.kern_right_class_count)),
            io,
        );
        if !scratch.ok() {
            unsafe { self.free_kern(io) };
            return false;
        }
        unsafe { ptr::write_bytes(scratch.data, 0, scratch.count) };
        let left = unsafe { slice::from_raw_parts_mut(scratch.data, cps.len()) };
        let right = unsafe { slice::from_raw_parts_mut(scratch.data.add(cps.len()), cps.len()) };
        let block = unsafe { scratch.data.add(cps.len() * 2) };
        let right_index = if self.kern_block_index.is_null() {
            ptr::null_mut()
        } else {
            unsafe {
                self.kern_block_index
                    .add(blocks(self.header.kern_left_entry_count) + 1)
            }
        };
        if !unsafe {
            read_classes(
                &file,
                self.offsets[2],
                self.header.kern_left_entry_count,
                self.header.kern_left_class_count,
                cps,
                left,
                block,
                self.kern_block_index,
                self.kern_block_index_ready,
            )
        } || !unsafe {
            read_classes(
                &file,
                self.offsets[3],
                self.header.kern_right_entry_count,
                self.header.kern_right_class_count,
                cps,
                right,
                block,
                right_index,
                self.kern_block_index_ready,
            )
        } {
            unsafe { self.free_kern(io) };
            return false;
        }
        self.kern_block_index_ready = !self.kern_block_index.is_null();
        // Fixed class maps match the old stack working set; no per-glyph allocation.
        let mut lmap = [0u8; 256];
        let mut rmap = [0u8; 256];
        let mut lreverse = [0u8; 256];
        let mut rreverse = [0u8; 256];
        let mut lentries = 0u16;
        let mut rentries = 0u16;
        for (&l, &r) in left.iter().zip(right.iter()) {
            if l != 0 {
                lmap[usize::from(l)] = 1;
                lentries += 1;
            }
            if r != 0 {
                rmap[usize::from(r)] = 1;
                rentries += 1;
            }
        }
        let mut lc = 0usize;
        let mut rc = 0usize;
        for i in 1..256 {
            if lmap[i] != 0 {
                lc += 1;
                lmap[i] = lc as u8;
                lreverse[lc] = i as u8;
            }
            if rmap[i] != 0 {
                rc += 1;
                rmap[i] = rc as u8;
                rreverse[rc] = i as u8;
            }
        }
        if lc == 0 || rc == 0 {
            self.kern_left_entries = 0;
            self.kern_right_entries = 0;
            self.kern_left_count = 0;
            self.kern_right_count = 0;
            return true;
        }
        let mut lcap = u32::from(self.kern_left_capacity);
        let mut rcap = u32::from(self.kern_right_capacity);
        let lok = unsafe { ensure(&mut self.kern_left, &mut lcap, u32::from(lentries), io) };
        self.kern_left_capacity = lcap as u16;
        let rok =
            lok && unsafe { ensure(&mut self.kern_right, &mut rcap, u32::from(rentries), io) };
        self.kern_right_capacity = rcap as u16;
        if !rok
            || !unsafe {
                ensure(
                    &mut self.kern_matrix,
                    &mut self.kern_matrix_capacity,
                    (lc * rc) as u32,
                    io,
                )
            }
        {
            unsafe { self.free_kern(io) };
            return false;
        }
        let (mut li, mut ri) = (0, 0);
        for (i, &cp) in cps.iter().enumerate() {
            if left[i] != 0 {
                unsafe {
                    self.kern_left.add(li).write(KernClass {
                        codepoint: cp as u16,
                        class_id: lmap[usize::from(left[i])],
                    })
                };
                li += 1;
            }
            if right[i] != 0 {
                unsafe {
                    self.kern_right.add(ri).write(KernClass {
                        codepoint: cp as u16,
                        class_id: rmap[usize::from(right[i])],
                    })
                };
                ri += 1;
            }
        }
        for (l, &old_l) in lreverse.iter().enumerate().take(lc + 1).skip(1) {
            let offset = self.offsets[4].wrapping_add(
                (u32::from(old_l) - 1) * u32::from(self.header.kern_right_class_count),
            );
            if !file.read(
                offset,
                block,
                usize::from(self.header.kern_right_class_count),
            ) {
                unsafe { self.free_kern(io) };
                return false;
            }
            for (r, &old_r) in rreverse.iter().enumerate().take(rc + 1).skip(1) {
                unsafe {
                    *self.kern_matrix.add((l - 1) * rc + r - 1) =
                        *block.add(usize::from(old_r) - 1) as i8
                };
            }
        }
        self.kern_left_entries = li as u16;
        self.kern_right_entries = ri as u16;
        self.kern_left_count = lc as u8;
        self.kern_right_count = rc as u8;
        true
    }
}

pub type TextGetter = unsafe extern "C" fn(*const c_void, u32) -> *const u8;
impl Cache {
    /// # Safety
    /// Input callback returns terminated strings valid until its next invocation.
    /// All state is exclusively lent; services cannot reenter this cache.
    #[allow(clippy::too_many_arguments)]
    pub unsafe fn prewarm(
        &mut self,
        getter: TextGetter,
        context: *const c_void,
        text_count: u32,
        mask: u8,
        metadata: bool,
        kern: bool,
        accumulate: bool,
    ) -> i32 {
        if !self.loaded {
            return -1;
        }
        let mask = unsafe { self.mask(mask) };
        if mask == 0 {
            return 0;
        }
        let io = unsafe { &*self.services };
        let start = unsafe { (io.clock)() };
        let mut budget = PAGE_LIMIT;
        if !metadata && text_count > 1 {
            for style in 0..4 {
                if mask & (1 << style) == 0 || !unsafe { self.has(style) } {
                    continue;
                }
                let s = unsafe { &*self.resident(style) };
                let bpp = if s.header.is2_bit { 2 } else { 1 };
                let bitmap = if s.glyph_count > 0 && s.bitmap_used > 0 {
                    s.bitmap_used / s.glyph_count
                } else {
                    u32::from(s.header.advance_y) * u32::from(s.header.advance_y) * bpp / 8 + 4
                };
                budget = budget.min(
                    (unsafe { (io.heap)(false) }.saturating_sub(16 * 1024) / (bitmap + 16).max(1))
                        as usize,
                );
                break;
            }
        }
        if budget == 0 {
            return -1;
        }
        let buffer = Buffer::<u32>::new(PAGE_LIMIT, io);
        if !buffer.ok() {
            return -1;
        }
        let p = buffer.data;
        let mut count = 0;
        for i in 0..text_count {
            if count >= budget {
                break;
            }
            let text = unsafe { ctext(getter(context, i)) };
            let mut at = 0;
            while count < budget {
                let Some(cp) = crate::utf8::next(text, &mut at) else {
                    break;
                };
                if !unsafe { table(p, count) }.contains(&cp) {
                    unsafe { p.add(count).write(cp) };
                    count += 1;
                }
            }
        }
        if count < PAGE_LIMIT && !unsafe { table(p, count) }.contains(&0xfffd) {
            unsafe { p.add(count).write(0xfffd) };
            count += 1;
        }
        if !metadata && kern {
            for style in 0..4 {
                if mask & (1 << style) == 0 || !unsafe { self.has(style) } {
                    continue;
                }
                let s = unsafe { &mut *self.resident(style) };
                unsafe { s.load_ligatures(io) };
                for pair in
                    unsafe { table(s.ligature_pairs, usize::from(s.header.ligature_pair_count)) }
                {
                    if count >= PAGE_LIMIT {
                        break;
                    }
                    let cp = pair.ligature_cp;
                    let values = unsafe { table(p, count) };
                    if values.contains(&(pair.pair >> 16))
                        && values.contains(&(pair.pair & 65535))
                        && !values.contains(&cp)
                    {
                        unsafe { p.add(count).write(cp) };
                        count += 1;
                    }
                }
            }
        }
        let cps = unsafe { slice::from_raw_parts_mut(p, count) };
        sort_by_key(cps, |cp| *cp);
        let mut missed = 0;
        for style in 0..4 {
            if mask & (1 << style) == 0 || !unsafe { self.has(style) } {
                continue;
            }
            let mut result = unsafe { self.prewarm_style(style, cps, metadata, kern, accumulate) };
            if result == ARENA_TOO_LARGE {
                let per = unsafe { (*self.resident(style)).measured_bytes_per_glyph }.max(1);
                let arena = unsafe { (io.heap)(true) }.saturating_sub(4096);
                let mut fit = ((arena / per) as usize).min(count);
                while fit > 0 {
                    result = unsafe {
                        self.prewarm_style(style, &cps[..fit], metadata, kern, accumulate)
                    };
                    if result != ARENA_TOO_LARGE {
                        break;
                    }
                    fit /= 2;
                }
                result = if result == ARENA_TOO_LARGE {
                    count as i32
                } else {
                    result + (count - fit) as i32
                };
            }
            missed += result;
        }
        unsafe { (*self.stats).total_ms = (io.clock)().wrapping_sub(start) };
        missed
    }
    unsafe fn prewarm_style(
        &mut self,
        style: usize,
        request: &[u32],
        mut metadata: bool,
        kern: bool,
        accumulate: bool,
    ) -> i32 {
        let s = unsafe { &mut *self.resident(style) };
        let io = unsafe { &*self.services };
        if s.glyph_count > 0 && (!s.metadata_only || metadata) {
            let mut covered = true;
            let mut missed = 0;
            for &cp in request {
                let in_mini = unsafe { table(s.mini_intervals, s.interval_count as usize) }
                    .iter()
                    .any(|iv| iv.first <= cp && cp <= iv.last);
                if in_mini {
                    continue;
                }
                if unsafe { s.index(cp).is_none() } {
                    missed += 1;
                } else {
                    covered = false;
                    break;
                }
            }
            if covered {
                if !metadata && kern && !s.kern_built {
                    let resident = Buffer::<u32>::new(s.glyph_count as usize, io);
                    let mut n = 0;
                    if resident.ok() {
                        for iv in unsafe { table(s.mini_intervals, s.interval_count as usize) } {
                            let mut cp = iv.first;
                            while cp <= iv.last && n < s.glyph_count as usize {
                                unsafe { resident.data.add(n).write(cp) };
                                n += 1;
                                if cp == u32::MAX {
                                    break;
                                }
                                cp += 1;
                            }
                        }
                    }
                    let cps = if resident.ok() {
                        unsafe { table(resident.data, n) }
                    } else {
                        request
                    };
                    if unsafe { s.load_ligatures(io) } {
                        s.kern_built = unsafe { s.build_kern(cps, io) } && resident.ok();
                        s.apply_kern();
                    }
                }
                return missed;
            }
        }
        if s.hysteresis_pending && s.bitmap_capacity > 0 && s.bitmap_used > 0 {
            s.hysteresis_pending = false;
            if s.bitmap_used < s.bitmap_capacity - s.bitmap_capacity / 4 {
                s.underuse_runs += 1;
                if s.underuse_runs >= 3 {
                    unsafe { s.free_mini(io) }
                }
            } else {
                s.underuse_runs = 0;
            }
        }
        if accumulate
            && s.glyph_count > 0
            && s.interval_count > 0
            && unsafe { (io.heap)(false) } < RETAIN_HEAP
        {
            let average = if s.bitmap_used > 0 {
                s.bitmap_used / s.glyph_count
            } else {
                64
            };
            let estimated = (s.glyph_count + request.len() as u32) * (16 + average);
            if estimated + 12 * 1024 > unsafe { (io.heap)(false) } {
                unsafe { s.free_mini(io) }
            }
        }
        let mut union = None;
        let mut cps = request;
        if accumulate && s.glyph_count > 0 && s.interval_count > 0 {
            let buffer = Buffer::<u32>::new(s.glyph_count as usize + request.len(), io);
            if buffer.ok() {
                let mut n = 0;
                let mut iv_index = 0;
                let mut ri = 0;
                let intervals = unsafe { table(s.mini_intervals, s.interval_count as usize) };
                let mut cp = intervals[0].first;
                let mut active = true;
                while (active || ri < request.len()) && n < buffer.count {
                    let next;
                    if active && (ri >= request.len() || cp <= request[ri]) {
                        next = cp;
                        if ri < request.len() && request[ri] == cp {
                            ri += 1;
                        }
                        if cp < intervals[iv_index].last {
                            cp += 1;
                        } else {
                            iv_index += 1;
                            if iv_index < intervals.len() {
                                cp = intervals[iv_index].first;
                            } else {
                                active = false;
                            }
                        }
                    } else {
                        next = request[ri];
                        ri += 1;
                    }
                    unsafe { buffer.data.add(n).write(next) };
                    n += 1;
                }
                if !active && ri >= request.len() && n <= PAGE_LIMIT {
                    metadata &= s.metadata_only;
                    cps = unsafe { table(buffer.data, n) };
                    union = Some(buffer);
                }
            }
        }
        let mut mappings = Some(Buffer::<Mapping>::new(cps.len(), io));
        let mapping = mappings.as_ref().map_or(ptr::null_mut(), |b| b.data);
        if mapping.is_null() {
            return cps.len() as i32;
        }
        let mut valid = 0;
        for &cp in cps {
            if let Some(index) = unsafe { s.index(cp) } {
                unsafe { mapping.add(valid).write(Mapping { cp, index }) };
                valid += 1;
            }
        }
        let missed = (cps.len() - valid) as i32;
        if valid == 0 {
            unsafe { s.free_mini(io) };
            return missed;
        }
        s.interval_count = 0;
        s.glyph_count = 0;
        s.kern_left_entries = 0;
        s.kern_right_entries = 0;
        s.kern_left_count = 0;
        s.kern_right_count = 0;
        s.mini = unsafe { zero() };
        s.font = &s.stub;
        if !unsafe {
            ensure(
                &mut s.mini_intervals,
                &mut s.interval_capacity,
                valid as u32,
                io,
            )
        } {
            return cps.len() as i32;
        }
        let mut range = 0;
        for i in 1..=valid {
            if i == valid
                || unsafe { (*mapping.add(i)).cp != (*mapping.add(i - 1)).cp.wrapping_add(1) }
            {
                unsafe {
                    s.mini_intervals
                        .add(s.interval_count as usize)
                        .write(Interval {
                            first: (*mapping.add(range)).cp,
                            last: (*mapping.add(i - 1)).cp,
                            offset: range as u32,
                        })
                };
                s.interval_count += 1;
                range = i;
            }
        }
        if !unsafe { ensure(&mut s.mini_glyphs, &mut s.glyph_capacity, valid as u32, io) } {
            unsafe { s.free_mini(io) };
            return cps.len() as i32;
        }
        s.glyph_count = valid as u32;
        let mut order = Some(Buffer::<u32>::new(valid, io));
        let p = order.as_ref().map_or(ptr::null_mut(), |b| b.data);
        if p.is_null() {
            unsafe { s.free_mini(io) };
            return cps.len() as i32;
        }
        for i in 0..valid {
            unsafe { p.add(i).write(i as u32) };
        }
        // Validated intervals map ascending codepoints to ascending glyph indices.
        let Some(file) = File::open(io) else {
            unsafe { s.free_mini(io) };
            return cps.len() as i32;
        };
        let start = unsafe { (io.clock)() };
        let mut seeks = 0u32;
        let mut last = i32::MIN;
        for i in 0..valid {
            let mi = unsafe { *p.add(i) } as usize;
            let m = unsafe { *mapping.add(mi) };
            let offset = s.offsets[1].wrapping_add(m.index.wrapping_mul(16));
            if m.index as i32 != last.wrapping_add(1) {
                seeks += 1;
            }
            if !file.read_sequential(
                offset,
                unsafe { s.mini_glyphs.add(mi) },
                1,
                m.index as i32 != last.wrapping_add(1),
            ) {
                unsafe { s.free_mini(io) };
                return cps.len() as i32;
            }
            let g = unsafe { &mut *s.mini_glyphs.add(mi) };
            if !unsafe { self.valid(s, g, m.cp) } {
                *g = unsafe { zero() };
            }
            last = m.index as i32;
        }
        mappings.take();
        let mut total = 0u32;
        if !metadata {
            for g in unsafe { table(s.mini_glyphs, valid) } {
                total += u32::from(g.data_length);
            }
            s.measured_bytes_per_glyph = total.div_ceil(valid as u32);
            if total > s.bitmap_capacity {
                order.take();
                unsafe { free(&mut s.mini_bitmap, io) };
                s.bitmap_capacity = 0;
                if unsafe { (io.heap)(true) } < total {
                    unsafe { s.free_mini(io) };
                    drop(file);
                    let mut ready =
                        unsafe { ensure(&mut s.mini_bitmap, &mut s.bitmap_capacity, total, io) };
                    if !ready && (0..4).any(|i| unsafe { *self.advance_sizes.add(i) > 0 }) {
                        unsafe { self.clear_advances() };
                        ready = unsafe {
                            ensure(&mut s.mini_bitmap, &mut s.bitmap_capacity, total, io)
                        };
                    }
                    if !ready {
                        return ARENA_TOO_LARGE;
                    }
                    unsafe {
                        (*self.stats).read_ms = (*self.stats)
                            .read_ms
                            .wrapping_add((io.clock)().wrapping_sub(start));
                        (*self.stats).seeks += seeks;
                    }
                    return unsafe { self.prewarm_style(style, cps, metadata, kern, accumulate) };
                }
            }
            if !unsafe { ensure(&mut s.mini_bitmap, &mut s.bitmap_capacity, total, io) } {
                unsafe { s.free_mini(io) };
                return ARENA_TOO_LARGE;
            }
            s.bitmap_used = total;
            if order.is_none() {
                let buffer = Buffer::<u32>::new(valid, io);
                if !buffer.ok() {
                    unsafe { s.free_mini(io) };
                    return cps.len() as i32;
                }
                for i in 0..valid {
                    unsafe { buffer.data.add(i).write(i as u32) };
                }
                order = Some(buffer);
            }
            let p = order.as_ref().map_or(ptr::null_mut(), |b| b.data);
            sort_by_key(
                unsafe { slice::from_raw_parts_mut(p, valid) },
                |&i| unsafe { (*s.mini_glyphs.add(i as usize)).data_offset },
            );
            let mut cursor = 0;
            let mut last_end = u32::MAX;
            for i in 0..valid {
                let g = unsafe { &mut *s.mini_glyphs.add(*p.add(i) as usize) };
                if g.data_length == 0 {
                    g.data_offset = cursor;
                    continue;
                }
                let offset = s.offsets[6].wrapping_add(g.data_offset);
                if offset != last_end {
                    seeks += 1;
                }
                if !file.read_sequential(
                    offset,
                    unsafe { s.mini_bitmap.add(cursor as usize) },
                    usize::from(g.data_length),
                    offset != last_end,
                ) {
                    unsafe { s.free_mini(io) };
                    return cps.len() as i32;
                }
                last_end = offset.wrapping_add(u32::from(g.data_length));
                g.data_offset = cursor;
                cursor += u32::from(g.data_length);
            }
        }
        let read_ms = unsafe { (io.clock)() }.wrapping_sub(start);
        order.take();
        let mut lig_ok = false;
        let mut kern_ok = false;
        if !metadata && kern {
            lig_ok = unsafe { s.load_ligatures(io) };
            if lig_ok {
                kern_ok = unsafe { s.build_kern(cps, io) };
            }
        }
        s.metadata_only = metadata;
        s.kern_built = kern_ok;
        s.hysteresis_pending = !metadata;
        s.mini = unsafe { zero() };
        s.mini.bitmap = s.mini_bitmap;
        s.mini.glyph = s.mini_glyphs;
        s.mini.intervals = s.mini_intervals;
        s.mini.interval_count = s.interval_count;
        s.mini.advance_y = s.header.advance_y;
        s.mini.ascender = i32::from(s.header.ascender);
        s.mini.descender = i32::from(s.header.descender);
        s.mini.is2_bit = s.header.is2_bit;
        if lig_ok {
            s.apply_kern();
        }
        s.mini.glyph_miss_handler = s.stub.glyph_miss_handler;
        s.mini.glyph_miss_ctx = s.stub.glyph_miss_ctx;
        s.mini.coverage_handler = s.stub.coverage_handler;
        s.font = &s.mini;
        unsafe {
            (*self.stats).read_ms = (*self.stats).read_ms.wrapping_add(read_ms);
            (*self.stats).seeks += seeks;
            (*self.stats).glyphs += valid as u32;
            (*self.stats).bitmap_bytes += total;
        }
        drop(file);
        drop(order);
        drop(mappings);
        drop(union);
        missed
    }
}

use core::mem::{offset_of, size_of};
pub const LAYOUT: [usize; 45] = [
    size_of::<Resident>(),
    offset_of!(Resident, header),
    offset_of!(Resident, offsets),
    offset_of!(Resident, full_intervals),
    offset_of!(Resident, bmp_intervals),
    offset_of!(Resident, intervals_are_bmp16),
    offset_of!(Resident, ligature_pairs),
    offset_of!(Resident, ligatures_loaded),
    offset_of!(Resident, kern_block_index),
    offset_of!(Resident, kern_block_index_ready),
    offset_of!(Resident, stub),
    offset_of!(Resident, mini),
    offset_of!(Resident, mini_intervals),
    offset_of!(Resident, mini_glyphs),
    offset_of!(Resident, mini_bitmap),
    offset_of!(Resident, interval_count),
    offset_of!(Resident, glyph_count),
    offset_of!(Resident, interval_capacity),
    offset_of!(Resident, glyph_capacity),
    offset_of!(Resident, bitmap_capacity),
    offset_of!(Resident, bitmap_used),
    offset_of!(Resident, measured_bytes_per_glyph),
    offset_of!(Resident, underuse_runs),
    offset_of!(Resident, metadata_only),
    offset_of!(Resident, hysteresis_pending),
    offset_of!(Resident, kern_left),
    offset_of!(Resident, kern_right),
    offset_of!(Resident, kern_left_entries),
    offset_of!(Resident, kern_right_entries),
    offset_of!(Resident, kern_left_count),
    offset_of!(Resident, kern_right_count),
    offset_of!(Resident, kern_matrix),
    offset_of!(Resident, kern_left_capacity),
    offset_of!(Resident, kern_right_capacity),
    offset_of!(Resident, kern_matrix_capacity),
    offset_of!(Resident, kern_built),
    offset_of!(Resident, font),
    size_of::<Overflow>(),
    offset_of!(Overflow, bitmap),
    offset_of!(Overflow, codepoint),
    offset_of!(Overflow, style),
    size_of::<Stats>(),
    size_of::<Cache>(),
    size_of::<Services>(),
    size_of::<Texts>(),
];
#[cfg(target_pointer_width = "32")]
const _: () = {
    let expected = [
        400, 0, 24, 52, 56, 60, 64, 68, 72, 76, 80, 200, 320, 324, 328, 332, 336, 340, 344, 348,
        352, 356, 360, 361, 362, 364, 368, 372, 374, 376, 377, 380, 384, 386, 388, 392, 396, 28,
        16, 20, 24, 20, 48, 36, 20,
    ];
    let mut i = 0;
    while i < LAYOUT.len() {
        assert!(LAYOUT[i] == expected[i]);
        i += 1;
    }
};
