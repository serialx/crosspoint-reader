//! Section cache wire format, version 68.
//!
//! Integers are little-endian, without padding; booleans occupy one byte.
//! Floating-point fields carry IEEE-754 binary32 bits. Strings are a u32 byte
//! length followed by exactly that many bytes, with no implicit terminator.
//!
//! The 44-byte header is, in order: version:u8, fontId:i32,
//! lineCompression:f32, extraParagraphSpacing:u8, paragraphIndentSpaces:u8,
//! paragraphAlignment:u8, viewportWidth:u16, viewportHeight:u16,
//! hyphenationEnabled:u8, embeddedStyle:u8, imageRendering:u8,
//! focusReadingEnabled:u8, characterSpacing:i8, wordSpacingPercent:u8,
//! pageCount:u16, pageLutOffset:u32, anchorMapOffset:u32,
//! paragraphLutOffset:u32, listItemLutOffset:u32, visibleTextLutOffset:u32.
//! Page count starts at byte 22; the five table offsets start at byte 24.
//!
//! Version 0 denotes an uncommitted build. Final commit patches the count and
//! offsets before stamping version 68. A suspended build stamps
//! `0xfe - (68 - 28) = 214`, invalidating partial caches alongside final caches.
//! It appends bytesConsumed:u32 and totalBytes:u32 after the visible-offset LUT.
//! Version changes can invalidate pagination without changing the binary layout.
//!
//! Each page begins with elementCount:u16. Every element starts with tag:u8:
//! - 1, line: x:i16, y:i16, then a TextBlock.
//! - 2, image: x:i16, y:i16, imagePath:String, srcPath:String, width:i16,
//!   height:i16.
//! - 3, horizontal rule: x:i16, y:i16, width:u16, thickness:u8.
//! - 4, drop cap: x:i16, y:i16, fontId:i32, scale256:u16, style:u8,
//!   length:u8, then length bytes (1..=12). Y is the final spanned baseline.
//! - 5, border box: x:i16, y:i16, width:u16, height:u16, four pairs of
//!   width:u8/style:u8 in top/right/bottom/left order, then shaded:u8.
//!
//! A TextBlock starts with wordCount:u16, hasFocus:u8, textBytes:u16,
//! paragraphStartWord:u16 (65535 means continuation). For nonzero wordCount,
//! its arena follows: wordCount pairs of start:u32/end:u32 source ranges,
//! wordCount text offsets:u16, wordCount x positions:i16, optional wordCount
//! focus suffix positions:u16, wordCount style bytes, optional wordCount
//! focus-boundary bytes, and textBytes bytes of NUL-terminated word text.
//! Both optional arrays exist when hasFocus is nonzero. Arena length is
//! wordCount * (13 + (hasFocus ? 3 : 0)) + textBytes. The word style's high bit
//! marks a discretionary hyphen; source ranges count visible Unicode codepoints.
//! Next come wordCount ruby Strings (including empty strings), then BlockStyle:
//! alignment:u8, textAlignDefined:u8, marginTop:i16, marginBottom:i16,
//! marginLeft:i16, marginRight:i16, paddingTop:i16, paddingBottom:i16,
//! paddingLeft:i16, paddingRight:i16, textIndent:i16, textIndentDefined:u8,
//! isRtl:u8, directionDefined:u8, characterSpacing:i8, fontId:i32.
//! Finally hasWordFonts:u8 is followed, when set, by three font ids:i32 and
//! wordCount slot bytes. The low seven bits select 0 (block font) or 1..=3;
//! bit 0x80 marks small caps. Layout-only style/link-span fields are absent.
//!
//! After all elements, footnoteCount:u16 precedes up to 16 fixed records of
//! number:[u8;32], href:[u8;256]. Then linkCount:u16 precedes up to 32 records:
//! href:[u8;256], x:i16, y:i16, width:i16, height:i16. Loaded fixed strings are
//! terminated in their last byte. Page visibleTextOffset is not in this body.
//!
//! After page bodies, the page LUT contains pageCount absolute u32 file offsets.
//! A page's range starts at its LUT entry and ends at the next entry, or the
//! page-LUT offset for the final page. The original C++ reader seeks to the
//! start and decodes counts without enforcing that upper boundary. The anchor
//! map is count:u16 followed by count pairs of anchor:String and page:u16.
//! The paragraph LUT is count:u16 followed by count paragraph indices:u16.
//! The list-item LUT has count indices:u16 and shares the paragraph count.
//! The visible-offset LUT has pageCount codepoint offsets:u32. Partial commits
//! omit anchors on the unfinished page. The watermark trailer follows this LUT,
//! not the list-item LUT as older source comments stated.

