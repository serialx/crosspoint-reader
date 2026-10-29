//! CSS handles are exclusively owned by C++; all slice pointers are borrowed per call.
extern crate alloc;
use alloc::{alloc::alloc, boxed::Box, vec::Vec};
use core::{alloc::Layout, ptr};
use xp_core::css::{
    self, Diagnostic, Diagnostics, Error, Store, Stream, CACHE_BUFFER_BYTES, CACHE_VERSION,
    MAX_CACHE_KEY_BYTES, WIRE_BYTES,
};

pub struct Css {
    pub(crate) store: Store,
    stream: Option<Box<Stream>>,
    cache: Option<Box<CacheReader>>,
    write_offset: usize,
    write_header: bool,
    complete: bool,
    feed_failed: bool,
}
#[repr(C)]
#[derive(Default)]
pub struct CacheDiagnostic {
    event: u32,
    a: u32,
    b: u32,
}
extern "C" fn record_cache_diagnostic(
    ctx: *mut core::ffi::c_void,
    event: u32,
    a: u32,
    b: u32,
    _: u32,
    _: u32,
) {
    // Installed only with a writable, per-call CacheDiagnostic output.
    unsafe {
        ctx.cast::<CacheDiagnostic>()
            .write(CacheDiagnostic { event, a, b });
    }
}
struct CacheReader {
    bytes: Vec<u8>,
    header: bool,
    expected: usize,
    read: usize,
}
fn boxed<T>(value: T) -> Option<Box<T>> {
    // The Box takes ownership only after allocation succeeds, and uses the same allocator.
    let p = unsafe { alloc(Layout::new::<T>()).cast::<T>() };
    if p.is_null() {
        return None;
    }
    unsafe {
        p.write(value);
        Some(Box::from_raw(p))
    }
}
fn error(e: Error) -> i32 {
    match e {
        Error::Limit | Error::Invalid => 2,
        Error::OutOfMemory => 1,
    }
}
unsafe fn bytes<'a>(p: *const u8, n: usize) -> &'a [u8] {
    if n == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(p, n)
    }
}
#[repr(C)]
pub struct Slice {
    data: *const u8,
    len: usize,
}
impl Slice {
    unsafe fn bytes(&self) -> &[u8] {
        bytes(self.data, self.len)
    }
}

