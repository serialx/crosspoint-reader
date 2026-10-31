//! The v19 wire layout is also the compact resident style representation.
use super::WIRE_BYTES;
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Style(pub [u8; WIRE_BYTES]);

// (wire offset, byte count) for each defined-property bit.
const FIELDS: [(usize, usize); 31] = [
    (0, 1),
    (1, 1),
    (2, 1),
    (3, 1),
    (5, 5),
    (10, 5),
    (15, 5),
    (20, 5),
    (25, 5),
    (30, 5),
    (35, 5),
    (40, 5),
    (45, 5),
    (50, 5),
    (55, 5),
    (65, 1),
    (4, 1),
    (66, 1),
    (67, 1),
    (60, 5),
    (68, 1),
    (69, 1),
    (70, 1),
    (71, 2),
    (73, 2),
    (75, 2),
    (77, 2),
    (80, 1),
    (81, 1),
    (82, 1),
    (83, 1),
];
impl Default for Style {
    fn default() -> Self {
        let mut s = Self([0; WIRE_BYTES]);
        s.0[0] = 1;
        s
    }
}
impl Style {
    pub fn flags(&self) -> u32 {
        u32::from_le_bytes([self.0[84], self.0[85], self.0[86], self.0[87]])
    }
    fn set_flags(&mut self, f: u32) {
        self.0[84..].copy_from_slice(&f.to_le_bytes());
    }
    fn mark(&mut self, bit: usize) {
        self.set_flags(self.flags() | (1 << bit));
    }
    fn scalar(&mut self, bit: usize, value: u8) {
        self.0[FIELDS[bit].0] = value;
        self.mark(bit);
    }
    fn length(&mut self, bit: usize, (value, unit): (f32, u8)) {
        let o = FIELDS[bit].0;
        self.0[o..o + 4].copy_from_slice(&value.to_le_bytes());
        self.0[o + 4] = unit;
        self.mark(bit);
    }
    pub fn apply(&mut self, other: &Self) {
        let flags = other.flags();
        for (bit, &(offset, len)) in FIELDS.iter().enumerate() {
            if flags & (1 << bit) == 0 {
                continue;
            }
            if (23..27).contains(&bit) {
                let side = bit - 23;
                if other.0[79] & (1 << side) != 0 {
                    self.0[offset] = other.0[offset];
                }
                if other.0[79] & (16 << side) != 0 {
                    self.0[offset + 1] = other.0[offset + 1];
                }
            } else {
                self.0[offset..offset + len].copy_from_slice(&other.0[offset..offset + len]);
            }
        }
        self.0[79] |= other.0[79];
        self.set_flags(self.flags() | flags);
    }
    pub fn valid(&self) -> bool {
        if self.0[0] > 4 || self.0[1] > 2 || self.0[2] > 2 || self.0[3] > 3 || self.0[4] > 1 {
            return false;
        }
        for i in 0..12 {
            let o = 5 + i * 5;
            if !f32::from_le_bytes([self.0[o], self.0[o + 1], self.0[o + 2], self.0[o + 3]])
                .is_finite()
                || self.0[o + 4] > 4
            {
                return false;
            }
        }
        if self.0[65] > 1 || self.0[66] > 2 || self.0[67] > 1 {
            return false;
        }
        for i in [68, 69, 70, 80, 81, 83] {
            if self.0[i] > 1 {
                return false;
            }
        }
        for i in [72, 74, 76, 78] {
            if self.0[i] > 4 {
                return false;
            }
        }
        self.0[82] <= 6 && self.flags() & 0x8000_0000 == 0
    }
    fn border_mark(&mut self, side: usize, width: bool, style: bool) {
        self.0[79] |= (u8::from(width) | (u8::from(style) << 4)) << side;
        self.mark(23 + side);
    }
    fn border(&mut self, name: &[u8], value: &[u8]) {
        let names: [&[u8]; 4] = [b"top", b"right", b"bottom", b"left"];
        if eq(name, b"border") {
            let (width, style) = border_shorthand(value);
            for i in 0..4 {
                self.0[71 + 2 * i] = width;
                self.0[72 + 2 * i] = style;
                self.border_mark(i, true, true);
            }
            return;
        }
        let widths = eq(name, b"border-width");
        if widths || eq(name, b"border-style") {
            let mut t: [&[u8]; 4] = [b""; 4];
            let n = tokens(value)
                .take(4)
                .enumerate()
                .map(|(i, v)| {
                    t[i] = v;
                    1
                })
                .sum::<usize>();
            if n == 0 {
                return;
            }
            let source = [
                0,
                if n >= 2 { 1 } else { 0 },
                if n >= 3 { 2 } else { 0 },
                if n >= 4 {
                    3
                } else if n >= 2 {
                    1
                } else {
                    0
                },
            ];
            for i in 0..4 {
                let v = if widths {
                    border_width(t[source[i]])
                } else {
                    border_style(t[source[i]])
                };
                if let Some(v) = v {
                    self.0[71 + 2 * i + usize::from(!widths)] = v;
                    self.border_mark(i, widths, !widths);
                }
            }
            return;
        }
        if name.len() <= 7 || name[6] != b'-' {
            return;
        }
        let prefix = &name[7..];
        for (i, n) in names.iter().enumerate() {
            if prefix.len() < n.len() || !eq(&prefix[..n.len()], n) {
                continue;
            }
            let suffix = &prefix[n.len()..];
            if suffix.is_empty() {
                let (w, s) = border_shorthand(value);
                self.0[71 + 2 * i] = w;
                self.0[72 + 2 * i] = s;
                self.border_mark(i, true, true);
            } else if eq(suffix, b"-width") {
                if let Some(w) = border_width(trim(value)) {
                    self.0[71 + 2 * i] = w;
                    self.border_mark(i, true, false);
                }
            } else if eq(suffix, b"-style") {
                if let Some(s) = border_style(trim(value)) {
                    self.0[72 + 2 * i] = s;
                    self.border_mark(i, false, true);
                }
            }
            return;
        }
    }
    pub fn declaration(&mut self, declaration: &[u8]) {
        let Some(colon) = declaration.iter().position(|&c| c == b':') else {
            return;
        };
        if colon == 0 {
            return;
        }
        let name = trim(&declaration[..colon]);
        let mut value = trim(&declaration[colon + 1..]);
        if name.is_empty() || value.is_empty() {
            return;
        }
        value = important(value);
        let scalar = if eq(name, b"text-align") {
            Some((
                0,
                if eq(value, b"right") || eq(value, b"end") {
                    3
                } else if eq(value, b"center") {
                    2
                } else if eq(value, b"justify") {
                    0
                } else {
                    1
                },
            ))
        } else if eq(name, b"font-style") {
            Some((
                1,
                if eq(value, b"inherit") {
                    2
                } else if eq(value, b"italic") || eq(value, b"oblique") {
                    1
                } else {
                    0
                },
            ))
        } else if eq(name, b"font-weight") {
            Some((
                2,
                if eq(value, b"inherit") {
                    2
                } else if eq(value, b"bold")
                    || eq(value, b"bolder")
                    || integer(value).is_some_and(|n| n >= 700)
                {
                    1
                } else {
                    0
                },
            ))
        } else if eq(name, b"text-decoration") || eq(name, b"text-decoration-line") {
            let mut v = 0;
            let mut none = false;
            for t in tokens(value) {
                if eq(t, b"none") {
                    none = true;
                } else if eq(t, b"underline") {
                    v |= 1;
                } else if eq(t, b"line-through") {
                    v |= 2;
                }
            }
            Some((3, if none { 0 } else { v }))
        } else if eq(name, b"display") {
            Some((15, u8::from(eq(value, b"none"))))
        } else if eq(name, b"direction") {
            if eq(value, b"rtl") {
                Some((16, 1))
            } else if eq(value, b"ltr") {
                Some((16, 0))
            } else {
                None
            }
        } else if eq(name, b"vertical-align") {
            if eq(value, b"super") {
                Some((17, 1))
            } else if eq(value, b"sub") {
                Some((17, 2))
            } else {
                None
            }
        } else if eq(name, b"list-style-type") {
            Some((18, u8::from(eq(important(value), b"none"))))
        } else if eq(name, b"font-variant") || eq(name, b"font-variant-caps") {
            Some((
                20,
                u8::from(tokens(value).any(|t| eq(t, b"small-caps") || eq(t, b"all-small-caps"))),
            ))
        } else if eq(name, b"page-break-before") || eq(name, b"break-before") {
            Some((21, u8::from(forced_break(value))))
        } else if eq(name, b"page-break-after") || eq(name, b"break-after") {
            Some((22, u8::from(forced_break(value))))
        } else if eq(name, b"background") || eq(name, b"background-color") {
            let l = luminance(value);
            Some((27, u8::from((128..240).contains(&l))))
        } else if eq(name, b"float") {
            Some((28, u8::from(eq(value, b"left"))))
        } else if eq(name, b"initial-letter") {
            let mut lines = 0;
            for t in tokens(value) {
                if lines == 0 {
                    if let Some(n) = number(t) {
                        if n >= 1.0 {
                            lines = (n + 0.5) as u32;
                        }
                    }
                }
            }
            Some((29, lines.min(6) as u8))
        } else if eq(name, b"white-space") {
            let p = eq(value, b"pre-wrap") || eq(value, b"pre") || eq(value, b"break-spaces");
            if p || eq(value, b"normal") {
                Some((30, u8::from(p)))
            } else {
                None
            }
        } else {
            None
        };
        if let Some((bit, v)) = scalar {
            self.scalar(bit, v);
            return;
        }
        const LENGTHS: [&[u8]; 11] = [
            b"text-indent",
            b"margin-top",
            b"margin-bottom",
            b"margin-left",
            b"margin-right",
            b"padding-top",
            b"padding-bottom",
            b"padding-left",
            b"padding-right",
            b"height",
            b"width",
        ];
        for (i, n) in LENGTHS.iter().enumerate() {
            if eq(name, n) {
                let v = length(value);
                if i < 9 || v.is_some() {
                    self.length(i + 4, v.unwrap_or((0.0, 0)));
                }
                return;
            }
        }
        if eq(name, b"font-size") {
            if let Some(v) = font_size(value) {
                self.length(19, v);
            }
            return;
        }
        let margin = eq(name, b"margin");
        if margin || eq(name, b"padding") {
            let mut t: [&[u8]; 4] = [b""; 4];
            let n = tokens(value)
                .take(4)
                .enumerate()
                .map(|(i, v)| {
                    t[i] = v;
                    1
                })
                .sum::<usize>();
            if n == 0 {
                return;
            }
            let top = length(t[0]).unwrap_or((0.0, 0));
            let right = if n >= 2 {
                length(t[1]).unwrap_or((0.0, 0))
            } else {
                top
            };
            let bottom = if n >= 3 {
                length(t[2]).unwrap_or((0.0, 0))
            } else {
                top
            };
            let left = if n >= 4 {
                length(t[3]).unwrap_or((0.0, 0))
            } else {
                right
            };
            let b = if margin { 5 } else { 9 };
            for (i, v) in [top, bottom, left, right].into_iter().enumerate() {
                self.length(b + i, v);
            }
            return;
        }
        if name.len() >= 6 && eq(&name[..6], b"border") {
            self.border(name, value);
        }
    }
}
pub fn inline(bytes: &[u8]) -> Style {
    let mut s = Style::default();
    for d in bytes.split(|&c| c == b';') {
        s.declaration(d);
    }
    s
}
pub(super) fn whitespace(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | b'\r' | b'\x0c')
}
pub(super) fn trim(mut b: &[u8]) -> &[u8] {
    while b.first().is_some_and(|&c| whitespace(c)) {
        b = &b[1..];
    }
    while b.last().is_some_and(|&c| whitespace(c)) {
        b = &b[..b.len() - 1];
    }
    b
}
pub(super) fn tokens(b: &[u8]) -> impl Iterator<Item = &[u8]> {
    b.split(|&c| whitespace(c)).filter(|t| !t.is_empty())
}
pub(super) fn eq(a: &[u8], b: &[u8]) -> bool {
    a.eq_ignore_ascii_case(b)
}
fn important(v: &[u8]) -> &[u8] {
    let v = trim(v);
    if v.len() >= 10 && eq(&v[v.len() - 10..], b"!important") {
        trim(&v[..v.len() - 10])
    } else {
        v
    }
}
fn number(v: &[u8]) -> Option<f32> {
    let v = v.strip_prefix(b"+").unwrap_or(v);
    // from_chars does not accept whitespace or a second leading plus.
    if v.is_empty() || v[0] == b'+' || v.iter().any(|&c| c.is_ascii_whitespace()) {
        return None;
    }
    let n = crate::text::parse_f32(v)?;
    if !n.is_finite()
        && ![
            b"inf".as_slice(),
            b"infinity",
            b"-inf",
            b"-infinity",
            b"nan",
            b"-nan",
        ]
        .iter()
        .any(|v2| eq(v, v2))
    {
        return None;
    }
    // from_chars reports range errors for nonzero values rounded to zero.
    if n == 0.0
        && v.iter()
            .take_while(|&&c| c != b'e' && c != b'E')
            .any(|&c| (b'1'..=b'9').contains(&c))
    {
        return None;
    }
    Some(n)
}
fn integer(v: &[u8]) -> Option<isize> {
    let v = v.strip_prefix(b"+").unwrap_or(v);
    if v.first() == Some(&b'+') {
        return None;
    }
    core::str::from_utf8(v).ok()?.parse().ok()
}
fn numeric_prefix(v: &[u8], signed: bool) -> usize {
    v.iter()
        .position(|&c| !c.is_ascii_digit() && c != b'.' && !(signed && (c == b'+' || c == b'-')))
        .unwrap_or(v.len())
}
fn length(v: &[u8]) -> Option<(f32, u8)> {
    let v = trim(v);
    let i = numeric_prefix(v, true);
    let n = number(&v[..i])?;
    let u = &v[i..];
    Some((
        n,
        if eq(u, b"em") {
            1
        } else if eq(u, b"rem") {
            2
        } else if eq(u, b"pt") {
            3
        } else if u == b"%" {
            4
        } else {
            0
        },
    ))
}
fn font_size(v: &[u8]) -> Option<(f32, u8)> {
    const NAMES: [&[u8]; 10] = [
        b"xx-small",
        b"x-small",
        b"small",
        b"medium",
        b"large",
        b"x-large",
        b"xx-large",
        b"xxx-large",
        b"smaller",
        b"larger",
    ];
    const SCALES: [f32; 10] = [0.6, 0.75, 0.89, 1.0, 1.2, 1.5, 2.0, 3.0, 0.83, 1.2];
    for (i, k) in NAMES.iter().enumerate() {
        if eq(v, k) {
            return Some((SCALES[i], if i >= 8 { 1 } else { 2 }));
        }
    }
    let i = numeric_prefix(v, true);
    let n = number(&v[..i])?;
    if n <= 0.0 {
        return None;
    }
    let u = &v[i..];
    if u.is_empty() || eq(u, b"px") {
        Some((n / 16.0, 2))
    } else if eq(u, b"pt") {
        Some((n / 12.0, 2))
    } else if eq(u, b"rem") {
        Some((n, 2))
    } else if eq(u, b"em") {
        Some((n, 1))
    } else if u == b"%" {
        Some((n / 100.0, 1))
    } else if eq(u, b"ex") || eq(u, b"ch") {
        Some((n / 2.0, 1))
    } else {
        None
    }
}
fn forced_break(v: &[u8]) -> bool {
    [
        b"always".as_slice(),
        b"page",
        b"left",
        b"right",
        b"recto",
        b"verso",
    ]
    .iter()
    .any(|k| eq(v, k))
}
fn border_width(v: &[u8]) -> Option<u8> {
    for (i, k) in [b"thin".as_slice(), b"medium", b"thick"].iter().enumerate() {
        if eq(v, k) {
            return Some((i + 1) as u8);
        }
    }
    let i = numeric_prefix(v, false);
    if i == 0 {
        return None;
    }
    let mut n = number(&v[..i])?;
    let u = &v[i..];
    if eq(u, b"pt") {
        n = n * 4.0 / 3.0;
    } else if eq(u, b"em") || eq(u, b"rem") {
        n *= 16.0;
    } else if !u.is_empty() && !eq(u, b"px") {
        return None;
    }
    Some(if n <= 0.0 {
        0
    } else {
        (n + 0.5).clamp(1.0, 16.0) as u8
    })
}
fn border_style(v: &[u8]) -> Option<u8> {
    if eq(v, b"none") || eq(v, b"hidden") {
        Some(0)
    } else if [
        b"solid".as_slice(),
        b"groove",
        b"ridge",
        b"inset",
        b"outset",
    ]
    .iter()
    .any(|k| eq(v, k))
    {
        Some(1)
    } else if eq(v, b"double") {
        Some(2)
    } else if eq(v, b"dotted") {
        Some(3)
    } else if eq(v, b"dashed") {
        Some(4)
    } else {
        None
    }
}
fn border_shorthand(v: &[u8]) -> (u8, u8) {
    let mut w = None;
    let mut s = 0;
    for t in tokens(v) {
        if let Some(n) = border_width(t) {
            w = Some(n);
        } else if let Some(n) = border_style(t) {
            s = n;
        }
    }
    (w.unwrap_or(2), s)
}
fn nibble(c: u8) -> i32 {
    match c.to_ascii_lowercase() {
        b'0'..=b'9' => (c - b'0') as i32,
        b'a'..=b'f' => (c.to_ascii_lowercase() - b'a' + 10) as i32,
        _ => -1,
    }
}
fn luminance(v: &[u8]) -> i32 {
    let lum = |r: i32, g: i32, b: i32| {
        r.wrapping_mul(299)
            .wrapping_add(g.wrapping_mul(587))
            .wrapping_add(b.wrapping_mul(114))
            / 1000
    };
    if let Some(rgb) = v.windows(3).position(|w| w == b"rgb") {
        let Some(open) = v[rgb..].iter().position(|&c| c == b'(') else {
            return -1;
        };
        let mut channels = [0; 3];
        let mut count = 0;
        let mut current: i32 = -1;
        for &c in &v[rgb + open..] {
            if count == 3 {
                break;
            }
            if c.is_ascii_digit() {
                current = (if current < 0 {
                    0
                } else {
                    current.wrapping_mul(10)
                })
                .wrapping_add((c - b'0') as i32);
            } else if current >= 0 {
                channels[count] = if c == b'%' {
                    current.wrapping_mul(255) / 100
                } else {
                    current.min(255)
                };
                count += 1;
                current = -1;
            }
        }
        return if count == 3 {
            lum(channels[0], channels[1], channels[2])
        } else {
            -1
        };
    }
    const NAMES: [(&[u8], i32); 18] = [
        (b"white", 255),
        (b"whitesmoke", 245),
        (b"gainsboro", 220),
        (b"lightgray", 211),
        (b"lightgrey", 211),
        (b"silver", 192),
        (b"darkgray", 169),
        (b"darkgrey", 169),
        (b"gray", 128),
        (b"grey", 128),
        (b"dimgray", 105),
        (b"dimgrey", 105),
        (b"black", 0),
        (b"ivory", 254),
        (b"beige", 243),
        (b"linen", 244),
        (b"lavender", 235),
        (b"transparent", 255),
    ];
    for t in tokens(v) {
        if t.len() > 1 && t[0] == b'#' {
            let h = &t[1..];
            let mut c = [-1; 3];
            if h.len() == 3 || h.len() == 4 {
                for i in 0..3 {
                    c[i] = nibble(h[i]) * 17;
                }
            } else if h.len() == 6 || h.len() == 8 {
                for i in 0..3 {
                    c[i] = nibble(h[2 * i]) * 16 + nibble(h[2 * i + 1]);
                }
            }
            if c.iter().all(|&n| n >= 0) {
                return lum(c[0], c[1], c[2]);
            }
        } else {
            for (n, l) in NAMES {
                if eq(t, n) {
                    return l;
                }
            }
        }
    }
    -1
}