/// Wire record discriminants shared with the C adapter. Variable payloads are
/// borrowed separately so decoding can use the object's existing arena.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Kind {
    U8 = 0,
    U16,
    U32,
    Header,
    TextHeader,
    BlockStyle,
    Position,
    ImageSize,
    Rule,
    DropCap,
    Border,
    ElementTag,
    FootnoteCount,
    LinkCount,
    LinkGeometry,
    WordFontFlag,
}

impl Kind {
    pub fn from_raw(value: u32) -> Option<Self> {
        Some(match value {
            0 => Self::U8,
            1 => Self::U16,
            2 => Self::U32,
            3 => Self::Header,
            4 => Self::TextHeader,
            5 => Self::BlockStyle,
            6 => Self::Position,
            7 => Self::ImageSize,
            8 => Self::Rule,
            9 => Self::DropCap,
            10 => Self::Border,
            11 => Self::ElementTag,
            12 => Self::FootnoteCount,
            13 => Self::LinkCount,
            14 => Self::LinkGeometry,
            15 => Self::WordFontFlag,
            _ => return None,
        })
    }

    fn widths(self) -> &'static [u8] {
        match self {
            Self::U8 | Self::ElementTag | Self::WordFontFlag => &[1],
            Self::U16 | Self::FootnoteCount | Self::LinkCount => &[2],
            Self::U32 => &[4],
            Self::Header => &[1, 4, 4, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 4, 4, 4, 4, 4],
            Self::TextHeader => &[2, 1, 2, 2],
            Self::BlockStyle => &[1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 4],
            Self::Position | Self::ImageSize => &[2, 2],
            Self::Rule => &[2, 2, 2, 1],
            Self::DropCap => &[2, 2, 4, 2, 1, 1],
            Self::Border => &[2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1],
            Self::LinkGeometry => &[2, 2, 2, 2],
        }
    }

    pub fn size(self) -> usize {
        self.widths().iter().map(|&n| usize::from(n)).sum()
    }
}

pub const VERSION: u8 = 68;
pub const PARTIAL_VERSION: u8 = 0xfe - (VERSION - 28);
pub const HEADER_SIZE: usize = 44;
pub const FIELD_COUNT: usize = 20;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(C)]
pub struct Record {
    pub kind: u32,
    pub values: [u32; FIELD_COUNT],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    NeedInput(usize),
    NeedOutput(usize),
}

/// Encode atomically: insufficient output never changes its bytes.
pub fn encode(record: &Record, output: &mut [u8]) -> Result<usize, Error> {
    let kind = Kind::from_raw(record.kind).ok_or(Error::Invalid)?;
    let size = kind.size();
    if output.len() < size {
        return Err(Error::NeedOutput(size));
    }
    let mut pos = 0;
    for (&width, value) in kind.widths().iter().zip(&record.values) {
        let width = usize::from(width);
        output[pos..pos + width].copy_from_slice(&value.to_le_bytes()[..width]);
        pos += width;
    }
    Ok(size)
}

pub fn decode(kind: Kind, input: &[u8]) -> Result<(Record, usize), Error> {
    let size = kind.size();
    if input.len() < size {
        return Err(Error::NeedInput(size));
    }
    let mut record = Record {
        kind: kind as u32,
        ..Record::default()
    };
    let mut pos = 0;
    for (&width, value) in kind.widths().iter().zip(&mut record.values) {
        let width = usize::from(width);
        let mut bytes = [0; 4];
        bytes[..width].copy_from_slice(&input[pos..pos + width]);
        *value = u32::from_le_bytes(bytes);
        pos += width;
    }
    validate(&record)?;
    Ok((record, size))
}

