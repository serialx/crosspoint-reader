//! Allocation-free selection spans. Each geometry byte loan ends before glyph callbacks.
use super::selection::{Layout, Painted, State, Word};
use crate::{
    draw,
    scene::{Command, Error, Kind, Rect},
};
use core::{ffi::c_void, slice};
use xp_core::{glyph::GlyphTarget, strip::Strip};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Fonts {
    pub context: *mut c_void,
    pub word: Option<unsafe extern "C" fn(*mut c_void, *const GlyphTarget, *const Word, i32, i32)>,
}
pub fn incremental(state: &State, words: &[Word], layout: &Layout, old: &Painted) -> u8 {
    if layout.touch != 0
        || old.valid == 0
        || old.page != state.page
        || old.selected < 0
        || old.selected as usize >= words.len()
    {
        return 0;
    }
    if state.offsets(words, layout) != (old.offset_x, old.offset_y) {
        return 0;
    }
    if state.range_start < 0 && old.range_start < 0 {
        return if state.selected == old.selected { 4 } else { 1 };
    }
    if state.range_start >= 0
        && old.range_start < 0
        && state.range_start == state.selected
        && state.selected == old.selected
    {
        return 2;
    }
    if state.range_start >= 0 && old.range_start == state.range_start {
        if state.selected == old.selected {
            return 4;
        }
        let indices = [
            state.range_start.min(old.selected),
            state.range_start.max(old.selected),
            state.range_start.min(state.selected),
            state.range_start.max(state.selected),
        ];
        if indices.iter().all(|&i| {
            words
                .get(i as usize)
                .is_some_and(|w| i32::from(w.pageOffset) == state.page)
        }) {
            return 3;
        }
    }
    0
}
struct Painter<'a> {
    target: &'a GlyphTarget,
    fonts: Fonts,
    words: &'a [Word],
    page: i32,
    x: i32,
    y: i32,
}
impl Painter<'_> {
    fn geometry(&mut self, r: Rect, paint: u8, stroke: bool) {
        let t = self.target;
        let target = GlyphTarget {
            buffer: t.buffer,
            buffer_len: t.buffer_len,
            width: t.width,
            height: t.height,
            stride: t.stride,
            origin_y: t.origin_y,
            rows: t.rows,
            clip: t.clip,
            orientation: t.orientation,
            plane: t.plane,
            absolute: t.absolute,
            strip: t.strip,
            scanning: t.scanning,
        };
        // Entry validated this descriptor. No byte loan survives the glyph leaf.
        let bytes = unsafe { slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize) };
        if let Some(mut band) = Strip::new(target, bytes) {
            draw::geometry(
                &mut band,
                &Command {
                    rect: r,
                    kind: if stroke { Kind::Stroke } else { Kind::Fill } as u8,
                    paint,
                    width: 1,
                    ..Command::default()
                },
            );
        }
    }
    fn word_rect(&self, w: &Word) -> Rect {
        Rect {
            x: (i32::from(w.x) + self.x) as i16,
            y: (i32::from(w.y) + self.y) as i16,
            w: w.width,
            h: w.height,
        }
    }
    fn gap(&mut self, a: usize, b: usize, paint: u8) {
        let (Some(a), Some(b)) = (self.words.get(a), self.words.get(b)) else {
            return;
        };
        if a.row != b.row || a.pageOffset != b.pageOffset {
            return;
        }
        let left = (i32::from(a.x) + i32::from(a.width)).min(i32::from(b.x) + i32::from(b.width));
        let right = i32::from(a.x).max(i32::from(b.x));
        if left < right {
            self.geometry(
                Rect {
                    x: (left + self.x) as i16,
                    y: (i32::from(a.y) + self.y) as i16,
                    w: (right - left) as i16,
                    h: a.height,
                },
                paint,
                false,
            );
        }
    }
    fn text(&self, w: &Word) {
        if let Some(callback) = self.fonts.word {
            unsafe {
                callback(
                    self.fonts.context,
                    self.target,
                    w,
                    i32::from(w.x) + self.x,
                    i32::from(w.y) + self.y,
                )
            };
        }
    }
    fn clean(&mut self, index: usize) {
        let Some(w) = self
            .words
            .get(index)
            .filter(|w| i32::from(w.pageOffset) == self.page)
        else {
            return;
        };
        self.geometry(self.word_rect(w), 1, false);
        if index > 0 {
            self.gap(index - 1, index, 1);
        }
        self.gap(index, index + 1, 1);
        self.text(w);
    }
    fn highlight(&mut self, index: usize, first: usize, last: usize) {
        let Some(w) = self
            .words
            .get(index)
            .filter(|w| i32::from(w.pageOffset) == self.page)
        else {
            return;
        };
        if index > first {
            self.gap(index - 1, index, 2);
        }
        if index < last {
            self.gap(index, index + 1, 2);
        }
        self.geometry(self.word_rect(w), 2, false);
        self.text(w);
    }
}
/// # Safety
/// Target storage is exclusively writable; words and strings are immutable and
/// disjoint from it. The callback obeys Fonts' synchronous borrowing contract.
pub unsafe fn paint(
    state: &State,
    words: &[Word],
    layout: &Layout,
    old: &Painted,
    target: &GlyphTarget,
    fonts: Fonts,
    mode: u8,
) -> Result<(), Error> {
    if !state.valid(words)
        || !target.valid()
        || target.buffer.is_null()
        || fonts.word.is_none()
        || words.len() > u16::MAX as usize
        || mode > 4
        || (mode != 0 && mode != incremental(state, words, layout, old))
        || words.iter().any(|w| w.text.is_null())
    {
        return Err(Error::Invalid);
    }
    let (x, y) = state.offsets(words, layout);
    let mut painter = Painter {
        target,
        fonts,
        words,
        page: state.page,
        x,
        y,
    };
    let (first, last) = state.bounds();
    match mode {
        0 => {
            let mut previous = None;
            for (index, w) in words.iter().enumerate().take(last + 1).skip(first) {
                if i32::from(w.pageOffset) != state.page {
                    continue;
                }
                if let Some(p) = previous {
                    painter.gap(p, index, 2);
                }
                painter.geometry(painter.word_rect(w), 2, false);
                painter.text(w);
                previous = Some(index);
            }
        }
        1 => {
            painter.clean(old.selected as usize);
            painter.highlight(state.selected as usize, first, last);
        }
        3 => {
            let first_old = state.range_start.min(old.selected) as usize;
            let last_old = state.range_start.max(old.selected) as usize;
            if last_old >= words.len() {
                return Err(Error::Invalid);
            }
            for i in first_old..=last_old {
                if i < first || i > last {
                    painter.clean(i);
                }
            }
            for i in first..=last {
                if i < first_old || i > last_old {
                    painter.highlight(i, first, last);
                }
            }
            if old.selected >= first as i32 && old.selected <= last as i32 {
                painter.highlight(old.selected as usize, first, last);
            }
        }
        2 | 4 => return Ok(()),
        _ => return Err(Error::Invalid),
    }
    if layout.touch == 0 || state.range_start < 0 {
        painter.geometry(painter.word_rect(&words[state.selected as usize]), 0, true);
    }
    Ok(())
}
