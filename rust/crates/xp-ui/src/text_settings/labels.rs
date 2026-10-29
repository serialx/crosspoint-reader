use core::ffi::c_char;
use xp_core::i18n;
use xp_core::i18n::ids::STR_ALIGNMENT as ALIGNMENT;
use xp_core::i18n::ids::STR_ALIGN_LEFT as ALIGN_LEFT;
use xp_core::i18n::ids::STR_ALIGN_RIGHT as ALIGN_RIGHT;
use xp_core::i18n::ids::STR_BOOK_S_STYLE as BOOK_S_STYLE;
use xp_core::i18n::ids::STR_CENTER as CENTER;
use xp_core::i18n::ids::STR_CHARACTER_SPACING as CHARACTER_SPACING;
use xp_core::i18n::ids::STR_EMBEDDED_STYLE_NO_PREVIEW as EMBEDDED_STYLE_NO_PREVIEW;
use xp_core::i18n::ids::STR_EXTRA_SPACING as EXTRA_SPACING;
use xp_core::i18n::ids::STR_EXTRA_WIDE as EXTRA_WIDE;
use xp_core::i18n::ids::STR_FOCUS_READING as FOCUS_READING;
use xp_core::i18n::ids::STR_FONT as FONT;
use xp_core::i18n::ids::STR_HYPHENATION_NO_PREVIEW as HYPHENATION_NO_PREVIEW;
use xp_core::i18n::ids::STR_JUSTIFY as JUSTIFY;
use xp_core::i18n::ids::STR_LAYOUT as LAYOUT;
use xp_core::i18n::ids::STR_LINE_SPACING as LINE_SPACING;
use xp_core::i18n::ids::STR_NORMAL as NORMAL;
use xp_core::i18n::ids::STR_PARAGRAPH_INDENTATION as PARAGRAPH_INDENTATION;
use xp_core::i18n::ids::STR_SCREEN_MARGIN as SCREEN_MARGIN;
use xp_core::i18n::ids::STR_SELECT as SELECT;
use xp_core::i18n::ids::STR_SIZE as SIZE;
use xp_core::i18n::ids::STR_SPACING_100_PERCENT as SPACING_100_PERCENT;
use xp_core::i18n::ids::STR_SPACING_125_PERCENT as SPACING_125_PERCENT;
use xp_core::i18n::ids::STR_SPACING_150_PERCENT as SPACING_150_PERCENT;
use xp_core::i18n::ids::STR_SPACING_175_PERCENT as SPACING_175_PERCENT;
use xp_core::i18n::ids::STR_SPACING_200_PERCENT as SPACING_200_PERCENT;
use xp_core::i18n::ids::STR_SPACING_50_PERCENT as SPACING_50_PERCENT;
use xp_core::i18n::ids::STR_SPACING_75_PERCENT as SPACING_75_PERCENT;
use xp_core::i18n::ids::STR_SPACING_MINUS_1 as SPACING_MINUS_1;
use xp_core::i18n::ids::STR_SPACING_MINUS_2 as SPACING_MINUS_2;
use xp_core::i18n::ids::STR_SPACING_PLUS_1 as SPACING_PLUS_1;
use xp_core::i18n::ids::STR_SPACING_PLUS_2 as SPACING_PLUS_2;
use xp_core::i18n::ids::STR_SPACING_ZERO as SPACING_ZERO;
use xp_core::i18n::ids::STR_STATE_OFF as STATE_OFF;
use xp_core::i18n::ids::STR_STYLE as STYLE;
use xp_core::i18n::ids::STR_TEXT_AA_NO_PREVIEW as TEXT_AA_NO_PREVIEW;
use xp_core::i18n::ids::STR_TIGHT as TIGHT;
use xp_core::i18n::ids::STR_TOGGLE as TOGGLE;
use xp_core::i18n::ids::STR_WIDE as WIDE;
use xp_core::i18n::ids::STR_WORD_SPACING as WORD_SPACING;