fn validate(record: &Record) -> Result<(), Error> {
    let v = &record.values;
    let valid = match Kind::from_raw(record.kind).ok_or(Error::Invalid)? {
        Kind::Header => {
            matches!(v[0], n if n == u32::from(VERSION) || n == u32::from(PARTIAL_VERSION))
        }
        Kind::TextHeader => {
            v[0] <= 10000
                && (v[3] == 65535 || v[3] < v[0])
                && if v[0] == 0 { v[2] == 0 } else { v[2] >= v[0] }
        }
        Kind::Rule => v[2] != 0 && v[3] != 0,
        Kind::DropCap => v[3] != 0 && (1..=12).contains(&v[5]),
        Kind::Border => v[2] != 0 && v[3] != 0 && [5, 7, 9, 11].iter().all(|&i| v[i] <= 4),
        Kind::ElementTag => (1..=5).contains(&v[0]),
        Kind::WordFontFlag => v[0] <= 1,
        Kind::FootnoteCount => v[0] <= 16,
        Kind::LinkCount => v[0] <= 32,
        Kind::LinkGeometry => v[2] as i16 > 0 && v[3] as i16 > 0,
        Kind::U8 | Kind::U16 | Kind::U32 | Kind::BlockStyle | Kind::Position | Kind::ImageSize => {
            true
        }
    };
    if valid {
        Ok(())
    } else {
        Err(Error::Invalid)
    }
}

pub fn arena_size(words: u16, focus: bool, text_bytes: u16) -> usize {
    if words == 0 {
        0
    } else {
        usize::from(words) * (13 + if focus { 3 } else { 0 }) + usize::from(text_bytes)
    }
}

pub fn validate_arena(words: u16, focus: bool, text_bytes: u16, data: &[u8]) -> bool {
    let n = usize::from(words);
    let text_len = usize::from(text_bytes);
    if data.len() != arena_size(words, focus, text_bytes) {
        return false;
    }
    if n == 0 {
        return text_len == 0;
    }
    if text_len < n {
        return false;
    }
    let offsets = &data[n * 8..n * 10];
    let text = &data[data.len() - text_len..];
    if offsets[..2] != [0, 0] || text[text_len - 1] != 0 {
        return false;
    }
    let mut previous = 0;
    for i in 1..n {
        let offset = usize::from(u16::from_le_bytes([offsets[i * 2], offsets[i * 2 + 1]]));
        if offset <= previous || offset >= text_len || text[offset - 1] != 0 {
            return false;
        }
        previous = offset;
    }
    true
}

pub fn validate_fonts(words: u16, data: &[u8]) -> bool {
    if data.len() != 12 + usize::from(words) {
        return false;
    }
    data[12..].iter().all(|&slot| {
        let slot = usize::from(slot & 0x7f);
        slot <= 3 && (slot == 0 || data[(slot - 1) * 4..slot * 4] != [0, 0, 0, 0])
    })
}

pub fn header_matches(header: &Record, expected: &Record) -> bool {
    header.kind == Kind::Header as u32
        && expected.kind == Kind::Header as u32
        && header.values[1] == expected.values[1]
        && f32::from_bits(header.values[2]) == f32::from_bits(expected.values[2])
        && header.values[3..14] == expected.values[3..14]
}

pub fn table_entry(offset: u32, index: u32, width: u32, size: u32, minimum: u32) -> Option<u32> {
    let position = offset.checked_add(index.checked_mul(width)?)?;
    if offset < minimum || position.checked_add(width)? > size {
        None
    } else {
        Some(position)
    }
}

pub fn partial_trailer(header: &Record, size: u32) -> Option<u32> {
    let v = &header.values;
    if v[0] != u32::from(PARTIAL_VERSION)
        || v[14] == 0
        || v[18] < HEADER_SIZE as u32
        || v[19] <= v[18]
    {
        return None;
    }
    let offset = v[19].checked_add(v[14].checked_mul(4)?)?;
    if offset.checked_add(8)? <= size {
        Some(offset)
    } else {
        None
    }
}

