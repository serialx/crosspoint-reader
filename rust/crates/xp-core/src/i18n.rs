//! Immutable translations generated from the firmware YAML sources.
use core::ffi::{c_char, CStr};

#[repr(transparent)]
pub struct CStrPtr(*const c_char);

// Every pointer is generated from a static, NUL-terminated byte literal.
unsafe impl Sync for CStrPtr {}

impl CStrPtr {
    fn bytes(&self) -> &'static [u8] {
        // The private field can only be populated by the generated tables.
        unsafe { CStr::from_ptr(self.0).to_bytes() }
    }
}

include!(concat!(env!("OUT_DIR"), "/i18n_generated.rs"));

const UNKNOWN: &[u8] = b"???\0";

pub fn get(language: u8, id: u16) -> *const c_char {
    let (data, offsets) = language_strings(language);
    let Some(&offset) = offsets.get(usize::from(id)) else {
        return UNKNOWN.as_ptr().cast();
    };
    let (data, offset) = if offset & 0x8000 != 0 {
        (STRINGS_EN, offset & 0x7fff)
    } else {
        (data, offset)
    };
    data.get(usize::from(offset)..)
        .unwrap_or(UNKNOWN)
        .as_ptr()
        .cast()
}

pub fn select_language(current: u8, requested: u8) -> u8 {
    if requested < LANGUAGE_COUNT {
        requested
    } else {
        current
    }
}

pub fn language_name(language: u8) -> *const c_char {
    LANGUAGE_NAMES
        .get(usize::from(language))
        .map_or(UNKNOWN.as_ptr().cast(), |v| v.0)
}

pub fn character_set(language: u8) -> *const c_char {
    CHARACTER_SETS
        .get(usize::from(language))
        .unwrap_or(&CHARACTER_SETS[0])
        .0
}

pub fn language_from_code(code: &[u8]) -> u8 {
    LANGUAGE_CODES
        .iter()
        .position(|value| value.bytes() == code)
        .unwrap_or(0) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn invalid_values_and_codes() {
        assert_eq!(get(0, u16::MAX), UNKNOWN.as_ptr().cast());
        assert_eq!(get(u8::MAX, 0), get(0, 0));
        assert_eq!(language_name(u8::MAX), UNKNOWN.as_ptr().cast());
        assert_eq!(character_set(u8::MAX), character_set(0));
        assert_eq!(select_language(3, u8::MAX), 3);
        assert_eq!(select_language(3, 0), 0);
        assert_eq!(language_from_code(b"en"), 0);
        assert_eq!(language_from_code(b"invalid"), 0);
        for (index, value) in LANGUAGE_CODES.iter().enumerate() {
            assert_eq!(language_from_code(value.bytes()), index as u8);
        }
    }
}
