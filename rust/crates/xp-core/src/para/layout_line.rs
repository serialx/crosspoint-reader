//! Integer line geometry. Renderer metrics and MiniBidi order remain host inputs.
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WordMetrics {
    pub width: u16,
    pub continues: u8,
    pub space: u8,
}
#[repr(C)]
pub struct Callbacks {
    pub context: *mut c_void,
    pub word: unsafe extern "C" fn(*mut c_void, usize) -> WordMetrics,
    pub gap: unsafe extern "C" fn(*mut c_void, usize) -> i32,
}
#[repr(C)]
pub struct Spec {
    pub count: usize,
    pub page: i32,
    pub indent: i32,
    pub extra_start: i32,
    pub extra_end: i32,
    pub alignment: u8,
    pub rtl: u8,
    pub reordered: u8,
    pub last: u8,
}
#[repr(C)]
#[derive(Default)]
pub struct Metrics {
    pub width: i32,
    pub natural_gaps: i32,
    pub gap_count: usize,
    pub spare: i32,
    pub justify: i32,
}
#[derive(Clone, Copy)]
enum Alignment {
    Justify,
    Left,
    Center,
    Right,
    None,
}
impl Alignment {
    fn from_wire(value: u8) -> Self {
        match value {
            0 => Self::Justify,
            1 => Self::Left,
            2 => Self::Center,
            3 => Self::Right,
            _ => Self::None,
        }
    }
}

/// # Safety
/// The callbacks borrow a stable count-word line and never reenter layout.
/// gap is the natural advance before index (queried only for index > 0).
pub unsafe fn metrics(s: &Spec, c: &Callbacks) -> Metrics {
    let mut m = Metrics::default();
    for i in 0..s.count {
        let w = (c.word)(c.context, i);
        m.width += i32::from(w.width);
        if i == 0 {
            continue;
        }
        let stretchable =
            w.space != 0 && i + 1 < s.count && (c.word)(c.context, i + 1).continues != 0;
        if w.continues == 0 || stretchable {
            m.gap_count += 1;
        }
        m.natural_gaps += (c.gap)(c.context, i);
    }
    m.spare = s.page - s.indent - s.extra_start - s.extra_end - m.width - m.natural_gaps;
    if s.alignment == 0 && s.last == 0 && m.gap_count >= 1 && m.spare > 0 {
        m.justify = m.spare / m.gap_count as i32;
    }
    m
}

/// # Safety
/// Same callback contract as metrics. out has count writable i16 elements,
/// disjoint from all inputs. Metrics must have been computed for this same line.
pub unsafe fn position(s: &Spec, c: &Callbacks, m: &Metrics, out: *mut i16) {
    let effective = s.page - s.indent;
    let alignment = Alignment::from_wire(s.alignment);
    let justify = s.alignment == 0 && s.last == 0;
    let mut x = if s.reordered != 0 {
        let contribution = if justify {
            m.justify * m.gap_count as i32
        } else {
            0
        };
        let content = m.width + m.natural_gaps + contribution;
        match (s.rtl != 0, alignment) {
            (true, Alignment::Right | Alignment::Justify) | (false, Alignment::Right) => {
                effective - content
            }
            (_, Alignment::Center) => (effective - content) / 2,
            (true, _) => 0,
            (false, _) => s.indent,
        }
    } else if s.rtl != 0 {
        match alignment {
            Alignment::Left => m.width + m.natural_gaps,
            Alignment::Center => (effective + m.width + m.natural_gaps) / 2,
            _ => effective,
        }
    } else {
        match alignment {
            Alignment::Right => effective - m.width - m.natural_gaps,
            Alignment::Center => (effective - m.width - m.natural_gaps) / 2,
            _ => s.indent + s.extra_start,
        }
    };
    let reverse = s.reordered == 0 && s.rtl != 0;
    for i in 0..s.count {
        let w = (c.word)(c.context, i);
        if reverse {
            x -= i32::from(w.width);
        }
        out.add(i).write(x as i16);
        if !reverse {
            x += i32::from(w.width);
        }
        if i + 1 == s.count {
            continue;
        }
        let next = (c.word)(c.context, i + 1);
        let mut advance = (c.gap)(c.context, i + 1);
        if justify && (next.continues == 0 || (i > 0 && w.space != 0 && w.continues != 0)) {
            advance += m.justify;
        }
        if reverse {
            x -= advance;
        } else {
            x += advance;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Line {
        words: [WordMetrics; 3],
        gaps: [i32; 3],
    }
    unsafe extern "C" fn word(p: *mut c_void, i: usize) -> WordMetrics {
        (*p.cast::<Line>()).words[i]
    }
    unsafe extern "C" fn gap(p: *mut c_void, i: usize) -> i32 {
        (*p.cast::<Line>()).gaps[i]
    }
    #[test]
    fn justification_rtl_visual_order_and_center_rounding() {
        let mut line = Line {
            words: [WordMetrics {
                width: 10,
                continues: 0,
                space: 0,
            }; 3],
            gaps: [0, 5, 5],
        };
        let c = Callbacks {
            context: (&mut line as *mut Line).cast(),
            word,
            gap,
        };
        let mut s = Spec {
            count: 3,
            page: 100,
            indent: 0,
            extra_start: 0,
            extra_end: 0,
            alignment: 0,
            rtl: 0,
            reordered: 0,
            last: 0,
        };
        let mut output = [0; 3];
        unsafe {
            let m = metrics(&s, &c);
            assert_eq!(
                (m.width, m.natural_gaps, m.gap_count, m.justify),
                (30, 10, 2, 30)
            );
            position(&s, &c, &m, output.as_mut_ptr());
            assert_eq!(output, [0, 45, 90]);
            s.rtl = 1;
            position(&s, &c, &m, output.as_mut_ptr());
            assert_eq!(output, [90, 45, 0]);
            s.reordered = 1;
            position(&s, &c, &m, output.as_mut_ptr());
            assert_eq!(output, [0, 45, 90]);
            s.alignment = 2;
            s.page = 35;
            s.rtl = 0;
            s.reordered = 0;
            let m = metrics(&s, &c);
            position(&s, &c, &m, output.as_mut_ptr());
            assert_eq!(output, [-2, 13, 28]);
        }
    }
    #[test]
    fn leading_attached_space_does_not_receive_extra_justification() {
        let mut line = Line {
            words: [
                WordMetrics {
                    width: 4,
                    continues: 1,
                    space: 1,
                },
                WordMetrics {
                    width: 10,
                    continues: 1,
                    space: 0,
                },
                WordMetrics {
                    width: 10,
                    continues: 0,
                    space: 0,
                },
            ],
            gaps: [0, 0, 5],
        };
        let c = Callbacks {
            context: (&mut line as *mut Line).cast(),
            word,
            gap,
        };
        let s = Spec {
            count: 3,
            page: 100,
            indent: 0,
            extra_start: 0,
            extra_end: 0,
            alignment: 0,
            rtl: 0,
            reordered: 0,
            last: 0,
        };
        let mut output = [0; 3];
        unsafe {
            let m = metrics(&s, &c);
            assert_eq!(m.gap_count, 1);
            assert_eq!(m.justify, 71);
            position(&s, &c, &m, output.as_mut_ptr());
            assert_eq!(output, [0, 4, 90]);
        }
    }
}
