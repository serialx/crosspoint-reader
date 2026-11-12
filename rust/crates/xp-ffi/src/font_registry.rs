//! Native-origin registration and UI queries share the Rust loaded-font owner.
use core::{
    cell::UnsafeCell,
    ffi::c_void,
    mem::{align_of, size_of},
    ptr,
    sync::atomic::AtomicU32,
};
use xp_core::font::loaded::{Entry, Registry};
use xp_core::{
    font::raw::FontData,
    glyph::BitmapSource,
    textm::{
        native::{Family, Provider},
        Glyph,
    },
};

extern "C" {
    #[cfg(feature = "app-test")]
    fn xp_native_sd_font_view(
        native: *mut c_void,
        font: i32,
        family: *mut Family,
        source: *mut BitmapSource,
    );
    #[cfg(feature = "app-test")]
    pub(crate) fn xp_native_sd_font_resident_slice(
        native: *mut c_void,
        font: i32,
        style: u8,
        text: *const u8,
        len: usize,
        metadata: u8,
    );
    #[cfg(any(not(feature = "sd-font-catalog"), feature = "app-test"))]
    pub(crate) fn xp_native_sd_font_load(native: *mut c_void, font: i32) -> u8;
    fn xp_native_font_diagnostic(native: *mut c_void, event: u32, value: u32);
    fn xp_native_bidi_acquire(size: usize, alignment: usize) -> *mut xp_core::bidi::Workspace;
    pub(crate) fn xp_native_bidi_release();
}

pub(crate) unsafe extern "C" fn acquire_bidi() -> *mut xp_core::bidi::Workspace {
    xp_native_bidi_acquire(
        size_of::<xp_core::bidi::Workspace>(),
        align_of::<xp_core::bidi::Workspace>(),
    )
}

#[repr(C)]
pub(crate) struct Owner {
    pub(crate) registry: Registry,
    pub(crate) native: *mut c_void,
    pub(crate) cache: UnsafeCell<xp_core::font::runtime::FontCacheOwner>,
    pub(crate) sd: UnsafeCell<xp_core::sdfont::control::Control<'static>>,
    #[cfg(feature = "vector-fonts")]
    pub(crate) vector: UnsafeCell<crate::font_vector::State>,
    pub(crate) intervals: xp_core::sdfont::parser::IntervalRegistry,
    #[cfg(any(test, feature = "sd-font-catalog"))]
    pub(crate) catalog: crate::font_sd::catalog::State,
}
#[repr(C)]
pub union QueryResult {
    value: usize,
    slot: *const *const FontData,
}

impl Owner {
    pub(crate) unsafe fn slot(owner: *const Self, id: i32, style: u8) -> *const *const FontData {
        (*owner).registry.slot(id, style)
    }
    pub(crate) unsafe fn data(owner: *const Self, id: i32, style: u8) -> *const FontData {
        let slot = Self::slot(owner, id, style);
        if slot.is_null() {
            ptr::null()
        } else {
            slot.read()
        }
    }
    pub(crate) unsafe fn ensure(owner: *const Self, id: i32) -> bool {
        #[cfg(not(any(test, feature = "sd-font-catalog")))]
        {
            let native = (*owner).native;
            xp_core::font::loaded::ensure(ptr::addr_of!((*owner).registry), id, |id| {
                xp_native_sd_font_load(native, id) != 0
            })
        }
        #[cfg(any(test, feature = "sd-font-catalog"))]
        {
            xp_core::font::loaded::ensure(ptr::addr_of!((*owner).registry), id, |id| {
                if crate::font_sd::catalog::ensure_id(owner, id) {
                    return true;
                }
                #[cfg(feature = "app-test")]
                {
                    xp_native_sd_font_load((*owner).native, id) != 0
                }
                #[cfg(not(feature = "app-test"))]
                {
                    false
                }
            })
        }
    }
}