/// Append one NUL-free word to the section text result. Empty leading words
/// preserve the original behavior: the separator depends on existing bytes.
pub fn encode_text_word(previous: usize, word: &[u8], output: &mut [u8]) -> Result<usize, Error> {
    let separator = usize::from(previous != 0);
    let size = word.len().checked_add(separator).ok_or(Error::Invalid)?;
    if output.len() < size {
        return Err(Error::NeedOutput(size));
    }
    if separator != 0 {
        output[0] = b' ';
    }
    output[separator..size].copy_from_slice(word);
    Ok(size)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_record_widths_round_trip_without_padding() {
        for raw in 0..=15 {
            let kind = Kind::from_raw(raw).unwrap();
            let mut record = Record {
                kind: raw,
                values: [1; FIELD_COUNT],
            };
            if kind == Kind::Header {
                record.values[0] = u32::from(VERSION);
            }
            if kind == Kind::TextHeader {
                record.values[3] = 0;
            }
            let mut bytes = [0xa5; 64];
            let size = kind.size();
            assert_eq!(
                encode(&record, &mut bytes[..size - 1]),
                Err(Error::NeedOutput(size))
            );
            assert_eq!(bytes, [0xa5; 64]);
            assert_eq!(encode(&record, &mut bytes), Ok(size));
            let (decoded, used) = decode(kind, &bytes[..size]).unwrap();
            assert_eq!(used, size);
            for (index, _) in kind.widths().iter().enumerate() {
                assert_eq!(decoded.values[index], record.values[index]);
            }
            for length in 0..size {
                assert_eq!(decode(kind, &bytes[..length]), Err(Error::NeedInput(size)));
            }
        }
    }
    #[test]
    fn invalid_arena_offsets_and_fonts_are_rejected() {
        let mut arena = [0u8; 30]; // two words, no focus, four text bytes
        arena[18] = 2;
        arena[26..].copy_from_slice(b"a\0b\0");
        assert!(validate_arena(2, false, 4, &arena));
        arena[18] = 0;
        assert!(!validate_arena(2, false, 4, &arena));
        assert!(!validate_arena(2, true, 4, &arena));
        let mut fonts = [0; 14];
        assert!(validate_fonts(2, &fonts));
        fonts[12] = 0x81;
        assert!(!validate_fonts(2, &fonts));
        fonts[0] = 16;
        assert!(validate_fonts(2, &fonts));
        fonts[13] = 4;
        assert!(!validate_fonts(2, &fonts));
    }
    #[test]
    fn table_arithmetic_never_wraps() {
        assert_eq!(table_entry(44, 2, 4, 56, 44), Some(52));
        assert_eq!(table_entry(44, 3, 4, 56, 44), None);
        assert_eq!(table_entry(u32::MAX, 1, 4, u32::MAX, 44), None);
        assert_eq!(table_entry(44, u32::MAX, 4, u32::MAX, 44), None);
    }
    #[test]
    fn partial_header_and_watermark_bounds() {
        let mut header = Record {
            kind: Kind::Header as u32,
            values: [0; FIELD_COUNT],
        };
        header.values[0] = u32::from(PARTIAL_VERSION);
        header.values[14] = 2;
        header.values[18] = 100;
        header.values[19] = 104;
        assert_eq!(PARTIAL_VERSION, 214);
        assert_eq!(partial_trailer(&header, 120), Some(112));
        assert_eq!(partial_trailer(&header, 119), None);
        header.values[19] = u32::MAX;
        assert_eq!(partial_trailer(&header, u32::MAX), None);
    }

    #[test]
    fn drop_cap_bounds_and_text_separators() {
        let mut cap = Record {
            kind: Kind::DropCap as u32,
            values: [0; FIELD_COUNT],
        };
        cap.values[3] = 256;
        cap.values[5] = 12;
        assert_eq!(validate(&cap), Ok(()));
        cap.values[5] = 13;
        assert_eq!(validate(&cap), Err(Error::Invalid));
        let mut output = [0xa5; 8];
        assert_eq!(encode_text_word(0, b"", &mut output), Ok(0));
        assert_eq!(
            encode_text_word(3, b"word", &mut output[..4]),
            Err(Error::NeedOutput(5))
        );
        assert_eq!(output, [0xa5; 8]);
        assert_eq!(encode_text_word(3, b"word", &mut output), Ok(5));
        assert_eq!(&output[..5], b" word");
    }
}
