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

use xp_core::para::layout_split::{self, SplitRequest, SplitResult};
/// # Safety
/// All pointers meet layout_split::split's exclusive/disjoint owner contract.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_split(
    store: *mut WordStore,
    tokens: *mut Tokens,
    widths: *mut Widths,
    request: *const SplitRequest,
    result: *mut SplitResult,
) -> u8 {
    layout_split::split(
        &mut *store,
        &mut *tokens,
        &mut *widths,
        &*request,
        &mut *result,
    ) as u8
}

use xp_core::para::layout_line::{
    self, Callbacks as LineCallbacks, Metrics as LineMetrics, Spec as LineSpec,
};
/// # Safety
/// Inputs meet layout_line::metrics's contract; out is writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_line_metrics(
    spec: *const LineSpec,
    callbacks: *const LineCallbacks,
    out: *mut LineMetrics,
) {
    out.write(layout_line::metrics(&*spec, &*callbacks));
}
/// # Safety
/// Inputs and output meet layout_line::position's contract.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_line_position(
    spec: *const LineSpec,
    callbacks: *const LineCallbacks,
    metrics: *const LineMetrics,
    out: *mut i16,
) {
    layout_line::position(&*spec, &*callbacks, &*metrics, out);
}

use xp_core::para::layout_extract::{self, Line};
/// # Safety
/// line satisfies Line::init's exclusive ownership contract; focus is 0 or 1.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_line_init(line: *mut Line, count: usize, focus: u8) -> u8 {
    (*line).init(count, focus != 0) as u8
}
/// # Safety
/// line satisfies Line::destroy's contract.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_line_destroy(line: *mut Line) {
    (*line).destroy();
}
/// # Safety
/// Inputs and outputs meet layout_extract::sources's disjoint borrowing contract.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_line_sources(
    line: *mut Line,
    store: *const WordStore,
    tokens: *mut Tokens,
    start: usize,
    order: *const u16,
    paragraph_emitted: u8,
    paragraph_start: *mut u16,
) -> u8 {
    layout_extract::sources(
        &mut *line,
        &*store,
        &mut *tokens,
        start,
        order,
        paragraph_emitted != 0,
        &mut *paragraph_start,
    ) as u8
}
/// # Safety
/// data borrows len writable initialized bytes exclusively; null only if len=0.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_strip_soft_hyphens(data: *mut u8, len: usize) -> usize {
    layout_extract::strip_soft_hyphens(data, len)
}

use xp_core::para::layout::Breaks;
/// # Safety
/// breaks is an exclusive zero-initialized owner with a live allocator, or a live
/// prior owner. Like Widths::insert, failure preserves values and latches failed.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_breaks_push(breaks: *mut Breaks, value: usize) -> u8 {
    (*breaks).insert((*breaks).len, value) as u8
}
/// # Safety
/// breaks is a live owner; no outstanding array borrows.
#[no_mangle]
pub unsafe extern "C" fn xp_layout_breaks_destroy(breaks: *mut Breaks) {
    (*breaks).destroy();
}

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
pub unsafe extern "C" fn xp_para_suppress_indent(p: *mut Paragraph) {
    (*p).first_consumed = true;
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_reset_indent(p: *mut Paragraph) {
    (*p).first_consumed = false;
    (*p).paragraph_emitted = false;
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
pub unsafe extern "C" fn xp_para_slot_count(p: *const Paragraph) -> u8 {
    (*p).slots
}

/// # Safety
/// Paragraph storage is aligned and live (uninitialized for init), exclusively
/// accessed. Spans/pointers borrow valid disjoint storage for the call; callbacks
/// follow the paragraph metrics/sink contract in xp_ffi.h.
#[no_mangle]
pub unsafe extern "C" fn xp_para_slot_scale(p: *const Paragraph, slot: u8) -> f32 {
    (*p).scales.get(slot as usize).copied().unwrap_or(1.0)
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
/// state is the current synchronous semantic reborrow. Paragraph is null or a live
/// disjoint owner; assembly must rebind before its destruction or replacement.
#[no_mangle]
pub unsafe extern "C" fn xp_para_bind_chapter(
    state: *mut xp_core::chapter::State,
    paragraph: *mut Paragraph,
) {
    (*state).set_paragraph(paragraph);
}