const TABS: [u16; 4] = [FONT, SIZE, LAYOUT, STYLE];
const ROWS: [u16; 7] = [
    LINE_SPACING,
    WORD_SPACING,
    CHARACTER_SPACING,
    EXTRA_SPACING,
    PARAGRAPH_INDENTATION,
    ALIGNMENT,
    SCREEN_MARGIN,
];
const STYLES: [u16; 4] = [
    FOCUS_READING,
    HYPHENATION_NO_PREVIEW,
    EMBEDDED_STYLE_NO_PREVIEW,
    TEXT_AA_NO_PREVIEW,
];
const LINES: [u16; 4] = [TIGHT, NORMAL, WIDE, EXTRA_WIDE];
const WORDS: [u16; 7] = [
    SPACING_50_PERCENT,
    SPACING_75_PERCENT,
    SPACING_100_PERCENT,
    SPACING_125_PERCENT,
    SPACING_150_PERCENT,
    SPACING_175_PERCENT,
    SPACING_200_PERCENT,
];
const CHARS: [u16; 5] = [
    SPACING_MINUS_2,
    SPACING_MINUS_1,
    SPACING_ZERO,
    SPACING_PLUS_1,
    SPACING_PLUS_2,
];
const ALIGNS: [u16; 5] = [JUSTIFY, ALIGN_LEFT, CENTER, ALIGN_RIGHT, BOOK_S_STYLE];
fn number(value: u8, percent: bool, scratch: &mut [u8]) -> *const c_char {
    if scratch.len() < 16 {
        return core::ptr::null();
    }
    let mut n = 0;
    if value >= 100 {
        scratch[n] = b'0' + value / 100;
        n += 1;
    }
    if value >= 10 {
        scratch[n] = b'0' + value / 10 % 10;
        n += 1;
    }
    scratch[n] = b'0' + value % 10;
    n += 1;
    if percent {
        scratch[n] = b'%';
        n += 1;
    }
    scratch[n] = 0;
    scratch.as_ptr().cast()
}
// All translated labels borrow the existing generated Rust i18n tables.
// Named IDs are generated together with the selected translation tables.
pub fn label(language: u8, kind: u8, index: u8, value: u8, scratch: &mut [u8]) -> *const c_char {
    let idx = usize::from(index);
    let v = usize::from(value);
    let id = match kind {
        0 => TABS.get(idx).copied(),
        1 => ROWS.get(idx).copied(),
        2 => STYLES.get(idx).copied(),
        3 => match index {
            0 => Some(*LINES.get(v).unwrap_or(&NORMAL)),
            1 => return number(value, true, scratch),
            2 => Some(*CHARS.get(v).unwrap_or(&SPACING_ZERO)),
            3 => return c"".as_ptr(),
            4 => {
                if value == 0 {
                    Some(STATE_OFF)
                } else {
                    return number(value.min(5), false, scratch);
                }
            }
            5 => Some(*ALIGNS.get(v).unwrap_or(&JUSTIFY)),
            6 => return number(value, false, scratch),
            _ => None,
        },
        4 => match index {
            0 => LINES.get(v).copied(),
            1 => WORDS.get(v).copied(),
            2 => CHARS.get(v).copied(),
            4 => {
                if value == 0 {
                    Some(STATE_OFF)
                } else if value <= 5 {
                    return number(value, false, scratch);
                } else {
                    None
                }
            }
            5 => ALIGNS.get(v).copied(),
            6 => {
                if value < 8 {
                    return number(5 + value * 5, false, scratch);
                } else {
                    None
                }
            }
            _ => None,
        },
        5 => {
            if value == 0 {
                TABS.get((idx + 1) % 4).copied()
            } else {
                Some(if index == 3 || index == 2 && value == 4 {
                    TOGGLE
                } else {
                    SELECT
                })
            }
        }
        _ => None,
    };
    id.map_or(core::ptr::null(), |id| i18n::get(language, id))
}
