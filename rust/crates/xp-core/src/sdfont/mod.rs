//! .cpfont v4 decoding and resident cache algorithms. HAL file I/O stays in C++.
pub mod pool;
use crate::font::raw::Interval;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Header {
    pub interval_count: u32,
    pub glyph_count: u32,
    pub advance_y: u8,
    pub ascender: i16,
    pub descender: i16,
    pub is2_bit: bool,
    pub kern_left_entry_count: u16,
    pub kern_right_entry_count: u16,
    pub kern_left_class_count: u8,
    pub kern_right_class_count: u8,
    pub ligature_pair_count: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Directory {
    pub hash: u32,
    pub style_count: u8,
    pub is2_bit: bool,
    pub seen: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Style {
    pub header: Header,
    pub offsets: [u32; 7],
    pub id: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Intervals {
    pub expected_offset: u32,
    pub previous_last: u32,
    pub bmp16: bool,
}

fn u16le(b: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([b[at], b[at + 1]])
}
fn u32le(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]])
}
fn hash(bytes: &[u8], seed: u32) -> u32 {
    bytes
        .iter()
        .fold(seed, |h, b| (h ^ u32::from(*b)).wrapping_mul(16777619))
}

pub fn header(bytes: &[u8]) -> Option<Directory> {
    let b = bytes.get(..32)?;
    if &b[..8] != b"CPFONT\0\0" || u16le(b, 8) != 4 || !(1..=4).contains(&b[12]) {
        return None;
    }
    Some(Directory {
        hash: hash(b, 2166136261),
        style_count: b[12],
        is2_bit: u16le(b, 10) & 1 != 0,
        seen: 0,
    })
}

pub fn style(bytes: &[u8], directory: &mut Directory) -> Option<Style> {
    let b = bytes.get(..32)?;
    let id = b[0];
    if id >= 4 || directory.seen & (1 << id) != 0 {
        return None;
    }
    let h = Header {
        interval_count: u32le(b, 4),
        glyph_count: u32le(b, 8),
        advance_y: b[12],
        ascender: u16le(b, 13) as i16,
        descender: u16le(b, 15) as i16,
        is2_bit: directory.is2_bit,
        kern_left_entry_count: u16le(b, 17),
        kern_right_entry_count: u16le(b, 19),
        kern_left_class_count: b[21],
        kern_right_class_count: b[22],
        ligature_pair_count: b[23],
    };
    if h.interval_count > 4096
        || h.glyph_count > 65536
        || h.kern_left_entry_count > 4096
        || h.kern_right_entry_count > 4096
    {
        return None;
    }
    // Preserve the loader's uint32 offset wrap and deferred table-read checks.
    let mut offsets = [0; 7];
    offsets[0] = u32le(b, 24);
    let sizes = [
        h.interval_count * 12,
        h.glyph_count * 16,
        u32::from(h.kern_left_entry_count) * 3,
        u32::from(h.kern_right_entry_count) * 3,
        u32::from(h.kern_left_class_count) * u32::from(h.kern_right_class_count),
        u32::from(h.ligature_pair_count) * 8,
    ];
    for i in 0..6 {
        offsets[i + 1] = offsets[i].wrapping_add(sizes[i]);
    }
    directory.hash = hash(b, directory.hash);
    directory.seen |= 1 << id;
    Some(Style {
        header: h,
        offsets,
        id,
    })
}

pub fn interval(bytes: &[u8], glyphs: u32, index: u32, state: &mut Intervals) -> Option<Interval> {
    let b = bytes.get(..12)?;
    let iv = Interval {
        first: u32le(b, 0),
        last: u32le(b, 4),
        offset: u32le(b, 8),
    };
    if iv.first > iv.last {
        return None;
    }
    // The legacy validator accepts the full uint32 range as a zero-length span.
    let span = iv.last.wrapping_sub(iv.first).wrapping_add(1);
    if (index > 0 && iv.first <= state.previous_last)
        || span > glyphs
        || iv.offset != state.expected_offset
        || iv.offset > glyphs.wrapping_sub(span)
    {
        return None;
    }
    state.bmp16 &= iv.first <= 65535 && iv.last <= 65535 && iv.offset <= 65535;
    state.expected_offset = state.expected_offset.wrapping_add(span);
    state.previous_last = iv.last;
    Some(iv)
}

pub fn glyph_valid(bytes: &[u8], two_bit: bool, bitmap_offset: u32, file_size: u32) -> bool {
    let Some(b) = bytes.get(..16) else {
        return false;
    };
    let pixels = u32::from(b[0]) * u32::from(b[1]);
    let packed = if two_bit {
        pixels.div_ceil(4)
    } else {
        pixels.div_ceil(8)
    };
    let length = u16le(b, 8);
    u32::from(length) == packed
        && u64::from(bitmap_offset) + u64::from(u32le(b, 12)) + u64::from(length)
            <= u64::from(file_size)
}

