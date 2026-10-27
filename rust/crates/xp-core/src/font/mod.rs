//! Allocation-free readers for resident C++ font tables.
//!
//! C++ owns preparation, storage and cache mutation. A view is borrowed for one
//! measurement operation; drop it before calling any loader or cache mutator.

mod layout;
pub mod raw;

use raw::{Advance, BmpInterval, FontData, Glyph, Interval, KernClass};

/// One resolved SD style. Kerning/ligatures are already carried by `data`.
/// Full coverage uses the compact table when `bmp_intervals` is non-null.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SdView {
    pub data: *const FontData,
    pub advances: *const Advance,
    pub advance_count: u32,
    pub full_intervals: *const Interval,
    pub bmp_intervals: *const BmpInterval,
    pub coverage_count: u32,
}

#[derive(Clone, Copy, Debug)]
pub enum GlyphResult {
    Missing,
    Resident(Glyph),
    Synthetic(Glyph),
    NotResident,
}

struct Coverage<'a> {
    full: &'a [Interval],
    bmp: &'a [BmpInterval],
}

/// A read-only loan of the current tables. Never invokes a C++ callback.
pub struct Font<'a> {
    data: &'a FontData,
    advances: &'a [Advance],
    coverage: Option<Coverage<'a>>,
}

// A null optional table is absent even when a failed load retained its count.
unsafe fn table<'a, T>(ptr: *const T, count: usize) -> &'a [T] {
    if ptr.is_null() || count == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(ptr, count) }
    }
}