unsafe extern "C" fn family(context: *mut c_void, id: i32, out: *mut Family) {
    let owner = context.cast::<Owner>();
    let Some(entry) = (*owner).registry.find(id) else {
        return;
    };
    (*out).fallback = entry.fallback;
    (*out).has_fallback = u8::from(entry.fallback != 0);
    for style in 0..4 {
        (*out).styles[style].data = entry.slot(style as u8).read();
        (*out).resolved[style] = style as u8;
    }
    let data = (*out).styles[0].data;
    if !data.is_null()
        && !(*data).glyph_miss_ctx.is_null()
        && (*data).vector_bitmap_handler.is_none()
    {
        if let Some(face) = crate::font_sd::control(owner).face(id) {
            face.metric_view(&mut *out);
        } else {
            #[cfg(feature = "app-test")]
            xp_native_sd_font_view((*owner).native, id, out, ptr::null_mut());
        }
    }
}
unsafe extern "C" fn glyph(
    context: *mut c_void,
    id: i32,
    cp: u32,
    style: u8,
    out: *mut Glyph,
) -> u8 {
    let owner = context.cast::<Owner>();
    let data = Owner::data(owner, id, style);
    if data.is_null() {
        return 0;
    }
    (*owner).registry.invalidate();
    let glyph = crate::font::xp_font_lookup(data, cp);
    if glyph.is_null() {
        return 0;
    }
    let g = glyph.read();
    out.write(Glyph {
        advance: g.advance_x,
        width: g.width,
        height: g.height,
        left: g.left,
        top: g.top,
    });
    1
}
unsafe extern "C" fn coverage(context: *mut c_void, id: i32, cp: u32, style: u8) -> u8 {
    let data = Owner::data(context.cast(), id, style);
    u8::from(!data.is_null() && crate::font::xp_font_coverage(data, cp))
}
unsafe extern "C" fn kern(context: *mut c_void, id: i32, left: u32, right: u32, style: u8) -> i8 {
    let owner = context.cast::<Owner>();
    let data = Owner::data(owner, id, style);
    (*owner).registry.invalidate();
    if data.is_null() {
        0
    } else {
        crate::font::xp_font_dynamic_kern(data, left, right)
    }
}
unsafe extern "C" fn prewarm(
    context: *mut c_void,
    id: i32,
    text: *const u8,
    len: usize,
    style: u8,
) {
    let owner = context.cast::<Owner>();
    crate::font_sd::resident(owner, id, style, text, len, true);
}
unsafe extern "C" fn diagnostic(
    context: *mut c_void,
    event: u32,
    value: u32,
    _: u32,
    _: u32,
    _: u32,
) {
    xp_native_font_diagnostic((*context.cast::<Owner>()).native, event, value);
}
pub(crate) unsafe fn report(owner: *const Owner, event: u32, value: u32) {
    xp_native_font_diagnostic((*owner).native, event, value);
}
/// # Safety
/// Owner, font slots and native context obey the serialized synchronous provider contract.
#[no_mangle]
pub unsafe extern "C" fn xp_font_registry_provider(owner: *const c_void) -> Provider {
    let owner = owner.cast::<Owner>();
    Provider {
        context: owner.cast_mut().cast(),
        generation: (*owner).registry.generation,
        family,
        glyph,
        coverage,
        kern,
        bidi_acquire: acquire_bidi,
        bidi_release: xp_native_bidi_release,
        prewarm,
        diagnostic: Some(diagnostic),
    }
}

/// # Safety
/// Owner and data are live; no table loan survives a cache/SD mutation.
#[no_mangle]
pub unsafe extern "C" fn xp_font_registry_source(
    owner: *const Owner,
    font: *const FontData,
    out: *mut BitmapSource,
) {
    ptr::write_bytes(out, 0, 1);
    let generation = (*owner).registry.generation;
    (*out).generation = generation;
    (*out).captured_generation = (*generation).load(core::sync::atomic::Ordering::Relaxed);
    (*out).font = font;
    if (*font).vector_bitmap_handler.is_some() {
        return;
    }
    if (*font).interval_count != 0 {
        let last = (*font)
            .intervals
            .add((*font).interval_count as usize - 1)
            .read();
        (*out).glyph_count = last
            .offset
            .wrapping_add(last.last.wrapping_sub(last.first))
            .wrapping_add(1);
        if (*font).groups.is_null() && (*out).glyph_count != 0 {
            let glyph = (*font).glyph.add((*out).glyph_count as usize - 1).read();
            (*out).bitmap_len = glyph.data_offset.wrapping_add(u32::from(glyph.data_length));
        }
    }
    let cache = (&*(*owner).cache.get()).cache();
    (*out).slots = cache.slots.as_ptr().cast();
    (*out).slot_count = u32::from(cache.count);
    if !(*font).glyph_miss_ctx.is_null() {
        #[cfg(feature = "app-test")]
        if crate::font_sd::is_native_test_face(owner, font) {
            xp_native_sd_font_view((*owner).native, 0, ptr::null_mut(), out);
            return;
        }
        xp_core::sdfont::owner::FaceOwner::from_context((*font).glyph_miss_ctx)
            .0
            .bitmap_view(font, &mut *out);
    }
}
const _: () = {
    let _: [u8; if cfg!(target_pointer_width = "32") {
        if cfg!(any(test, feature = "sd-font-catalog")) {
            4272
        } else {
            4252
        }
    } else {
        if cfg!(any(test, feature = "sd-font-catalog")) {
            4968
        } else {
            4928
        }
    } + if cfg!(feature = "vector-fonts") {
        if cfg!(target_pointer_width = "32") {
            260
        } else {
            520
        }
    } else {
        0
    }] = [0; size_of::<Owner>()];
    assert!(align_of::<Owner>() == align_of::<usize>());
    assert!(size_of::<QueryResult>() == size_of::<usize>());
};