pub fn kern_class(bytes: &[u8], count: u8) -> Option<(u32, u8)> {
    let b = bytes.get(..3)?;
    Some((u32::from(u16le(b, 0)), if b[2] <= count { b[2] } else { 0 }))
}

pub fn ligature(bytes: &[u8]) -> Option<(u32, u32, u32)> {
    let b = bytes.get(..8)?;
    let pair = u32le(b, 0);
    Some((pair >> 16, pair & 65535, u32le(b, 4)))
}

pub fn filename(bytes: &[u8]) -> Option<u8> {
    let base = bytes.strip_suffix(b".cpfont")?;
    if base.is_empty() || base.len() > 127 {
        return None;
    }
    let underscore = base.iter().rposition(|b| *b == b'_')?;
    if underscore == 0 {
        return None;
    }
    let mut size = &base[underscore + 1..];
    while size
        .first()
        .is_some_and(|b| matches!(b, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c))
    {
        size = &size[1..];
    }
    if size.first() == Some(&b'+') {
        size = &size[1..];
    }
    if size.is_empty() {
        return None;
    }
    let mut value = 0u16;
    for &b in size {
        if !b.is_ascii_digit() {
            return None;
        }
        value = value * 10 + u16::from(b - b'0');
        if value > 255 {
            return None;
        }
    }
    (value > 0).then_some(value as u8)
}

const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(size_of::<Header>() == 24);
    assert!(offset_of!(Header, ascender) == 10);
    assert!(offset_of!(Header, kern_left_entry_count) == 16);
    assert!(size_of::<Directory>() == 8);
    assert!(size_of::<Style>() == 56);
    assert!(offset_of!(Style, offsets) == 24);
    assert!(offset_of!(Style, id) == 52);
    assert!(size_of::<Intervals>() == 12);
};

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn short_inputs_and_mutations_never_panic() {
        let mut bytes = [0u8; 160];
        bytes[..8].copy_from_slice(b"CPFONT\0\0");
        bytes[8] = 4;
        bytes[12] = 4;
        for n in 0..bytes.len() {
            for bit in 0..8 {
                bytes[n] ^= 1 << bit;
                for len in 0..=bytes.len() {
                    let b = &bytes[..len];
                    let _ = header(b);
                    let _ = style(b, &mut Directory::default());
                    let _ = interval(b, u32::MAX, 0, &mut Intervals::default());
                    let _ = glyph_valid(b, true, u32::MAX, u32::MAX);
                    let _ = kern_class(b, 255);
                    let _ = ligature(b);
                    let _ = filename(b);
                }
                bytes[n] ^= 1 << bit;
            }
        }
    }
    #[test]
    fn directory_limits_and_offset_wrap() {
        let mut b = [0u8; 32];
        b[4..8].copy_from_slice(&4096u32.to_le_bytes());
        b[8..12].copy_from_slice(&65536u32.to_le_bytes());
        b[17..19].copy_from_slice(&4096u16.to_le_bytes());
        b[19..21].copy_from_slice(&4096u16.to_le_bytes());
        b[21..24].fill(255);
        b[24..28].copy_from_slice(&u32::MAX.to_le_bytes());
        let mut d = Directory::default();
        let s = style(&b, &mut d).unwrap();
        assert_eq!(s.offsets[1], 4096 * 12 - 1);
        assert_eq!(
            s.offsets[6],
            4096 * 12 + 65536 * 16 + 4096 * 6 + 255 * 255 + 255 * 8 - 1
        );
        let before = d.hash;
        assert!(style(&b, &mut d).is_none());
        assert_eq!(before, d.hash);
        for offset in [4, 8, 17, 19] {
            let mut bad = b;
            bad[offset] += 1;
            assert!(style(&bad, &mut Directory::default()).is_none());
        }
    }
    #[test]
    fn glyph_extent_and_deferred_records() {
        let mut b = [0u8; 16];
        b[0] = 8;
        b[1] = 8;
        b[8] = 8;
        b[12] = 4;
        assert!(glyph_valid(&b, false, 100, 112));
        assert!(!glyph_valid(&b, false, 100, 111));
        assert!(!glyph_valid(&b, false, u32::MAX, u32::MAX));
        assert_eq!(kern_class(&[0x34, 0x12, 5], 4), Some((0x1234, 0)));
        assert_eq!(ligature(&[2, 0, 1, 0, 3, 0, 0, 0]), Some((1, 2, 3)));
    }
    #[test]
    fn registry_strtol_compatibility() {
        for name in [
            b"Family_14.cpfont".as_slice(),
            b"Family_ +0014.cpfont",
            b"Family_\x0b14.cpfont",
        ] {
            assert_eq!(filename(name), Some(14));
        }
        for name in [
            b"Family_-14.cpfont".as_slice(),
            b"Family_256.cpfont",
            b"Family_14.cpfont.tmp",
            b"_14.cpfont",
            b"Family_14 .cpfont",
        ] {
            assert_eq!(filename(name), None);
        }
    }
}
