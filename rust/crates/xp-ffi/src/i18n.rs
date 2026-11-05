use core::ffi::{c_char, CStr};
use xp_core::i18n;

#[no_mangle]
pub extern "C" fn xp_i18n_get(language: u8, id: u16) -> *const c_char {
    i18n::get(language, id)
}

#[no_mangle]
pub extern "C" fn xp_i18n_select_language(current: u8, requested: u8) -> u8 {
    i18n::select_language(current, requested)
}

#[no_mangle]
pub extern "C" fn xp_i18n_language_name(language: u8) -> *const c_char {
    i18n::language_name(language)
}

#[no_mangle]
pub extern "C" fn xp_i18n_character_set(language: u8) -> *const c_char {
    i18n::character_set(language)
}

/// # Safety
/// Non-null code must point to a readable NUL-terminated string unchanged for
/// this call. The string is borrowed and not retained.
#[no_mangle]
pub unsafe extern "C" fn xp_i18n_language_from_code(code: *const c_char) -> u8 {
    if code.is_null() {
        return 0;
    }
    i18n::language_from_code(CStr::from_ptr(code).to_bytes())
}
