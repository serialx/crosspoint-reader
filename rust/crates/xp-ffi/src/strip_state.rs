//! Caller-owned strip state: caller-provided bounds, invalidated whenever page/font geometry changes.
use xp_core::{
    glyph::{Clip, GlyphTarget},
    page::Element,
    strip::row_bounds,
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RowBounds {
    pub start: i32,
    pub end: i32,
}
#[repr(C)]
#[derive(Default)]
pub struct Stats {
    pub records: u64,
    pub drawn: u64,
    pub glyphs: u64,
}
pub struct State<'a> {
    pub bounds: &'a mut [RowBounds],
    pub stats: &'a mut Stats,
    pub prepare: bool,
    pub current: usize,
}
impl State<'_> {
    pub fn add(&mut self, target: &GlyphTarget, rect: Clip) {
        let (start, end) = row_bounds(target, rect);
        let b = &mut self.bounds[self.current];
        b.start = b.start.min(start.max(0));
        b.end = b.end.max(end.min(target.height));
    }
    pub fn unbounded(&mut self, target: &GlyphTarget) {
        self.bounds[self.current] = RowBounds {
            start: 0,
            end: target.height,
        };
    }
    pub fn element(
        &mut self,
        target: &GlyphTarget,
        index: u16,
        e: Element<'_>,
        x: i32,
        y: i32,
    ) -> bool {
        self.current = usize::from(index);
        self.stats.records += 1;
        if self.prepare {
            if let Ok((_, _, w, h)) = e.image() {
                self.add(
                    target,
                    Clip {
                        left: x,
                        top: y,
                        right: x + i32::from(w),
                        bottom: y + i32::from(h),
                    },
                );
            }
            return true;
        }
        let b = self.bounds[self.current];
        let visible = b.start < target.origin_y + target.rows && b.end > target.origin_y;
        self.stats.drawn += u64::from(visible);
        visible
    }
}

/// # Safety
/// Target owns its writable buffer; row is readable for len bytes. No loans escape.
#[no_mangle]
pub unsafe extern "C" fn xp_strip_packed_row(
    target: *const xp_core::image::frame::Target,
    x: i32,
    y: i32,
    width: i32,
    row: *const u8,
    len: usize,
) {
    if target.is_null() || row.is_null() || !(0..=65535).contains(&width) {
        return;
    }
    let t = &*target;
    if t.data.is_null() || t.len > u32::MAX as usize {
        return;
    }
    let (lw, lh) = if t.orientation & 1 == 0 {
        (t.height, t.width)
    } else {
        (t.width, t.height)
    };
    let target = GlyphTarget {
        buffer: t.data,
        buffer_len: t.len as u32,
        width: t.width,
        height: t.height,
        stride: t.stride,
        origin_y: t.origin,
        rows: t.rows,
        clip: Clip {
            left: 0,
            top: 0,
            right: lw,
            bottom: lh,
        },
        orientation: t.orientation,
        plane: t.mode,
        absolute: t.absolute,
        strip: 1,
        scanning: 0,
    };
    if !target.valid() {
        return;
    }
    let bytes = core::slice::from_raw_parts_mut(t.data, t.len);
    if let Some(mut strip) = xp_core::strip::Strip::new(target, bytes) {
        strip.packed_row(x, y, width as u16, core::slice::from_raw_parts(row, len));
    }
}
