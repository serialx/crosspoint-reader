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
/// state is live.
#[no_mangle]
pub unsafe extern "C" fn xp_word_store_chunk_count(state: *const WordStore) -> usize {
    (*state).chunk_count()
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

use xp_core::chapter::style::Block;
use xp_core::para::{
    paragraph::{Metrics, Paragraph, Span},
    paragraph_extract::{self, Options},
};
const _: () = assert!(mem::size_of::<Paragraph>() <= 64 * mem::size_of::<usize>());
const _: () = assert!(mem::align_of::<Paragraph>() <= mem::align_of::<usize>());
unsafe fn para_span<'a>(span: Span) -> &'a [u8] {
    if span.len == 0 {
        &[]
    } else {
        slice::from_raw_parts(span.data, span.len as usize)
    }
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_init(
    p: *mut Paragraph,
    allocator: *const Allocator,
    block: *const Block,
    hyphenation: u8,
    focus: u8,
    indent: u8,
    starts_rtl: unsafe extern "C" fn(Span, i32) -> u8,
) {
    Paragraph::initialize(
        p,
        allocator,
        *block,
        hyphenation != 0,
        focus != 0,
        indent,
        starts_rtl,
    );
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_destroy(p: *mut Paragraph) {
    ptr::drop_in_place(p);
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_add_word(
    p: *mut Paragraph,
    word: Span,
    style: u8,
    underline: u8,
    attach: u8,
    offset: u32,
    link: u8,
    slot: u8,
    no_space: u8,
) {
    (*p).add_word(
        para_span(word),
        style,
        underline != 0,
        attach != 0,
        offset,
        link,
        slot,
        no_space != 0,
    );
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_add_link(p: *mut Paragraph, href: Span) -> u8 {
    (*p).add_link(para_span(href))
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_link_matches(p: *const Paragraph, id: u8, href: Span) -> u8 {
    (*p).link_matches(id, para_span(href)) as u8
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_ruby(p: *const Paragraph, index: u32) -> Span {
    Span::from_bytes((*p).ruby_text(index as usize))
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_set_ruby(p: *mut Paragraph, index: u32, text: Span) {
    (*p).set_ruby(index as usize, para_span(text));
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_set_ruby_group(
    p: *mut Paragraph,
    index: u32,
    count: u32,
    text: Span,
) {
    (*p).set_ruby_group(index as usize, count as usize, para_span(text));
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_size(p: *const Paragraph) -> u32 {
    (*p).tokens.len() as u32
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_dropped(p: *const Paragraph) -> u8 {
    (*p).dropped as u8
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_lines_after(p: *const Paragraph) -> i32 {
    (*p).lines_after
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_word_style(p: *const Paragraph, index: u32) -> u8 {
    (*p).style(index as usize)
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_token(p: *const Paragraph, index: u32, out: *mut Token) {
    out.write((*p).tokens.get(index as usize).unwrap_or_default());
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_get_style(p: *const Paragraph, out: *mut Block) {
    out.write((*p).block);
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_set_style(p: *mut Paragraph, block: *const Block) {
    (*p).block = *block;
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_size_slot(p: *mut Paragraph, scale: f32) -> u8 {
    (*p).size_slot(scale)
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_slot_font(p: *mut Paragraph, slot: u8, font: i32) {
    if let Some(value) = (*p).fonts.get_mut(slot as usize) {
        *value = font;
    }
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_layout(
    p: *mut Paragraph,
    metrics: *const Metrics,
    options: *const Options,
) {
    paragraph_extract::layout(p, &*metrics, &*options);
}

/// # Safety
/// p is live and serialized; result borrows word storage until paragraph mutation.
#[no_mangle]
pub unsafe extern "C" fn xp_para_word(p: *const Paragraph, index: u32) -> Span {
    Span::from_bytes((*p).word(index as usize))
}
/// # Safety
/// p is live and serialized.
#[no_mangle]
pub unsafe extern "C" fn xp_para_indent(p: *const Paragraph) -> u8 {
    (*p).indent
}

/// # Safety
/// Allocator callbacks obey the paragraph allocation/release contract.
#[no_mangle]
pub unsafe extern "C" fn xp_para_allocate(
    allocator: *const Allocator,
    bytes: usize,
    word: u8,
) -> *mut u8 {
    (*allocator).allocate_bytes(bytes, word)
}
