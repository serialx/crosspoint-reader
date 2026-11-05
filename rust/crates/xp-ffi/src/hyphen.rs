use core::{mem, slice};
use xp_core::hyphen::{self, Break};

/// # Safety
/// tag borrows len readable bytes in one allocation, unchanged during the call.
/// Null is accepted only for zero length; no pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn xp_hyphen_language(tag: *const u8, len: usize) -> i32 {
    if len > isize::MAX as usize || (len != 0 && tag.is_null()) {
        return -1;
    }
    let bytes = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(tag, len)
    };
    hyphen::language(bytes)
}

/// # Safety
/// word borrows len readable bytes. needed points to an aligned writable usize.
/// out points to capacity aligned writable Break entries, or is null for zero
/// capacity. All three regions must be disjoint and remain valid during the call.
/// No pointer is retained. NEED_OUTPUT leaves out untouched.
#[no_mangle]
pub unsafe extern "C" fn xp_hyphen_breaks(
    language: i32,
    word: *const u8,
    len: usize,
    include_fallback: u8,
    out: *mut Break,
    capacity: usize,
    needed: *mut usize,
) -> i32 {
    if !(-1..=10).contains(&language)
        || include_fallback > 1
        || len > u32::MAX as usize
        || len > isize::MAX as usize
        || (len != 0 && word.is_null())
        || needed.is_null()
        || !needed.is_aligned()
        || capacity > isize::MAX as usize / mem::size_of::<Break>()
        || (capacity != 0 && (out.is_null() || !out.is_aligned()))
    {
        return -1;
    }
    let bytes = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(word, len)
    };
    let mut count = 0;
    hyphen::breaks(language, bytes, include_fallback != 0, &mut |_| count += 1);
    needed.write(count);
    if count > capacity {
        return 1;
    }
    if count != 0 {
        let mut index = 0;
        hyphen::breaks(language, bytes, include_fallback != 0, &mut |br| {
            // Count and emission use identical immutable input and explicit language.
            out.add(index).write(br);
            index += 1;
        });
    }
    0
}
