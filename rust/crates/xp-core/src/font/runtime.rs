//! Bitmap-font cache and scan lifetime; the registry owns family identity.
use super::{
    cache::{Cache, Services, Stats},
    loaded::{Entry, Registry},
    raw::{FontData, Glyph},
};
use crate::inflate::SmallInflateState;
use core::{
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
pub enum ClearKind {
    Frame,
    Resident,
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
/// implementations neither iterate registrations nor reenter the cache owner.
pub trait SdFontCacheCompat {
    /// # Safety
    /// The registered SD font and its native resource remain live for the call.
    unsafe fn resolve_style(&mut self, font: i32, style: u8) -> u8;
    /// # Safety
    /// Text is borrowed synchronously; no view survives the font mutation.
    unsafe fn prewarm(&mut self, font: i32, request: Prewarm<'_>) -> i32;
    /// # Safety
    /// All source/bitmap loans have ended before clearing this font.
    unsafe fn clear(&mut self, font: i32, kind: ClearKind);
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
    fn finish(&mut self, mut prewarm: impl FnMut(i32, Prewarm<'_>)) {
        self.mode = ScanMode::None;
        if self.count == 0 {
            return;
        }
        self.codepoints[..usize::from(self.count)].sort_unstable();
        let mut start = usize::from(self.count);
        // High groups go first: a UTF-8 terminator can overwrite only consumed words.
        for group in (0..GROUPS).rev() {
            let count = usize::from(self.groups[group]);
            start -= count;
            if count == 0 {
                continue;
            }
            let words = self.codepoints.as_mut_ptr();
            let bytes = unsafe { words.add(start).cast::<u8>() };
            let mut written = 0;
            for i in 0..count {
                let cp = unsafe { words.add(start + i).read() } & CODEPOINT_MASK;
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
                prewarm(
                    self.fonts[group / 4],
                    Prewarm {
                        text,
                        styles: 1 << (group & 3),
                        metadata_only: false,
                        load_kern_lig: true,
                        accumulate: false,
                    },
                );
            }
        }
        self.reset();
    }
}

/// Bitmap-font owner for the no-vector-font profile. Initialize in its final
/// storage; calls are serialized and physical services must not reenter it.
pub struct FontCacheOwner {
    cache: Cache,
    decoder: Decoder,
    scan: Scan,
    // Retain the former native service/map-pointer footprint during the port.
    _placement: [usize; 4],
}
#[cfg(target_pointer_width = "32")]
const _: () = assert!(size_of::<FontCacheOwner>() == 3612);
impl FontCacheOwner {
    /// # Safety
    /// `out` is aligned final storage, exclusively writable and not initialized.
    pub unsafe fn initialize(out: *mut Self) {
        // Every field has a valid all-zero representation, including ScanMode::None.
        ptr::write_bytes(out, 0, 1);
        ptr::addr_of_mut!((*out).cache.hot_index).write(u16::MAX);
    }
    pub fn scanning(&self) -> bool {
        self.scan.mode == ScanMode::Scanning
    }
    pub fn cache(&self) -> &Cache {
        &self.cache
    }
    /// # Safety
    /// Registry slots/SD resources remain live. No vector faces or source loans
    /// may be present; callbacks are synchronous and cannot reenter this owner.
    pub unsafe fn begin(
        &mut self,
        registry: *const Registry,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        self.scan.mode = ScanMode::Scanning;
        self.clear(registry, ClearKind::Frame, io, sd);
        self.cache.stats = Stats::default();
        for_sd(registry, |id| sd.reset_stats(id));
        self.scan.reset();
    }
    /// # Safety
    /// Same resource/serialization contract as begin; text is used only in this call.
    pub unsafe fn record(
        &mut self,
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
    /// Style is already canonical; services follow begin's non-reentry contract.
    pub unsafe fn record_resolved(&mut self, font: i32, text: &[u8], style: u8, io: &Physical) {
        self.scan.record(font, text, style, io);
    }
    /// # Safety
    /// Same resource/serialization contract as begin. No borrowed source survives prewarm.
    pub unsafe fn finish(
        &mut self,
        registry: *const Registry,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        let Self {
            cache,
            decoder,
            scan,
            ..
        } = self;
        scan.finish(|id, request| {
            prewarm(cache, decoder, registry, id, request, io, sd);
        });
    }
    /// # Safety
    /// Same resource/serialization contract as begin; this closes one active scope.
    pub unsafe fn end(
        &mut self,
        registry: *const Registry,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        self.finish(registry, io, sd);
        self.clear(registry, ClearKind::Frame, io, sd);
    }
    /// # Safety
    /// Same resource/serialization contract as begin. Bitmap/source loans have ended.
    pub unsafe fn clear(
        &mut self,
        registry: *const Registry,
        kind: ClearKind,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) {
        let mut call = CacheCall {
            decoder: &mut self.decoder,
            physical: io,
            font: ptr::null(),
        };
        self.cache.clear(&call.services());
        for_sd(registry, |id| sd.clear(id, kind));
    }
    /// # Safety
    /// Same resource/serialization contract as begin. Text has a terminating zero.
    pub unsafe fn prewarm(
        &mut self,
        registry: *const Registry,
        font: i32,
        request: Prewarm<'_>,
        io: &Physical,
        sd: &mut dyn SdFontCacheCompat,
    ) -> i32 {
        prewarm(
            &mut self.cache,
            &mut self.decoder,
            registry,
            font,
            request,
            io,
            sd,
        )
    }
    /// # Safety
    /// Font/index/glyph refer to live resident tables. Consume the returned bytes
    /// before another bitmap request or any cache/SD/registry mutation.
    pub unsafe fn bitmap(
        &mut self,
        font: &FontData,
        glyph: &Glyph,
        index: u32,
        io: &Physical,
    ) -> *const u8 {
        let mut call = CacheCall {
            decoder: &mut self.decoder,
            physical: io,
            font,
        };
        self.cache.bitmap(font, glyph, index, &call.services())
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
unsafe fn for_sd(registry: *const Registry, mut call: impl FnMut(i32)) {
    for i in 0..(*registry).len() {
        let entry = (*registry).at(i).unwrap();
        if is_sd(data(entry, 0)) {
            call(entry.id);
        }
    }
}
unsafe fn prewarm(
    cache: &mut Cache,
    decoder: &mut Decoder,
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
    if is_sd(slot.read()) {
        let missed = sd.prewarm(font, request);
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
    prewarm_builtin(cache, decoder, registry, font, request, io)
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
