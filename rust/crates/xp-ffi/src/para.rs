use core::{mem, ptr, slice};
use xp_core::para::{Allocator, Word, WordStore};

// The caller owns pointer-aligned state storage and serializes all access. Callbacks
// and their allocator record outlive the store; they must not reenter it.
const _: () = assert!(mem::size_of::<WordStore>() <= 4 * mem::size_of::<usize>());

/// # Safety
/// state points to at least four pointer-sized, pointer-aligned writable words.
/// allocator satisfies WordStore::new; state is uninitialized and disjoint from it.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_init(state: *mut WordStore, allocator: *const Allocator) {
    state.write(WordStore::new(allocator));
}

/// # Safety
/// state is a live store initialized by xp_word_store_init, destroyed exactly once.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_destroy(state: *mut WordStore) {
    ptr::drop_in_place(state);
}

/// # Safety
/// state is live. text borrows len bytes, null only for zero length; len <= isize::MAX.
/// out is one writable aligned Word, disjoint from state and text. Inputs stay valid.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_append(
    state: *mut WordStore,
    text: *const u8,
    len: usize,
    out: *mut Word,
) -> u8 {
    if len > isize::MAX as usize || (len != 0 && text.is_null()) {
        return 0;
    }
    let bytes = if len == 0 {
        &[]
    } else {
        slice::from_raw_parts(text, len)
    };
    match (*state).append(bytes) {
        Some(word) => {
            out.write(word);
            1
        }
        None => 0,
    }
}

/// # Safety
/// state is live. word is a live handle or suffix sharing its release obligation.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_cstr(state: *const WordStore, word: Word) -> *const u8 {
    (*state)
        .view(word)
        .map_or(ptr::null(), |bytes| bytes.as_ptr())
}

/// # Safety
/// state is live; release exactly one of an original handle and its suffix aliases.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_release(state: *mut WordStore, word: Word) {
    (*state).release(word);
}

/// # Safety
/// state is live.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_chunk_count(state: *const WordStore) -> usize {
    (*state).chunk_count()
}

/// # Safety
/// state is live. Returned memory is borrowed until its chunk retires or the store dies.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_chunk_data(
    state: *const WordStore,
    index: usize,
) -> *const u8 {
    (*state).chunk(index).0
}

/// # Safety
/// state is live.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_chunk_used(state: *const WordStore, index: usize) -> usize {
    (*state).chunk(index).1
}