fn lower_bound<T>(values: &[T], before: impl Fn(&T) -> bool) -> usize {
    let (mut base, mut count) = (0, values.len());
    while count != 0 {
        let half = count / 2;
        let middle = base + half;
        // The remaining interval [base, base+count) is within values.
        if before(unsafe { values.get_unchecked(middle) }) {
            base = middle + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    base
}
fn interval_index(intervals: &[Interval], cp: u32) -> Option<usize> {
    if cp < 0x80 {
        if let Some(iv) = intervals.first() {
            if iv.first <= cp && cp <= iv.last {
                return Some((iv.offset + cp - iv.first) as usize);
            }
        }
    }
    let end = lower_bound(intervals, |iv| iv.first <= cp);
    let iv = intervals.get(end.checked_sub(1)?)?;
    (cp <= iv.last).then(|| (iv.offset + cp - iv.first) as usize)
}

impl<'a> Font<'a> {
    /// # Safety
    /// All non-null table pointers and their transitive index ranges must be
    /// valid, aligned for their declared type, and immutable for `'a`. Tables
    /// must have the sorted, non-overlapping layout produced by the font loader
    /// or generator. Every interval must index a live glyph array. Class IDs,
    /// CSR ranges and split arrays must index live parallel tables; required
    /// pointers cannot be null. All ranges must fit isize::MAX bytes.
    /// The font and every borrowed allocation must outlive this view. No cache
    /// mutation or callback that might mutate these tables may run during the loan.
    pub unsafe fn new(data: &'a FontData) -> Self {
        Self {
            data,
            advances: &[],
            coverage: None,
        }
    }

    /// # Safety
    /// `view.data` must be non-null. `new`'s contract applies to it and every
    /// pointer/count pair in `view`. Coverage must describe the complete font,
    /// and advance entries must use SdCardFont::AdvanceEntry's sorted layout.
    pub unsafe fn from_sd(view: &'a SdView) -> Self {
        Self {
            data: unsafe { &*view.data },
            advances: unsafe { table(view.advances, view.advance_count as usize) },
            coverage: Some(Coverage {
                full: if view.bmp_intervals.is_null() {
                    unsafe { table(view.full_intervals, view.coverage_count as usize) }
                } else {
                    &[]
                },
                bmp: unsafe { table(view.bmp_intervals, view.coverage_count as usize) },
            }),
        }
    }

    pub fn data(&self) -> &FontData {
        self.data
    }

    /// Exact interval lookup, without replacement or loading.
    pub fn glyph_index(&self, cp: u32) -> Option<usize> {
        interval_index(
            unsafe { table(self.data.intervals, self.data.interval_count as usize) },
            cp,
        )
    }

    /// None means a dynamic coverage handler would be needed.
    pub fn has_codepoint(&self, cp: u32) -> Option<bool> {
        if self.glyph_index(cp).is_some() {
            return Some(true);
        }
        if let Some(coverage) = &self.coverage {
            let end = coverage.bmp.partition_point(|iv| u32::from(iv.first) <= cp);
            let in_bmp = end > 0 && cp <= u32::from(coverage.bmp[end - 1].last);
            return Some(in_bmp || interval_index(coverage.full, cp).is_some());
        }
        if self.data.coverage_handler.is_some() {
            None
        } else {
            Some(false)
        }
    }

    /// Raw 12.4 advance, including a cached zero. None is a table miss.
    /// GfxRenderer treats zero as a slow-path trigger for non-combining glyphs.
    pub fn advance(&self, cp: u32) -> Option<u16> {
        let i = self.advances.partition_point(|entry| entry.codepoint < cp);
        self.advances
            .get(i)
            .filter(|entry| entry.codepoint == cp)
            .map(|entry| entry.advance_x)
    }

    #[inline(always)]
    pub fn glyph(&self, cp: u32) -> GlyphResult {
        self.glyph_with_pointer(cp).0
    }

    /// The pointer is borrowed from this loan and must be consumed before any
    /// cache mutation. Metrics remain a value for callers that need no bitmap.
    #[inline(always)]
    pub fn glyph_with_pointer(&self, cp: u32) -> (GlyphResult, *const Glyph) {
        if let Some(index) = self.glyph_index(cp) {
            let ptr = unsafe { self.data.glyph.add(index) };
            return (GlyphResult::Resident(unsafe { *ptr }), ptr);
        }
        if self.data.glyph_miss_handler.is_some() {
            return (GlyphResult::NotResident, core::ptr::null());
        }
        if self.data.interval_count == 0 {
            return (GlyphResult::Missing, core::ptr::null());
        }
        if cp == 0x02bb {
            match self.has_codepoint(0x2018) {
                Some(true) => return self.glyph_with_pointer(0x2018),
                None => return (GlyphResult::NotResident, core::ptr::null()),
                Some(false) => (),
            }
        }
        if cp != 0xfffd {
            self.glyph_with_pointer(0xfffd)
        } else {
            (GlyphResult::Missing, core::ptr::null())
        }
    }

    pub fn metrics(&self, cp: u32) -> GlyphResult {
        if cp != 0x2588 && cp != 0x25a0 {
            return self.glyph(cp);
        }
        match self.has_codepoint(cp) {
            Some(true) => return self.glyph(cp),
            None => return GlyphResult::NotResident,
            Some(false) => (),
        }
        let em = match self.has_codepoint(u32::from(b'M')) {
            Some(true) => match self.glyph(u32::from(b'M')) {
                GlyphResult::Resident(glyph) => Some(glyph),
                GlyphResult::Missing => None,
                _ => return GlyphResult::NotResident,
            },
            Some(false) => None,
            None => return GlyphResult::NotResident,
        };
        GlyphResult::Synthetic(solid_metrics(
            cp,
            self.data.ascender,
            em.map(|glyph| glyph.advance_x),
        ))
    }

    /// Raw signed 4.4 kerning. None requests the dynamic callback face.
    pub fn kerning(&self, left: u32, right: u32) -> Option<i8> {
        if (left >= 0x1100 && crate::text::cjk(left))
            || (right >= 0x1100 && crate::text::cjk(right))
        {
            return Some(0);
        }
        let d = self.data;
        if d.kern_handler.is_some() {
            return None;
        }
        if (d.kern_matrix.is_null() && d.kern_row_offsets.is_null())
            || (d.kern_left_classes.is_null() && d.kern_left_codepoints.is_null())
        {
            return Some(0);
        }
        let split = !d.kern_left_codepoints.is_null();
        let lc = unsafe {
            kern_class(
                split,
                d.kern_left_classes,
                d.kern_left_codepoints,
                d.kern_left_class_ids,
                d.kern_left_entry_count,
                left,
            )
        };
        if lc == 0 {
            return Some(0);
        }
        let rc = unsafe {
            kern_class(
                split,
                d.kern_right_classes,
                d.kern_right_codepoints,
                d.kern_right_class_ids,
                d.kern_right_entry_count,
                right,
            )
        };
        if rc == 0 {
            return Some(0);
        }
        if !d.kern_row_offsets.is_null() {
            let start = unsafe { *d.kern_row_offsets.add(usize::from(lc - 1)) };
            let end = unsafe { *d.kern_row_offsets.add(usize::from(lc)) };
            for i in start..end {
                let col = unsafe { *d.kern_sparse_cols.add(usize::from(i)) };
                if col == rc - 1 {
                    return Some(unsafe { *d.kern_sparse_values.add(usize::from(i)) });
                }
                if col > rc - 1 {
                    break;
                }
            }
            Some(0)
        } else {
            let index =
                usize::from(lc - 1) * usize::from(d.kern_right_class_count) + usize::from(rc - 1);
            Some(unsafe { *d.kern_matrix.add(index) })
        }
    }

    pub fn ligature(&self, left: u32, right: u32) -> u32 {
        if left > 0xffff || right > 0xffff || arabic_form(left) || arabic_form(right) {
            return 0;
        }
        let pairs = unsafe {
            table(
                self.data.ligature_pairs,
                self.data.ligature_pair_count as usize,
            )
        };
        let key = (left << 16) | right;
        let i = lower_bound(pairs, |pair| pair.pair < key);
        pairs
            .get(i)
            .filter(|pair| pair.pair == key)
            .map_or(0, |pair| pair.ligature_cp)
    }

    /// Greedy substitution; position advances only for consumed input bytes.
    pub fn apply_ligatures(&self, mut cp: u32, text: &[u8], position: &mut usize) -> u32 {
        if self.data.ligature_pairs.is_null() || self.data.ligature_pair_count == 0 {
            return cp;
        }
        loop {
            let saved = *position;
            let Some(next) = crate::utf8::next(text, position) else {
                break;
            };
            let lig = self.ligature(cp, next);
            if lig == 0 {
                *position = saved;
                break;
            }
            cp = lig;
        }
        cp
    }
}

unsafe fn kern_class(
    split: bool,
    packed: *const KernClass,
    codepoints: *const u16,
    ids: *const u8,
    count: u16,
    cp: u32,
) -> u8 {
    if cp > 0xffff {
        return 0;
    }
    if split {
        let cps = unsafe { table(codepoints, usize::from(count)) };
        if cps.first().is_none_or(|&first| cp < u32::from(first)) {
            return 0;
        }
        let i = lower_bound(cps, |&value| value < cp as u16);
        if cps.get(i).copied() == Some(cp as u16) {
            unsafe { *ids.add(i) }
        } else {
            0
        }
    } else {
        let entries = unsafe { table(packed, usize::from(count)) };
        if entries
            .first()
            .is_none_or(|first| cp < u32::from(first.codepoint))
        {
            return 0;
        }
        let i = lower_bound(entries, |entry| entry.codepoint < cp as u16);
        entries
            .get(i)
            .filter(|entry| entry.codepoint == cp as u16)
            .map_or(0, |entry| entry.class_id)
    }
}

fn arabic_form(cp: u32) -> bool {
    matches!(cp, 0xfb50..=0xfdff | 0xfe70..=0xfeff)
}

/// Same integer evaluation order as EpdFont::getGlyphMetrics.
pub fn solid_metrics(cp: u32, ascender: i32, em_advance: Option<u16>) -> Glyph {
    let ascender = if ascender > 0 { ascender } else { 8 };
    let advance = em_advance
        .filter(|&a| a > 0)
        .unwrap_or_else(|| (((ascender.wrapping_mul(3).wrapping_add(3) / 4).max(1)) << 4) as u16);
    let advance_px = ((i32::from(advance) + 8) >> 4).max(1);
    let square = cp == 0x25a0;
    let height = (if square {
        ascender.wrapping_mul(2).wrapping_add(2) / 3
    } else {
        ascender
    })
    .clamp(1, 255);
    let width = (if square {
        height.min(advance_px)
    } else {
        advance_px
    })
    .min(255);
    let left = if square {
        ((advance_px - width) / 2).max(0)
    } else {
        0
    };
    let top = if square {
        height + ((ascender - height) / 2).max(0)
    } else {
        height
    };
    Glyph {
        width: width as u8,
        height: height as u8,
        advance_x: advance,
        left: left as i16,
        top: top as i16,
        data_length: 0,
        data_offset: 0,
    }
}
