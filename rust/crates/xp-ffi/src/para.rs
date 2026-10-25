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

use xp_core::para::{Token, Tokens};
const _: () = assert!(mem::size_of::<Tokens>() <= 26 * mem::size_of::<usize>());

/// # Safety
/// state is 26 writable pointer-sized words, pointer-aligned; allocator outlives it.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_init(state: *mut Tokens, allocator: *const Allocator) {
    state.write(Tokens::new(allocator));
}
/// # Safety
/// state is live and destroyed exactly once, with no simultaneous access.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_destroy(state: *mut Tokens) {
    ptr::drop_in_place(state);
}
/// # Safety
/// state is live and externally serialized for every token operation below.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_size(state: *const Tokens) -> usize {
    (*state).len()
}
/// # Safety
/// state is live; out points to one aligned writable Token disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_get(state: *const Tokens, index: usize, out: *mut Token) {
    out.write((*state).get(index).unwrap_or_default());
}
/// # Safety
/// state is live; token is an initialized Token disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_set(state: *mut Tokens, index: usize, token: *const Token) {
    (*state).set(index, token.read());
}
/// # Safety
/// state is live.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_reserve(state: *mut Tokens, required: usize) -> u8 {
    (*state).reserve(required) as u8
}
/// # Safety
/// state is live; token is an initialized Token disjoint from state.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_insert(
    state: *mut Tokens,
    index: usize,
    token: *const Token,
    offset: u32,
) -> u8 {
    (*state).insert(index, token.read(), offset) as u8
}
/// # Safety
/// state is live. Tokens do not own word-text release obligations; caller releases them.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_erase_prefix(state: *mut Tokens, count: usize) {
    (*state).erase_prefix(count);
}
/// # Safety
/// state is live.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_offset(state: *const Tokens, index: usize) -> u32 {
    (*state).offset(index)
}
/// # Safety
/// state is live. Absorbed positions are supplied in source order, as by NFC composition.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_record_absorbed(state: *mut Tokens, offset: u32) -> u8 {
    (*state).record_absorbed(offset) as u8
}
/// # Safety
/// state is live; this updates a cursor so even lookups need exclusive access.
#[no_mangle]
pub unsafe extern "C" fn xp_tokens_source_after(
    state: *mut Tokens,
    start: u32,
    rendered: u32,
) -> u32 {
    (*state).source_after(start, rendered)
}

// Paragraph layout: synchronous metrics and token mutation remain with the caller.
use xp_core::para::layout::{self, Callbacks};

/// # Safety
/// callbacks and allocator meet layout::optimal's contract and are disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_optimal(
    callbacks: *const Callbacks,
    allocator: *const Allocator,
    page: i32,
    indent: i32,
) -> u8 {
    layout::optimal(&*callbacks, allocator, page, indent) as u8
}

/// # Safety
/// callbacks meet layout::greedy's contract; flags must be 0 or 1.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_greedy(
    callbacks: *const Callbacks,
    page: i32,
    indent: i32,
    max_lines: usize,
    first_consumed: u8,
    hyphenation: u8,
) -> u8 {
    layout::greedy(
        &*callbacks,
        page,
        indent,
        max_lines,
        first_consumed != 0,
        hyphenation != 0,
    ) as u8
}

use xp_core::para::layout::Widths;
/// # Safety
/// widths is an exclusive live owner satisfying Widths::reserve's contract.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_widths_reserve(widths: *mut Widths, needed: usize) -> u8 {
    (*widths).reserve(needed) as u8
}
/// # Safety
/// widths satisfies Widths::insert's contract; index <= len.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_widths_insert(
    widths: *mut Widths,
    index: usize,
    value: u16,
) -> u8 {
    (*widths).insert(index, value) as u8
}
/// # Safety
/// widths satisfies Widths::destroy's contract; no outstanding element borrows.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_widths_destroy(widths: *mut Widths) {
    (*widths).destroy();
}
