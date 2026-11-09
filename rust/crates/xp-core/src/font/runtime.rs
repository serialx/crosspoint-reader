//! Bitmap-font cache and scan lifetime; the registry owns family identity.
use super::{
    cache::{Cache, Services, Stats},
    loaded::{Entry, Registry},
    raw::{FontData, Glyph},
};
use crate::inflate::SmallInflateState;
use core::{
    cell::UnsafeCell,
    ffi::{c_void, CStr},
    mem::{align_of, size_of},
    ptr, slice,
};

const CODEPOINTS: usize = 512;
const FONTS: usize = 4;
const GROUPS: usize = FONTS * 4;
const CODEPOINT_MASK: u32 = (1 << 21) - 1;
const SMALL_CAPS: u8 = 128;

#[repr(u32)]
pub enum ManagerEvent {
    ScanCap = 11,
    SdMiss = 12,
    BuiltInMiss = 13,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum ClearKind {
    Frame,
    Resident,
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Backend {
    Sd,
    Vector,
}

#[derive(Clone, Copy)]
pub struct Prewarm<'a> {
    pub text: &'a CStr,
    pub styles: u8,
    pub metadata_only: bool,
    pub load_kern_lig: bool,
    pub accumulate: bool,
}

/// Temporary per-font operations. The caller chooses font, style and flags;
/// implementations never iterate registrations. Vector pressure may evict caches,
/// but cannot mutate the active scan, registrations or borrowed input text.
pub trait SdFontCacheCompat {
    /// # Safety
    /// The registered SD font and its native resource remain live for the call.
    unsafe fn resolve_style(&mut self, font: i32, style: u8) -> u8;
    /// # Safety
    /// Text is borrowed synchronously; no view survives the font mutation.
    unsafe fn prewarm(&mut self, font: i32, backend: Backend, request: Prewarm<'_>) -> i32;
    /// # Safety
    /// All source/bitmap loans have ended before clearing this font.
    unsafe fn clear(&mut self, font: i32, backend: Backend, kind: ClearKind);
    /// # Safety
    /// Serialized access excludes any concurrent font operation.
    unsafe fn reset_stats(&mut self, font: i32);
}

pub struct Physical {
    pub context: *mut c_void,
    pub alloc: unsafe extern "C" fn(usize) -> *mut u8,
    pub free: unsafe extern "C" fn(*mut u8),
    pub clock: unsafe extern "C" fn(bool) -> u32,
    pub generation: unsafe extern "C" fn(),
    pub diagnostic:
        Option<unsafe extern "C" fn(*mut c_void, *const FontData, u32, u32, u32, u32, u32)>,
}
impl Physical {
    unsafe fn log(&self, font: *const FontData, event: u32, a: u32, b: u32, c: u32, d: u32) {
        if let Some(log) = self.diagnostic {
            log(self.context, font, event, a, b, c, d);
        }
    }
}

// Preserve the former font InflateReader's C3 reservation during ownership moves.
#[repr(C)]
struct Decoder {
    alignment: [usize; 0],
    storage: [u8; 1292],
}
const _: () = {
    assert!(size_of::<SmallInflateState>() <= size_of::<Decoder>());
    assert!(align_of::<SmallInflateState>() <= align_of::<Decoder>());
};
impl Decoder {
    unsafe fn inflate(&mut self, input: *const u8, len: u32, output: *mut u8, bytes: u32) -> i32 {
        let state = self.storage.as_mut_ptr().cast::<SmallInflateState>();
        SmallInflateState::init_in_place(state, false);
        if bytes == 0 {
            return 1;
        }
        // Match InflateReader's initialization and exact-length read contract.
        ptr::write_bytes(output, 0, bytes as usize);
        let mut input = slice::from_raw_parts(input, len as usize);
        let output = slice::from_raw_parts_mut(output, bytes as usize);
        let mut position = 0;
        while position < output.len() {
            let (status, used, written) = (*state).step_with_limit(
                input,
                false,
                output,
                position,
                false,
                output.len() - position,
            );
            input = &input[used..];
            position += written;
            if status < 0 {
                return 0;
            }
            if status == 1 {
                return i32::from(position == output.len());
            }
            if used == 0 && written == 0 {
                return 0;
            }
        }
        1
    }
}
struct CacheCall<'a> {
    decoder: &'a mut Decoder,
    physical: &'a Physical,
    font: *const FontData,
}
unsafe extern "C" fn inflate(
    context: *mut c_void,
    input: *const u8,
    len: u32,
    output: *mut u8,
    bytes: u32,
) -> i32 {
    (*context.cast::<CacheCall<'_>>())
        .decoder
        .inflate(input, len, output, bytes)
}
unsafe extern "C" fn diagnostic(context: *mut c_void, event: u32, a: u32, b: u32, c: u32, d: u32) {
    let call = &*context.cast::<CacheCall<'_>>();
    call.physical.log(call.font, event, a, b, c, d);
}
impl CacheCall<'_> {
    fn services(&mut self) -> Services {
        Services {
            ctx: ptr::from_mut(self).cast(),
            alloc: self.physical.alloc,
            free: self.physical.free,
            inflate,
            clock: self.physical.clock,
            generation: self.physical.generation,
            diagnostic: Some(diagnostic),
        }
    }
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
enum ScanMode {
    None,
    Scanning,
}
#[repr(C)]
struct Scan {
    fonts: [i32; FONTS],
    codepoints: [u32; CODEPOINTS + 1],
    groups: [u16; GROUPS],
    count: u16,
    font_count: u8,
    warned: bool,
    mode: ScanMode,
}
impl Scan {
    fn reset(&mut self) {
        self.count = 0;
        self.font_count = 0;
        self.warned = false;
        self.groups.fill(0);
    }
    unsafe fn record(&mut self, font: i32, text: &[u8], style: u8, io: &Physical) {
        if text.is_empty() || text[0] == 0 {
            return;
        }
        let slot = match self.fonts[..usize::from(self.font_count)]
            .iter()
            .position(|&id| id == font)
        {
            Some(slot) => slot,
            None if usize::from(self.font_count) < FONTS => {
                let slot = usize::from(self.font_count);
                self.fonts[slot] = font;
                self.font_count += 1;
                slot
            }
            None => return,
        };
        let group = slot * 4 + usize::from(style & 3);
        let mut position = 0;
        while let Some(cp) = crate::utf8::next(text, &mut position) {
            if cp == 0 {
                break;
            }
            let cp = if style & SMALL_CAPS != 0 {
                crate::text::small_caps_upper(cp)
            } else {
                cp
            };
            let packed = ((slot as u32) << 23) | (u32::from(style & 3) << 21) | cp;
            if self.codepoints[..usize::from(self.count)].contains(&packed) {
                continue;
            }
            if usize::from(self.count) == CODEPOINTS {
                if !self.warned {
                    io.log(
                        ptr::null(),
                        ManagerEvent::ScanCap as u32,
                        CODEPOINTS as u32,
                        0,
                        0,
                        0,
                    );
                    self.warned = true;
                }
                continue;
            }
            self.codepoints[usize::from(self.count)] = packed;
            self.count += 1;
            self.groups[group] += 1;
        }
    }
    fn prepare(&mut self) -> usize {
        self.mode = ScanMode::None;
        self.codepoints[..usize::from(self.count)].sort_unstable();
        usize::from(self.count)
    }
    fn batch(&mut self, group: usize, start: &mut usize) -> Option<(i32, Prewarm<'_>)> {
        let count = usize::from(self.groups[group]);
        *start -= count;
        if count == 0 {
            return None;
        }
        // High groups go first; the terminator touches only consumed words.
        let words = self.codepoints.as_mut_ptr();
        let bytes = unsafe { words.add(*start).cast::<u8>() };
        let mut written = 0;
        for i in 0..count {
            let cp = unsafe { words.add(*start + i).read() } & CODEPOINT_MASK;
            let mut encoded = [0; 4];
            let len = crate::utf8::encode(cp, &mut encoded);
            unsafe {
                ptr::copy_nonoverlapping(encoded.as_ptr(), bytes.add(written), len);
            }
            written += len;
        }
        unsafe {
            bytes.add(written).write(0);
            let text =
                CStr::from_bytes_with_nul_unchecked(slice::from_raw_parts(bytes, written + 1));
            Some((
                self.fonts[group / 4],
                Prewarm {
                    text,
                    styles: 1 << (group & 3),
                    metadata_only: false,
                    load_kern_lig: true,
                    accumulate: false,
                },
            ))
        }
    }
}

/// One cache/scan owner in final storage. Calls are serialized; vector pressure
/// may reenter eviction while the scan's immutable input remains borrowed.
#[repr(C)]
pub struct FontCacheOwner {
    cache: UnsafeCell<Cache>,
    decoder: UnsafeCell<Decoder>,
    scan: UnsafeCell<Scan>,
    // Two former native pointer words now hold the native handle/current binding.
    _placement: [usize; 2],
}
#[cfg(target_pointer_width = "32")]
const _: () = assert!(size_of::<FontCacheOwner>() == 3604);
impl FontCacheOwner {
    /// # Safety
    /// `out` is aligned final storage, exclusively writable and uninitialized.
    pub unsafe fn initialize(out: *mut Self) {
        ptr::write_bytes(out, 0, 1);
        ptr::addr_of_mut!((*(*out).cache.get()).hot_index).write(u16::MAX);
    }
    pub fn scanning(&self) -> bool {
        unsafe { (*self.scan.get()).mode == ScanMode::Scanning }
    }
    pub fn cache(&self) -> &Cache {
        unsafe { &*self.cache.get() }
    }
    /// # Safety
    /// Host oracle only: no statistics loan survives this serialized reset.
    #[cfg(feature = "chapter-test")]
    pub unsafe fn reset_stats(&self) {
        (*self.cache.get()).stats = Stats::default();
    }
    /// # Safety
    /// Host oracle only: bitmap loans have ended; physical callbacks cannot reenter.
    #[cfg(feature = "chapter-test")]
    pub unsafe fn clear_builtin(&self, io: &Physical) {
        let mut call = CacheCall {
            decoder: &mut *self.decoder.get(),
            physical: io,
            font: ptr::null(),
        };
        (*self.cache.get()).clear(&call.services());
    }
    /// # Safety
    /// Host oracle only: validated tables and non-reentrant physical callbacks.
    #[cfg(feature = "chapter-test")]
    pub unsafe fn prewarm_data(&self, font: &FontData, text: &[u8], io: &Physical) -> i32 {
        let mut call = CacheCall {
            decoder: &mut *self.decoder.get(),
            physical: io,
            font,
        };
        (*self.cache.get()).prewarm(font, text, &call.services())
    }
    /// # Safety
    /// Live registrations/resources; all bitmap loans have ended. Compat may
    /// evict caches, but cannot mutate the scan, registry or its borrowed text.
    /// Physical allocator/clock/diagnostic callbacks cannot reenter this owner.
    pub unsafe fn begin(
        &self,
        registry: *const Registry,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        (*self.scan.get()).mode = ScanMode::Scanning;
        self.clear(registry, ClearKind::Frame, io, sd);
        (*self.cache.get()).stats = Stats::default();
        for_backend(registry, Backend::Sd, |id| sd.reset_stats(id));
        (*self.scan.get()).reset();
    }
    /// # Safety
    /// Same resource/serialization contract as begin; text is borrowed synchronously.
    pub unsafe fn record(
        &self,
        registry: *const Registry,
        font: i32,
        text: &[u8],
        style: u8,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        if text.is_empty() || text[0] == 0 {
            return;
        }
        let resolved = match (*registry).find(font) {
            Some(entry) => {
                let selected = data(entry, style);
                if selected.is_null() {
                    style & 3
                } else if is_sd(selected) {
                    sd.resolve_style(font, style & 3)
                } else {
                    (0..4)
                        .find(|&s| data(entry, s) == selected)
                        .unwrap_or(style & 3)
                }
            }
            None => style & 3,
        };
        self.record_resolved(font, text, resolved | (style & SMALL_CAPS), io);
    }
    /// # Safety
    /// Canonical style; scan mutation excludes every active batch/scan loan.
    pub unsafe fn record_resolved(&self, font: i32, text: &[u8], style: u8, io: &Physical) {
        (*self.scan.get()).record(font, text, style, io);
    }
    /// # Safety
    /// Same contract as begin. No cache/decoder/scan reference crosses compat;
    /// the immutable batch bytes remain live while vector pressure evicts caches.
    pub unsafe fn finish(
        &self,
        registry: *const Registry,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        let mut start = (*self.scan.get()).prepare();
        for group in (0..GROUPS).rev() {
            if let Some((font, request)) = (*self.scan.get()).batch(group, &mut start) {
                prewarm(self, registry, font, request, io, sd);
            }
        }
        (*self.scan.get()).reset();
    }
    /// # Safety
    /// Same contract as begin; closes one active native/Rust scope.
    pub unsafe fn end(
        &self,
        registry: *const Registry,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        self.finish(registry, io, sd);
        self.clear(registry, ClearKind::Frame, io, sd);
    }
    /// # Safety
    /// Same contract as begin; every bitmap/source loan has ended.
    pub unsafe fn clear(
        &self,
        registry: *const Registry,
        kind: ClearKind,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        {
            let mut call = CacheCall {
                decoder: &mut *self.decoder.get(),
                physical: io,
                font: ptr::null(),
            };
            (*self.cache.get()).clear(&call.services());
        }
        for backend in [Backend::Sd, Backend::Vector] {
            for_backend(registry, backend, |id| sd.clear(id, backend, kind));
        }
    }
    /// # Safety
    /// Same loan/compat contract as clear; SDK eviction chooses one backend.
    pub unsafe fn clear_backend(
        &self,
        registry: *const Registry,
        backend: Backend,
        kind: ClearKind,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        for_backend(registry, backend, |id| sd.clear(id, backend, kind));
    }
    /// # Safety
    /// Same contract as begin; text is terminated and borrowed synchronously.
    pub unsafe fn prewarm(
        &self,
        registry: *const Registry,
        font: i32,
        request: Prewarm<'_>,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) -> i32 {
        prewarm(self, registry, font, request, io, sd)
    }
    /// # Safety
    /// Live resident font/glyph tables. Consume bytes before any font mutation;
    /// physical services cannot reenter the built-in decoder/cache operation.
    pub unsafe fn bitmap(
        &self,
        font: &FontData,
        glyph: &Glyph,
        index: u32,
        io: &Physical,
    ) -> *const u8 {
        let mut call = CacheCall {
            decoder: &mut *self.decoder.get(),
            physical: io,
            font,
        };
        (*self.cache.get()).bitmap(font, glyph, index, &call.services())
    }
}
unsafe fn data(entry: Entry, style: u8) -> *const FontData {
    let slot = entry.slot(style);
    if slot.is_null() {
        ptr::null()
    } else {
        slot.read()
    }
}
unsafe fn is_sd(data: *const FontData) -> bool {
    !data.is_null() && !(*data).glyph_miss_ctx.is_null() && (*data).vector_bitmap_handler.is_none()
}
unsafe fn backend(data: *const FontData) -> Option<Backend> {
    if data.is_null() {
        None
    } else if (*data).vector_bitmap_handler.is_some() {
        Some(Backend::Vector)
    } else if is_sd(data) {
        Some(Backend::Sd)
    } else {
        None
    }
}
unsafe fn for_backend(registry: *const Registry, kind: Backend, mut call: impl FnMut(i32)) {
    for i in 0..(*registry).len() {
        let entry = (*registry).at(i).unwrap();
        if backend(data(entry, 0)) == Some(kind) {
            call(entry.id);
        }
    }
}
#[inline(always)]
unsafe fn prewarm(
    owner: &FontCacheOwner,
    registry: *const Registry,
    font: i32,
    request: Prewarm<'_>,
    io: &Physical,
    sd: &mut dyn SdFontCacheCompat,
) -> i32 {
    let slot = (*registry).slot(font, 0);
    if slot.is_null() {
        return 0;
    }
    if let Some(backend) = backend(slot.read()) {
        let missed = sd.prewarm(font, backend, request);
        if missed > 0 {
            io.log(
                ptr::null(),
                ManagerEvent::SdMiss as u32,
                missed as u32,
                u32::from(request.styles),
                0,
                0,
            );
        }
        return missed;
    }
    prewarm_builtin(
        &mut *owner.cache.get(),
        &mut *owner.decoder.get(),
        registry,
        font,
        request,
        io,
    )
}

// SD prewarm must not pay for the built-in cache's call/service frame.
#[inline(never)]
unsafe fn prewarm_builtin(
    cache: &mut Cache,
    decoder: &mut Decoder,
    registry: *const Registry,
    font: i32,
    request: Prewarm<'_>,
    io: &Physical,
) -> i32 {
    let mut total = 0;
    for style in 0..4 {
        if request.styles & (1 << style) == 0 {
            continue;
        }
        let slot = (*registry).slot(font, style);
        let data = if slot.is_null() {
            ptr::null()
        } else {
            slot.read()
        };
        if data.is_null() || (*data).groups.is_null() {
            continue;
        }
        let mut call = CacheCall {
            decoder,
            physical: io,
            font: data,
        };
        let missed = cache.prewarm(&*data, request.text.to_bytes(), &call.services());
        if missed > 0 {
            io.log(
                data,
                ManagerEvent::BuiltInMiss as u32,
                missed as u32,
                u32::from(style),
                0,
                0,
            );
        }
        total += missed;
    }
    total
}

#[cfg(test)]
mod tests;
