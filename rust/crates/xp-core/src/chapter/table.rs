use super::{services::Span, whitespace, Measure, State};
use core::ffi::c_void;

pub type Read = unsafe extern "C" fn(*mut c_void, *mut u8, u32) -> u32;
struct Scan {
    word: [u8; 40],
    word_len: usize,
    word_width: i32,
    tag: [u8; 48],
    tag_len: usize,
    in_tag: bool,
    in_entity: bool,
    quote: u8,
    nesting: i32,
    column: i32,
    in_cell: bool,
    skip_cell: bool,
    line_width: i32,
    style: u8,
    sd: bool,
    space: i32,
    result: Measure,
}
impl Scan {
    fn chunk(&mut self, state: &mut State) {
        if self.word_len == 0 {
            return;
        }
        self.word[self.word_len] = 0;
        let word = Span::new(&self.word[..self.word_len]);
        if self.sd {
            state.call_prepare_text(word, 1 << self.style);
        }
        self.word_width = self
            .word_width
            .wrapping_add(state.call_text_advance(word, self.style));
        self.word_len = 0;
    }
    fn word(&mut self, state: &mut State) {
        self.chunk(state);
        if self.word_width == 0 || self.column < 0 {
            return;
        }
        if let Some(min) = self.result.min.get_mut(self.column as usize) {
            *min = i32::from(*min).max(self.word_width) as u16;
        }
        self.line_width = self
            .line_width
            .wrapping_add(if self.line_width > 0 { self.space } else { 0 })
            .wrapping_add(self.word_width);
        self.word_width = 0;
    }
    fn line(&mut self, state: &mut State) {
        self.word(state);
        if self.column >= 0 {
            if let Some(pref) = self.result.preferred.get_mut(self.column as usize) {
                *pref = i32::from(*pref).max(self.line_width) as u16;
            }
        }
        self.line_width = 0;
    }
    fn tag(&mut self, state: &mut State) -> i8 {
        self.tag[self.tag_len] = 0;
        let closing = self.tag[0] == b'/';
        let start = usize::from(closing);
        let end = self.tag[start..self.tag_len]
            .iter()
            .position(|&b| whitespace(b) || b == b'/')
            .map_or(self.tag_len, |n| start + n);
        // Copy the tiny classification result before a mutable scanner operation.
        let name = &self.tag[start..end];
        let table = name == b"table";
        let row = name == b"tr";
        let cell = name == b"td" || name == b"th";
        let heading = name == b"th";
        let br = name == b"br";
        let block = matches!(name, b"p" | b"div" | b"li") || (name.len() == 2 && name[0] == b'h');
        if table {
            self.nesting += if closing { -1 } else { 1 };
            return if self.nesting == 0 { 1 } else { 0 };
        }
        if self.nesting != 1 {
            self.word(state);
            return 0;
        }
        if row && !closing {
            if self.in_cell {
                self.line(state);
            }
            self.in_cell = false;
            self.column = -1;
        } else if cell {
            if self.in_cell {
                self.line(state);
            }
            self.in_cell = !closing;
            if !closing {
                let tag = &self.tag[..self.tag_len];
                self.skip_cell = tag.windows(7).any(|s| s == b"colspan" || s == b"rowspan");
                self.column += 1;
                if self.column >= 4 {
                    return -1;
                }
                self.result.columns = self.result.columns.max((self.column + 1) as u8);
                self.style = u8::from(heading);
            }
        } else if br {
            self.line(state);
        } else if block {
            self.word(state);
        }
        0
    }
}
impl State {
    /// # Safety
    /// read is synchronous and writes no more than the supplied capacity.
    pub unsafe fn measure_table(&mut self, ctx: *mut c_void, read: Read) -> Option<Measure> {
        let sd = self.call_body_sd() != 0;
        let space = self.call_body_space();
        let mut scan = Scan {
            word: [0; 40],
            word_len: 0,
            word_width: 0,
            tag: [0; 48],
            tag_len: 0,
            in_tag: false,
            in_entity: false,
            quote: 0,
            nesting: 0,
            column: -1,
            in_cell: false,
            skip_cell: false,
            line_width: 0,
            style: 0,
            sd,
            space,
            result: Measure::default(),
        };
        let mut buffer = [0u8; 96];
        let mut scanned = 0;
        while scanned < 32768 {
            let count = read(ctx, buffer.as_mut_ptr(), 96) as usize;
            if count == 0 || count > buffer.len() {
                return None;
            }
            scanned += count;
            for &c in &buffer[..count] {
                if scan.in_tag {
                    if scan.quote != 0 {
                        if c == scan.quote {
                            scan.quote = 0;
                        }
                    } else if matches!(c, b'"' | b'\'') {
                        scan.quote = c;
                    } else if c == b'>' {
                        scan.in_tag = false;
                        let result = scan.tag(self);
                        if result != 0 {
                            return (result > 0 && scan.result.columns >= 2).then_some(scan.result);
                        }
                        continue;
                    }
                    if scan.tag_len + 1 < scan.tag.len() {
                        scan.tag[scan.tag_len] = c.to_ascii_lowercase();
                        scan.tag_len += 1;
                    }
                    continue;
                }
                if c == b'<' {
                    scan.in_tag = true;
                    scan.tag_len = 0;
                    continue;
                }
                if !scan.in_cell || scan.skip_cell || scan.nesting != 1 {
                    continue;
                }
                if scan.in_entity {
                    scan.in_entity = c != b';';
                    continue;
                }
                if whitespace(c) {
                    scan.word(self);
                    continue;
                }
                if scan.word_len + 1 >= scan.word.len() {
                    scan.chunk(self);
                }
                scan.in_entity = c == b'&';
                scan.word[scan.word_len] = if scan.in_entity { b'n' } else { c };
                scan.word_len += 1;
            }
        }
        None
    }
    pub fn plan_columns(&mut self, measure: Measure) {
        self.column_count = 0;
        let columns = usize::from(measure.columns);
        if !(2..=4).contains(&columns) {
            return;
        }
        let padding = 8;
        let available = i32::from(self.call_page_width());
        let mut min = [0i32; 4];
        let mut pref = [0i32; 4];
        let mut min_total = 0;
        let mut pref_total = 0;
        for c in 0..columns {
            min[c] = i32::from(measure.min[c]) + padding;
            pref[c] = min[c].max(i32::from(measure.preferred[c]) + padding);
            min_total += min[c];
            pref_total += pref[c];
        }
        let mut assigned = 0;
        for c in 0..columns {
            let mut width = if pref_total <= available {
                pref[c] + (available - pref_total).wrapping_mul(pref[c]) / pref_total
            } else if min_total <= available {
                min[c]
                    + (available - min_total).wrapping_mul(pref[c] - min[c])
                        / (pref_total - min_total)
            } else {
                available.wrapping_mul(min[c]) / min_total
            };
            if c + 1 == columns {
                width = available - assigned;
            }
            self.column_widths[c] = width.max(padding + 1) as u16;
            assigned += width;
        }
        #[cfg(feature = "chapter-trace")]
        {
            self.trace(
                6,
                &[columns as u32],
                super::Span::new(&[]),
                super::Span::new(&[]),
                core::ptr::null(),
            );
            for c in 0..columns {
                self.trace(
                    7,
                    &[
                        c as u32,
                        u32::from(measure.min[c]),
                        u32::from(measure.preferred[c]),
                        u32::from(self.column_widths[c]),
                    ],
                    super::Span::new(&[]),
                    super::Span::new(&[]),
                    core::ptr::null(),
                );
            }
        }
        self.column_count = columns as u8;
    }
}