#[no_mangle]
pub extern "C" fn xp_css_create() -> *mut Css {
    boxed(Css {
        store: Store::default(),
        stream: None,
        cache: None,
        write_offset: 0,
        write_header: true,
        complete: true,
        feed_failed: false,
    })
    .map_or(ptr::null_mut(), Box::into_raw)
}
/// # Safety
/// Handle is null or an exclusively owned live result of xp_css_create; destroy once.
#[no_mangle]
pub unsafe extern "C" fn xp_css_destroy(handle: *mut Css) {
    if !handle.is_null() {
        drop(Box::from_raw(handle));
    }
}
/// # Safety
/// Handle must be exclusively borrowed and live.
#[no_mangle]
pub unsafe extern "C" fn xp_css_clear(handle: *mut Css) {
    let h = &mut *handle;
    h.store.clear();
    h.stream = None;
    h.cache = None;
    h.write_offset = 0;
    h.write_header = true;
    h.feed_failed = false;
}
/// # Safety
/// Handle is live and exclusive. Input points to len readable bytes, or null with zero len.
/// end marks the stylesheet boundary; 0/1/2 are Complete/Partial/Error.
#[no_mangle]
pub unsafe extern "C" fn xp_css_feed(handle: *mut Css, data: *const u8, len: usize, end: u8) -> u8 {
    xp_css_feed_with_diagnostics(handle, data, len, end, ptr::null())
}
/// # Safety
/// Same as xp_css_feed; diagnostics is null or borrowed for this call only.
/// Its callback must not reenter Rust or retain borrowed pointers.
#[no_mangle]
pub unsafe extern "C" fn xp_css_feed_with_diagnostics(
    handle: *mut Css,
    data: *const u8,
    len: usize,
    end: u8,
    diagnostics: *const Diagnostics,
) -> u8 {
    let diagnostics = diagnostics.as_ref().copied().unwrap_or_default();
    let h = &mut *handle;
    if h.feed_failed {
        if end != 0 {
            h.feed_failed = false;
        }
        return css::ParseResult::Partial as u8;
    }
    if h.stream.is_none() {
        let Some(mut stream) = boxed(Stream::default()) else {
            diagnostics.emit(Diagnostic::ScratchOom, 0, 0);
            h.feed_failed = end == 0;
            h.store.stop_growth();
            return css::ParseResult::Partial as u8;
        };
        if stream.prepare().is_err() {
            diagnostics.emit(Diagnostic::ScratchOom, 0, 0);
            h.feed_failed = end == 0;
            h.store.stop_growth();
            return css::ParseResult::Partial as u8;
        }
        h.stream = Some(stream);
    }
    let stream = h.stream.as_mut().unwrap();
    stream.feed_with_diagnostics(&mut h.store, bytes(data, len), &diagnostics);
    if end != 0 {
        let result = stream.finish_with_diagnostics(&mut h.store, &diagnostics);
        h.stream = None;
        result as u8
    } else {
        0
    }
}
/// # Safety
/// Handle and all slices are borrowed for the call. Ancestors point to count valid records.
/// out points to WIRE_BYTES writable bytes, disjoint from all inputs and the handle.
#[no_mangle]
pub unsafe extern "C" fn xp_css_resolve(
    handle: *const Css,
    tag: Slice,
    classes: Slice,
    id: Slice,
    ancestors: *const css::Ancestor,
    count: usize,
    first: u8,
    out: *mut u8,
) {
    let ancestors = if count == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ancestors, count)
    };
    let style = (&*handle).store.resolve(
        tag.bytes(),
        classes.bytes(),
        id.bytes(),
        ancestors,
        first != 0,
    );
    ptr::copy_nonoverlapping(style.0.as_ptr(), out, WIRE_BYTES);
}
/// # Safety
/// The input slice is readable and output has WIRE_BYTES writable, disjoint bytes.
#[no_mangle]
pub unsafe extern "C" fn xp_css_inline(data: *const u8, len: usize, out: *mut u8) {
    let s = css::inline(bytes(data, len));
    ptr::copy_nonoverlapping(s.0.as_ptr(), out, WIRE_BYTES);
}
/// # Safety
/// Slices are readable and out points to a writable Ancestor disjoint from them.
#[no_mangle]
pub unsafe extern "C" fn xp_css_ancestor(
    tag: Slice,
    classes: Slice,
    id: Slice,
    out: *mut css::Ancestor,
) {
    out.write(css::ancestor(tag.bytes(), classes.bytes(), id.bytes()));
}
/// # Safety
/// Handle is live and borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_css_rule_count(handle: *const Css) -> usize {
    (&*handle).store.rule_count()
}
/// # Safety
/// Handle is live and borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_css_has_first_letter(handle: *const Css) -> u8 {
    u8::from((&*handle).store.has_first_letter())
}
/// # Safety
/// Handle is live/exclusive. order has count writable u16s, disjoint from the handle.
/// count equals rule_count; no store mutation until serialization ends.
#[no_mangle]
pub unsafe extern "C" fn xp_css_cache_write_begin(
    handle: *mut Css,
    complete: u8,
    order: *mut u16,
    count: usize,
) -> i32 {
    let h = &mut *handle;
    h.write_header = true;
    h.write_offset = 0;
    h.complete = complete != 0;
    let order = if count == 0 {
        &mut []
    } else {
        core::slice::from_raw_parts_mut(order, count)
    };
    h.store.cache_order(order).map_or_else(error, |()| 0)
}
/// # Safety
/// Handle is live/exclusive; out has cap writable bytes; written points to usize.
/// Outputs must not overlap each other, order, or the handle. cap >= CACHE_BUFFER_BYTES.
/// order has count readable u16s from write_begin, unchanged. Zero written means done.
#[no_mangle]
pub unsafe extern "C" fn xp_css_cache_write(
    handle: *mut Css,
    order: *const u16,
    count: usize,
    out: *mut u8,
    cap: usize,
    written: *mut usize,
) -> i32 {
    let h = &mut *handle;
    *written = 0;
    if cap < CACHE_BUFFER_BYTES || count != h.store.rule_count() {
        return 2;
    }
    let out = core::slice::from_raw_parts_mut(out, cap);
    if h.write_header {
        out[0] = CACHE_VERSION;
        out[1] = u8::from(!h.complete);
        out[2..4].copy_from_slice(&(h.store.rule_count() as u16).to_le_bytes());
        *written = 4;
        h.write_header = false;
        return 0;
    }
    if h.write_offset == count {
        return 0;
    }
    match h.store.cache_record(*order.add(h.write_offset), out) {
        Ok(n) => {
            *written = n;
            h.write_offset += 1;
            0
        }
        Err(e) => error(e),
    }
}
impl CacheReader {
    fn feed(
        &mut self,
        store: &mut Store,
        input: &[u8],
        end: bool,
        diagnostics: &Diagnostics,
    ) -> Result<(), Error> {
        for &b in input {
            if self.header && self.read == self.expected {
                return Err(Error::Invalid);
            }
            self.bytes.push(b);
            if !self.header {
                if self.bytes.len() == 4 {
                    if self.bytes[0] != CACHE_VERSION || self.bytes[1] > 1 {
                        if self.bytes[1] > 1 {
                            diagnostics.emit(Diagnostic::CacheFlags, usize::from(self.bytes[1]), 0);
                        }
                        return Err(Error::Invalid);
                    }
                    self.expected = usize::from(u16::from_le_bytes([self.bytes[2], self.bytes[3]]));
                    store.cache_begin_with_diagnostics(self.expected, diagnostics)?;
                    self.header = true;
                    self.bytes.clear();
                }
                continue;
            }
            if self.bytes.len() < 2 {
                continue;
            }
            let len = usize::from(u16::from_le_bytes([self.bytes[0], self.bytes[1]]));
            if len == 0 || len > MAX_CACHE_KEY_BYTES {
                diagnostics.emit(Diagnostic::CacheSelectorLength, len, 0);
                return Err(Error::Invalid);
            }
            if self.bytes.len() == 2 + len + WIRE_BYTES {
                store.cache_insert_with_diagnostics(
                    &self.bytes[2..2 + len],
                    &self.bytes[2 + len..],
                    diagnostics,
                )?;
                self.read += 1;
                self.bytes.clear();
            }
        }
        if end && (!self.header || self.read != self.expected || !self.bytes.is_empty()) {
            if !self.header && (self.bytes.len() < 2 || self.bytes[1] > 1) {
                diagnostics.emit(
                    Diagnostic::CacheFlags,
                    usize::from(*self.bytes.get(1).unwrap_or(&0)),
                    0,
                );
            }
            return Err(Error::Invalid);
        }
        Ok(())
    }
}
/// # Safety
/// Handle is live/exclusive. Input is borrowed readable bytes. First call after clear starts
/// a cache; end validates the complete file. Returns 0 complete, 1 OOM, 2 invalid.
#[no_mangle]
pub unsafe extern "C" fn xp_css_cache_read(
    handle: *mut Css,
    data: *const u8,
    len: usize,
    end: u8,
) -> i32 {
    xp_css_cache_read_with_diagnostic(handle, data, len, end, ptr::null_mut())
}
/// # Safety
/// Same as xp_css_cache_read; diagnostic is null or writable, aligned and disjoint.
/// Returns one failure detail (event zero when none); no callback crosses into C++.
#[no_mangle]
pub unsafe extern "C" fn xp_css_cache_read_with_diagnostic(
    handle: *mut Css,
    data: *const u8,
    len: usize,
    end: u8,
    diagnostic: *mut CacheDiagnostic,
) -> i32 {
    if let Some(out) = diagnostic.as_mut() {
        *out = CacheDiagnostic::default();
    }
    let diagnostics = Diagnostics {
        ctx: diagnostic.cast(),
        diagnostic: if diagnostic.is_null() {
            None
        } else {
            Some(record_cache_diagnostic)
        },
    };
    let h = &mut *handle;
    if h.cache.is_none() {
        h.store.clear();
        let Some(mut c) = boxed(CacheReader {
            bytes: Vec::new(),
            header: false,
            expected: 0,
            read: 0,
        }) else {
            diagnostics.emit(Diagnostic::CacheScratchOom, 1, 0);
            return 1;
        };
        if c.bytes.try_reserve_exact(CACHE_BUFFER_BYTES).is_err() {
            diagnostics.emit(Diagnostic::CacheScratchOom, 1, 0);
            return 1;
        }
        h.cache = Some(c);
    }
    let result =
        h.cache
            .as_mut()
            .unwrap()
            .feed(&mut h.store, bytes(data, len), end != 0, &diagnostics);
    if result.is_err() || end != 0 {
        h.cache = None;
    }
    match result {
        Ok(()) => 0,
        Err(e) => {
            h.store.clear();
            error(e)
        }
    }
}
