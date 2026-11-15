//! Incremental character decoding; widths always refer to original input bytes.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum Encoding {
    Utf8,
    Utf16Le,
    Utf16Be,
    Latin1,
    Ascii,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum Decoded {
    Character(char, usize),
    Incomplete,
    Invalid,
}

impl Encoding {
    pub(super) fn decode(self, input: &[u8]) -> Decoded {
        use Decoded::*;
        let Some(&first) = input.first() else {
            return Incomplete;
        };
        match self {
            Self::Latin1 => Character(char::from(first), 1),
            Self::Ascii => {
                if first < 0x80 {
                    Character(char::from(first), 1)
                } else {
                    Invalid
                }
            }
            Self::Utf16Le | Self::Utf16Be => {
                let Some(pair) = input.get(..2).and_then(|s| <&[u8; 2]>::try_from(s).ok()) else {
                    return Incomplete;
                };
                let unit = |a, b| {
                    if self == Self::Utf16Le {
                        u16::from_le_bytes([a, b])
                    } else {
                        u16::from_be_bytes([a, b])
                    }
                };
                let a = unit(pair[0], pair[1]);
                if (0xd800..=0xdbff).contains(&a) {
                    let Some(pair) = input.get(2..4).and_then(|s| <&[u8; 2]>::try_from(s).ok())
                    else {
                        return Incomplete;
                    };
                    let b = unit(pair[0], pair[1]);
                    if !(0xdc00..=0xdfff).contains(&b) {
                        return Invalid;
                    }
                    let cp = 0x10000 + ((u32::from(a) - 0xd800) << 10) + u32::from(b) - 0xdc00;
                    match char::from_u32(cp) {
                        Some(c) => Character(c, 4),
                        None => Invalid,
                    }
                } else {
                    match char::from_u32(u32::from(a)) {
                        Some(c) => Character(c, 2),
                        None => Invalid,
                    }
                }
            }
            Self::Utf8 => {
                let width = match first {
                    0..=0x7f => return Character(char::from(first), 1),
                    0xc2..=0xdf => 2,
                    0xe0..=0xef => 3,
                    0xf0..=0xf4 => 4,
                    _ => return Invalid,
                };
                // Validate available continuation bytes before requesting more input.
                for (index, &byte) in input.iter().take(width).enumerate().skip(1) {
                    if !(0x80..=0xbf).contains(&byte)
                        || (index == 1
                            && ((first == 0xe0 && byte < 0xa0)
                                || (first == 0xed && byte >= 0xa0)
                                || (first == 0xf0 && byte < 0x90)
                                || (first == 0xf4 && byte >= 0x90)))
                    {
                        return Invalid;
                    }
                }
                let Some(scalar) = input.get(..width) else {
                    return Incomplete;
                };
                match core::str::from_utf8(scalar)
                    .ok()
                    .and_then(|s| s.chars().next())
                {
                    Some(c) => Character(c, width),
                    None => Invalid,
                }
            }
        }
    }

    pub(super) fn original_width(self, value: &str) -> usize {
        match self {
            Self::Utf8 | Self::Ascii => value.len(),
            Self::Latin1 => value.chars().count(),
            Self::Utf16Le | Self::Utf16Be => value.chars().map(|c| c.len_utf16() * 2).sum(),
        }
    }
}

pub(super) fn xml_character(c: char) -> bool {
    matches!(c as u32, 9 | 10 | 13 | 0x20..=0xd7ff | 0xe000..=0xfffd | 0x10000..=0x10ffff)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn original_width_and_partial_characters() {
        assert_eq!(Encoding::Utf8.decode(&[0xf0, 0x9f]), Decoded::Incomplete);
        assert_eq!(Encoding::Utf8.decode(&[0xf0, 0x28]), Decoded::Invalid);
        assert_eq!(Encoding::Utf16Le.decode(&[0x3d, 0xd8]), Decoded::Incomplete);
        assert_eq!(
            Encoding::Utf16Le.decode(&[0x3d, 0xd8, 0x42, 0xde]),
            Decoded::Character('🙂', 4)
        );
        assert_eq!(
            Encoding::Utf16Be.decode(&[0xd8, 0x3d, 0xde, 0x42]),
            Decoded::Character('🙂', 4)
        );
        assert_eq!(Encoding::Latin1.decode(&[0xe9]), Decoded::Character('é', 1));
        assert_eq!(Encoding::Utf16Le.original_width("a🙂é"), 8);
        assert_eq!(Encoding::Latin1.original_width("aé"), 2);
    }
}