/// # Safety
/// Storage, stable slot and serialization contracts are declared in xp_font_registry.h.
#[no_mangle]
pub unsafe extern "C" fn xp_font_registry_init(
    out: *mut Owner,
    generation: *const AtomicU32,
    native: *mut c_void,
) {
    Registry::initialize(ptr::addr_of_mut!((*out).registry), generation);
    ptr::addr_of_mut!((*out).native).write(native);
    xp_core::font::runtime::FontCacheOwner::initialize((*out).cache.get());
    (*out)
        .sd
        .get()
        .write(xp_core::sdfont::control::Control::new(
            &crate::font_sd::MEMORY,
        ));
    #[cfg(feature = "vector-fonts")]
    crate::font_vector::initialize(out);
    ptr::addr_of_mut!((*out).intervals).write(xp_core::sdfont::parser::IntervalRegistry::new());
    #[cfg(any(test, feature = "sd-font-catalog"))]
    ptr::addr_of_mut!((*out).catalog).write(crate::font_sd::catalog::State::new());
}
/// # Safety
/// Owner is exclusive; no font/source/cache loan survives its destruction.
#[no_mangle]
pub unsafe extern "C" fn xp_font_registry_drop(owner: *mut Owner) {
    #[cfg(feature = "vector-fonts")]
    crate::font_vector::destroy(owner);
    crate::font_sd::control(owner).unload(ptr::addr_of_mut!((*owner).registry), None);
    ptr::drop_in_place((*owner).sd.get());
    #[cfg(any(test, feature = "sd-font-catalog"))]
    ptr::drop_in_place(ptr::addr_of_mut!((*owner).catalog));
}
/// # Safety
/// `owner` is initialized and exclusive; `entry` is readable for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_font_registry_update(
    owner: *mut Owner,
    operation: u32,
    entry: *const Entry,
) -> u8 {
    #[cfg(any(test, feature = "sd-font-catalog"))]
    if operation == 4 {
        return u8::from(Owner::ensure(owner, (*entry).id));
    }
    let registry = &mut (*owner).registry;
    match operation {
        0 => u8::from(registry.insert(*entry)),
        1 => {
            registry.remove((*entry).id);
            1
        }
        2 => {
            registry.set_fallback((*entry).id, (*entry).fallback);
            1
        }
        3 => {
            registry.clear_fallbacks();
            1
        }
        _ => 0,
    }
}
/// # Safety
/// `owner` is initialized and shared; `out` is exclusive, disjoint output storage.
#[no_mangle]
pub unsafe extern "C" fn xp_font_registry_query(
    owner: *const Owner,
    query: u32,
    id: i32,
    index: u32,
    out: *mut Entry,
) -> QueryResult {
    match query {
        6 => QueryResult {
            value: usize::from(crate::font_sd::control(owner).face(id).is_some()),
        },
        2 => QueryResult {
            value: (*owner).registry.len(),
        },
        3 => QueryResult {
            slot: Owner::slot(owner, id, index as u8),
        },
        4 => QueryResult {
            value: usize::from((*owner).registry.contains(id)),
        },
        5 => QueryResult {
            value: (*owner).registry.fallback(id) as u32 as usize,
        },
        _ => QueryResult {
            value: query_descriptor(owner, query, id, index, out),
        },
    }
}

// Keep descriptor copies off the shared scalar/slot query frame.
#[inline(never)]
unsafe fn query_descriptor(
    owner: *const Owner,
    query: u32,
    id: i32,
    index: u32,
    out: *mut Entry,
) -> usize {
    let registry = &(*owner).registry;
    let entry = match query {
        0 => {
            for index in 0..registry.len() {
                let entry = registry.at(index).unwrap();
                if entry.id == id {
                    out.write(entry);
                    return index + 1;
                }
            }
            return 0;
        }
        1 => registry.at(index as usize),
        _ => None,
    };
    if let Some(value) = entry {
        out.write(value);
        1
    } else {
        0
    }
}
