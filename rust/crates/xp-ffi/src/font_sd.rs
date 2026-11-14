//! SD faces and resident caches belong to the explicit loaded-font owner.
use crate::font_registry::Owner;
use core::{
    ffi::{c_char, c_void, CStr},
    pin::{pin, Pin},
    ptr,
    sync::atomic::{AtomicBool, Ordering},
};
use xp_core::{
    font::{
        raw::{FontData, Glyph},
        runtime::{ClearKind, Prewarm},
    },
    sdfont::{
        self,
        control::Control,
        io::{self, Slot},
        owner::FaceOwner,
        parser::Allocator,
        pool,
    },
};

#[cfg(any(test, feature = "sd-font-catalog"))]
#[path = "font_catalog.rs"]
pub(crate) mod catalog;

#[cfg(any(test, feature = "app-test"))]
use xp_core::font::manager::Family;
#[cfg(any(test, feature = "app-test", feature = "sd-font-catalog"))]
use xp_core::font::manager::Mode;

extern "C" {
    #[cfg(not(target_has_atomic = "8"))]
    fn __atomic_exchange_1(value: *mut u8, desired: u8, order: i32) -> u8;
    fn xp_native_chapter_font_owner(context: *mut c_void) -> *const Owner;
    pub(crate) fn xp_native_sd_handle_init(slot: *mut Slot);
    pub(crate) fn xp_native_sd_handle_destroy(slot: *mut Slot);
    pub(crate) fn xp_native_sd_file_open(
        slot: *mut Slot,
        path: *const c_char,
        size: *mut u32,
    ) -> u8;
    pub(crate) fn xp_native_sd_file_read(slot: *mut Slot, bytes: *mut u8, length: usize) -> i32;
    pub(crate) fn xp_native_sd_file_seek(slot: *mut Slot, offset: u32) -> u8;
    pub(crate) fn xp_native_sd_file_close(slot: *mut Slot);
    #[cfg(any(test, feature = "sd-font-catalog"))]
    pub(crate) fn xp_native_sd_allocate(bytes: usize, domain: u8) -> *mut u8;
    #[cfg(not(any(test, feature = "sd-font-catalog")))]
    pub(crate) fn xp_native_sd_allocate(bytes: usize, resident: bool) -> *mut u8;
    pub(crate) fn xp_native_sd_free(bytes: *mut u8, resident: bool);
    pub(crate) fn xp_native_sd_diagnostic(
        path: *const c_char,
        event: u32,
        args: *const u32,
        style: u32,
        index: u32,
        seek_failed: bool,
        #[cfg(any(test, feature = "sd-font-catalog"))] other: *const c_char,
    );
    pub(crate) fn heap_caps_get_largest_free_block(caps: u32) -> usize;
    pub(crate) fn heap_caps_get_free_size(caps: u32) -> usize;
    fn esp_timer_get_time() -> i64;
}
static CORRUPT_NOTICE: AtomicBool = AtomicBool::new(false);
pub(crate) static MEMORY: Allocator = Allocator {
    styles: allocate_internal,
    resident: allocate_resident,
    free_styles: free_internal,
    free_resident: xp_native_sd_free,
};
unsafe extern "C" fn allocate_internal(bytes: usize) -> *mut u8 {
    #[cfg(any(test, feature = "sd-font-catalog"))]
    {
        xp_native_sd_allocate(bytes, 0)
    }
    #[cfg(not(any(test, feature = "sd-font-catalog")))]
    {
        xp_native_sd_allocate(bytes, false)
    }
}
unsafe extern "C" fn allocate_resident(bytes: usize) -> *mut u8 {
    #[cfg(any(test, feature = "sd-font-catalog"))]
    {
        xp_native_sd_allocate(bytes, 1)
    }
    #[cfg(not(any(test, feature = "sd-font-catalog")))]
    {
        xp_native_sd_allocate(bytes, true)
    }
}
#[cfg(any(test, feature = "sd-font-catalog"))]
unsafe extern "C" fn allocate(bytes: usize, resident: bool) -> *mut u8 {
    xp_native_sd_allocate(bytes, u8::from(resident))
}
unsafe extern "C" fn free_internal(bytes: *mut u8, _: usize) {
    xp_native_sd_free(bytes, false)
}
unsafe extern "C" fn heap(largest: bool) -> u32 {
    // ESP.get{Free,MaxAlloc}Heap use MALLOC_CAP_INTERNAL (also on PSRAM boards).
    if largest {
        heap_caps_get_largest_free_block(1 << 11) as u32
    } else {
        heap_caps_get_free_size(1 << 11) as u32
    }
}
unsafe extern "C" fn clock() -> u32 {
    (esp_timer_get_time() as u64 / 1000) as u32
}
struct Physical;
// SAFETY: native leaves construct and operate only the pinned, correctly sized slot.
unsafe impl io::Physical for Physical {
    unsafe fn init(&mut self, slot: *mut Slot) {
        xp_native_sd_handle_init(slot);
    }
    unsafe fn destroy(&mut self, slot: *mut Slot) {
        xp_native_sd_handle_destroy(slot);
    }
    unsafe fn open(&mut self, slot: *mut Slot, path: &CStr) -> Option<u32> {
        let mut size = 0;
        (xp_native_sd_file_open(slot, path.as_ptr(), &mut size) != 0).then_some(size)
    }
    unsafe fn read(&mut self, slot: *mut Slot, bytes: &mut [u8]) -> usize {
        xp_native_sd_file_read(slot, bytes.as_mut_ptr(), bytes.len()).max(0) as usize
    }
    unsafe fn seek(&mut self, slot: *mut Slot, offset: u32) -> bool {
        xp_native_sd_file_seek(slot, offset) != 0
    }
    unsafe fn finish(&mut self, slot: *mut Slot) {
        xp_native_sd_file_close(slot);
    }
    fn diagnostic(&mut self, event: u32, a: u32, b: u32, c: u32) {
        unsafe {
            xp_native_sd_diagnostic(
                c"".as_ptr(),
                0x100 + event,
                [a, b, c, 0].as_ptr(),
                0,
                0,
                false,
                #[cfg(any(test, feature = "sd-font-catalog"))]
                ptr::null(),
            );
        }
    }
    fn parse_error(detail: &sdfont::Diagnostic, style: u32, index: u32) {
        unsafe {
            xp_native_sd_diagnostic(
                c"".as_ptr(),
                0x200 + detail.kind as u32,
                detail.args.as_ptr(),
                style,
                index,
                false,
                #[cfg(any(test, feature = "sd-font-catalog"))]
                ptr::null(),
            );
        }
    }
}
struct PoolIo<'a> {
    files: Pin<&'a mut io::Files<Physical>>,
    face: *mut c_void,
    path: *const c_char,
}
unsafe extern "C" fn open(raw: *mut c_void) -> bool {
    let io = &mut *raw.cast::<PoolIo<'_>>();
    io.files.as_mut().open(CStr::from_ptr(io.path)).is_some()
}
unsafe extern "C" fn close(raw: *mut c_void, release: bool) {
    (*raw.cast::<PoolIo<'_>>()).files.as_mut().close(release);
}
unsafe extern "C" fn read(
    raw: *mut c_void,
    offset: u32,
    bytes: *mut u8,
    length: usize,
    seek: bool,
) -> bool {
    let io = &mut *raw.cast::<PoolIo<'_>>();
    (!seek || io.files.as_mut().seek(offset))
        && io
            .files
            .as_mut()
            .read(core::slice::from_raw_parts_mut(bytes, length))
            == length
}
unsafe extern "C" fn corrupt(raw: *mut c_void, glyph: *const Glyph, cp: u32) {
    let io = &*raw.cast::<PoolIo<'_>>();
    let (mut face, _) = FaceOwner::from_context(io.face);
    let (first, log) = face.report_corrupt();
    if first {
        CORRUPT_NOTICE.store(true, Ordering::SeqCst);
    }
    if log {
        let g = &*glyph;
        xp_native_sd_diagnostic(
            io.path,
            0x300,
            [
                cp,
                u32::from(g.width) << 16 | u32::from(g.height),
                u32::from(g.data_length),
                g.data_offset,
            ]
            .as_ptr(),
            0,
            0,
            false,
            #[cfg(any(test, feature = "sd-font-catalog"))]
            ptr::null(),
        );
    }
}
unsafe extern "C" fn diagnostic(raw: *mut c_void, event: u32, a: u32, b: u32, c: u32, d: u32) {
    let io = &*raw.cast::<PoolIo<'_>>();
    xp_native_sd_diagnostic(
        io.path,
        event,
        [a, b, c, d].as_ptr(),
        0,
        0,
        io.files.seek_failed(),
        #[cfg(any(test, feature = "sd-font-catalog"))]
        ptr::null(),
    );
}
unsafe fn with_pool<R>(
    face: &mut FaceOwner<'_>,
    invalidate: bool,
    operation: impl FnOnce(&mut pool::Cache) -> R,
) -> R {
    if invalidate {
        face.invalidate();
    }
    let mut files = pin!(io::Files::new(Physical));
    let mut io = PoolIo {
        files: files.as_mut(),
        face: face.address().cast_mut(),
        path: face.path().as_ptr(),
    };
    let services = pool::Services {
        context: (&mut io as *mut PoolIo<'_>).cast(),
        open,
        close,
        read,
        #[cfg(any(test, feature = "sd-font-catalog"))]
        alloc: allocate,
        #[cfg(not(any(test, feature = "sd-font-catalog")))]
        alloc: xp_native_sd_allocate,
        free: xp_native_sd_free,
        heap,
        clock,
        corrupt,
        diagnostic: Some(diagnostic),
    };
    face.with_cache(&services, operation)
}
unsafe extern "C" fn miss(context: *mut c_void, cp: u32) -> *const Glyph {
    let (mut face, style) = FaceOwner::from_context(context);
    with_pool(&mut face, true, |cache| cache.miss(style, cp))
}
pub(crate) unsafe fn control<'a>(owner: *const Owner) -> &'a mut Control<'static> {
    &mut *(*owner).sd.get()
}
/// # Safety
/// Context lends an initialized owner; request and fallback are synchronous and
/// serialized. Fallback handles builtin/vector fonts only and cannot reenter SD.
#[cfg(any(test, feature = "sd-font-catalog"))]
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_chapter(
    context: *mut c_void,
    request: *const xp_layout::chapter::assembly::runtime::FontOperation,
    fallback: xp_layout::chapter::assembly::runtime::FontFallback,
) -> i32 {
    use xp_layout::chapter::assembly::runtime::FontOperationKind;
    let request = &*request;
    let Some(kind) = request.kind() else { return 0 };
    let owner = xp_native_chapter_font_owner(context);
    let result = match kind {
        FontOperationKind::Variant => {
            catalog::variant(owner, request.font, f32::from_bits(request.argument))
        }
        FontOperationKind::Ready => catalog::ready(owner, request.font, request.argument as i32),
    };
    result.unwrap_or_else(|| fallback(context, request))
}
#[cfg(any(test, feature = "sd-font-catalog"))]
unsafe fn load_face(owner: *const Owner, path: &CStr) -> Option<FaceOwner<'static>> {
    let mut files = pin!(io::Files::new(Physical));
    let mut face = FaceOwner::load(path, files.as_mut(), MEMORY, &(*owner).intervals)?;
    face.bind((*owner).registry.generation, miss);
    Some(face)
}
pub(crate) unsafe fn resolve(owner: *const Owner, id: i32, style: u8) -> Option<u8> {
    control(owner)
        .face(id)
        .map(|face| with_pool(face, false, |cache| cache.resolve(usize::from(style)) as u8))
}
pub(crate) unsafe fn prewarm(owner: *const Owner, id: i32, request: Prewarm<'_>) -> Option<i32> {
    control(owner).face(id).map(|face| {
        with_pool(face, true, |cache| {
            cache.prewarm_span(
                request.text.to_bytes(),
                request.styles,
                request.metadata_only,
                request.load_kern_lig,
                request.accumulate,
            )
        })
    })
}
pub(crate) unsafe fn resident(
    owner: *const Owner,
    id: i32,
    style: u8,
    text: *const u8,
    len: usize,
    metadata: bool,
) {
    #[cfg(feature = "app-test")]
    if control(owner).face(id).is_none() {
        crate::font_registry::xp_native_sd_font_resident_slice(
            (*owner).native,
            id,
            style,
            text,
            len,
            u8::from(metadata),
        );
        return;
    }
    if let Some(face) = control(owner).face(id) {
        with_pool(face, true, |cache| {
            cache.prewarm_span(
                core::slice::from_raw_parts(text, len),
                1 << (style & 3),
                metadata,
                false,
                true,
            )
        });
    }
}
pub(crate) unsafe fn clear(owner: *const Owner, id: i32, kind: ClearKind) -> bool {
    let Some(face) = control(owner).face(id) else {
        return false;
    };
    with_pool(face, true, |cache| {
        if kind == ClearKind::Frame {
            cache.clear_cache();
            return;
        }
        cache.clear_overflow();
        cache.clear_advances();
        for style in 0..4 {
            let slot = *cache.slots.add(style);
            if slot == 0 {
                continue;
            }
            let s = &mut *cache.styles.add(usize::from(slot - 1));
            s.free_mini(&*cache.services);
            s.free_ligatures(&*cache.services);
        }
    });
    true
}
pub(crate) unsafe fn reset(owner: *const Owner, id: i32) -> bool {
    let Some(face) = control(owner).face(id) else {
        return false;
    };
    with_pool(face, false, |cache| {
        cache.stats.write(pool::Stats::default())
    });
    true
}
/// # Safety
/// Owner is initialized and serialized. Family strings/files are borrowed only
/// until return; no glyph/source/registry loan survives this mutation.
#[cfg(any(test, feature = "app-test"))]
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_load(
    owner: *mut Owner,
    family: *const Family,
    points: u8,
    id: i32,
    mode: u8,
) -> i32 {
    let mode = match mode {
        0 => Mode::Replace,
        1 => Mode::ExtraSize,
        2 => Mode::Variant,
        _ => return 0,
    };
    let generation = (*owner).registry.generation;
    control(owner)
        .load_borrowed(
            &*family,
            points,
            id,
            mode,
            ptr::addr_of_mut!((*owner).registry),
            |path| {
                let mut files = pin!(io::Files::new(Physical));
                let mut face = FaceOwner::load(path, files.as_mut(), MEMORY, &(*owner).intervals)?;
                face.bind(generation, miss);
                Some(face)
            },
        )
        .unwrap_or(0)
}
/// # Safety
/// Owner is serialized; no loan survives unregister-before-free.
#[cfg(any(test, feature = "app-test"))]
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_unload(owner: *mut Owner, id: i32, all: u8) {
    control(owner).unload(
        ptr::addr_of_mut!((*owner).registry),
        if all != 0 { None } else { Some(id) },
    );
}
/// # Safety
/// Name is borrowed until replacement, full unload or drop; size is writable.
#[cfg(any(test, feature = "app-test"))]
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_current(owner: *const Owner, size: *mut u8) -> *const c_char {
    let manager = (*owner).sd.get();
    *size = (*manager).points();
    Control::family_pointer(manager)
}
/// # Safety
/// Family is readable and terminated; owner is initialized and serialized.
#[cfg(any(test, feature = "app-test"))]
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_id(
    owner: *const Owner,
    family: *const c_char,
    length: usize,
    size: u8,
) -> i32 {
    control(owner).get_bytes(
        if length == 0 {
            &[]
        } else {
            core::slice::from_raw_parts(family.cast(), length)
        },
        size,
    )
}
/// # Safety
/// Getter lends terminated strings until the next getter call. No descriptor or
/// glyph loan survives this synchronous, serialized prewarm operation.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_prewarm(
    owner: *const Owner,
    id: i32,
    getter: unsafe extern "C" fn(*const c_void, u32) -> *const u8,
    context: *const c_void,
    count: u32,
    styles: u8,
    metadata: u8,
    kern: u8,
    accumulate: u8,
) -> i32 {
    let Some(face) = control(owner).face(id) else {
        return 0;
    };
    with_pool(face, true, |cache| {
        cache.prewarm(
            getter,
            context,
            count,
            styles,
            metadata != 0,
            kern != 0,
            accumulate != 0,
        )
    })
}
/// # Safety
/// Process notice is shared only by the serialized font owner and reader.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_take_corrupt_notice() -> u8 {
    #[cfg(target_has_atomic = "8")]
    {
        u8::from(CORRUPT_NOTICE.swap(false, Ordering::SeqCst))
    }
    #[cfg(not(target_has_atomic = "8"))]
    {
        // C3 uses std::atomic<bool>'s compiler runtime; 5 is __ATOMIC_SEQ_CST.
        __atomic_exchange_1(CORRUPT_NOTICE.as_ptr().cast(), 0, 5)
    }
}

pub(crate) unsafe fn bitmap(font: *const FontData, glyph: *const Glyph) -> *const u8 {
    FaceOwner::from_context((*font).glyph_miss_ctx)
        .0
        .bitmap(font, glyph)
}

unsafe fn prepare<'a>(
    owner: *const Owner,
    id: i32,
    segments: impl Clone + Iterator<Item = &'a [u8]>,
    space: bool,
    hyphen: bool,
    styles: u8,
    small: bool,
) -> i32 {
    let Some(face) = control(owner).face(id) else {
        return 0;
    };
    // The retained BiDi workspace is shared with measurement; no shaped string is copied.
    let rtl = segments
        .clone()
        .any(|s| s.iter().any(|b| (0xd6..=0xdb).contains(b)));
    let visit = |emit: &mut dyn FnMut(&[u8])| {
        let work = if rtl {
            crate::font_registry::acquire_bidi()
        } else {
            ptr::null_mut()
        };
        for segment in segments.clone() {
            emit(segment);
            if work.is_null() {
                continue;
            }
            // Packed paragraph words are separated by NUL, just like native segments.
            for word in segment.split(|&b| b == 0) {
                for token in word.split(|b| matches!(b, b' ' | b'\n' | b'\r' | b'\t')) {
                    if !token.iter().any(|b| (0xd6..=0xdb).contains(b)) {
                        continue;
                    }
                    match xp_core::bidi::visual_diagnostic(token, -1, &mut *work) {
                        Ok(text) => emit(text),
                        Err(xp_core::bidi::VisualError::TooLong) => {
                            crate::font_registry::report(owner, 2, xp_core::bidi::MAX_LINE as u32)
                        }
                        _ => {}
                    }
                }
            }
        }
        if !work.is_null() {
            crate::font_registry::xp_native_bidi_release();
        }
    };
    let mut limit = 0usize;
    visit(&mut |bytes| limit = limit.saturating_add(bytes.len()));
    if small {
        limit = limit.saturating_mul(2);
    }
    with_pool(face, true, |cache| {
        cache.build_advances_from(limit, styles, |emit| {
            visit(&mut |bytes| {
                let mut at = 0;
                while at < bytes.len() {
                    if bytes[at] == 0 {
                        at += 1;
                        continue;
                    }
                    let Some(cp) = xp_core::utf8::next(bytes, &mut at) else {
                        break;
                    };
                    emit(cp, false);
                    if small {
                        let upper = xp_core::text::small_caps_upper(cp);
                        if upper != cp {
                            emit(upper, false);
                        }
                    }
                }
            });
            if space {
                emit(32, true);
            }
            if hyphen {
                emit(45, true);
            }
        })
    })
}
unsafe fn bytes<'a>(data: *const u8, length: usize) -> &'a [u8] {
    if length == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(data, length)
    }
}
/// # Safety
/// Spans and their NUL-separated word bytes are borrowed for this serialized call.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_prepare_spans(
    context: *mut c_void,
    id: i32,
    spans: *const xp_layout::para::paragraph::Span,
    count: u32,
    space: u8,
    hyphen: u8,
    styles: u8,
    small: u8,
) {
    prepare(
        context.cast(),
        id,
        (if count == 0 {
            &[]
        } else {
            core::slice::from_raw_parts(spans, count as usize)
        })
        .iter()
        .map(|s| bytes(s.data, s.len as usize)),
        space != 0,
        hyphen != 0,
        styles,
        small != 0,
    );
}
/// # Safety
/// Context is a live native chapter sink. Its borrowed owner and spans remain
/// serialized and valid for this call; the address leaf performs no policy.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_prepare_chapter(
    context: *mut c_void,
    id: i32,
    spans: *const xp_layout::para::paragraph::Span,
    count: u32,
    space: u8,
    hyphen: u8,
    styles: u8,
    small: u8,
) {
    xp_sd_font_prepare_spans(
        xp_native_chapter_font_owner(context).cast_mut().cast(),
        id,
        spans,
        count,
        space,
        hyphen,
        styles,
        small,
    );
}
/// # Safety
/// Descriptor arrays and their bytes are borrowed until return; extra is null.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_prepare(
    owner: *const Owner,
    id: i32,
    texts: *const pool::Texts,
    styles: u8,
) -> i32 {
    let texts = &*texts;
    prepare(
        owner,
        id,
        (0..texts.count).map(|i| bytes(*texts.segments.add(i), *texts.lengths.add(i))),
        texts.space,
        texts.hyphen,
        styles,
        texts.small_caps,
    )
}

#[cfg(feature = "app-test")]
pub(crate) unsafe fn is_native_test_face(_: *const Owner, font: *const FontData) -> bool {
    !(*font).glyph_miss_handler.is_some_and(|handler| {
        core::ptr::fn_addr_eq(
            handler,
            miss as unsafe extern "C" fn(*mut c_void, u32) -> *const Glyph,
        )
    })
}
