//! Exercise the Rust owner and historical manager front door with two registry owners.
#![allow(dead_code)]
#[path = "../../xp-ffi/src/font.rs"]
#[allow(unexpected_cfgs)]
mod font;
#[path = "../../xp-ffi/src/font_registry.rs"]
#[allow(unexpected_cfgs)]
mod font_registry;
#[path = "../../xp-ffi/src/font_runtime.rs"]
#[allow(unexpected_cfgs)]
mod font_runtime;
#[path = "../../xp-ffi/src/font_sd.rs"]
#[allow(unexpected_cfgs)]
mod font_sd;

use core::{
    ffi::c_void,
    ptr,
    sync::atomic::{AtomicU32, AtomicUsize, Ordering},
};
use font_registry::{xp_font_registry_init, Owner};

#[path = "support/font_catalog.rs"]
mod catalog_cases;
#[cfg(feature = "sd-font-catalog")]
#[path = "../../xp-ffi/src/system_items.rs"]
mod system_items;
#[cfg(feature = "sd-font-catalog")]
#[path = "../../xp-ffi/src/web.rs"]
#[allow(unexpected_cfgs)]
mod web;
#[cfg(feature = "sd-font-catalog")]
#[path = "support/font_web.rs"]
mod web_cases;

static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);

#[test]
fn another_registry_cannot_redirect_generation_or_a_captured_scope() {
    let generation = AtomicU32::new(10);
    let other_generation = AtomicU32::new(20);
    let mut owner = Box::<Owner>::new_uninit();
    let mut other = Box::<Owner>::new_uninit();
    let owner_ptr = owner.as_mut_ptr();
    let other_ptr = other.as_mut_ptr();
    unsafe {
        // Keep final storage raw after publication, as the native owner does.
        xp_font_registry_init(owner_ptr, &generation, ptr::null_mut());
        let captured = owner_ptr;
        assert!(ptr::eq(captured, owner_ptr));
        font_runtime::begin(captured);
        assert_eq!(generation.load(Ordering::Relaxed), 11);
        assert!(font_runtime::scanning(captured) != 0);

        xp_font_registry_init(other_ptr, &other_generation, ptr::null_mut());

        // An explicit second owner invalidates only its own source generation.
        font_runtime::release(other_ptr, 0);
        assert_eq!(generation.load(Ordering::Relaxed), 11);
        assert_eq!(other_generation.load(Ordering::Relaxed), 21);
        font_runtime::release(owner_ptr, 0);
        assert_eq!(generation.load(Ordering::Relaxed), 12);
        assert_eq!(other_generation.load(Ordering::Relaxed), 21);

        // Reader storage captures this loan at begin and reuses it at finish/end.
        font_runtime::finish(captured);
        font_runtime::end(captured);
        assert_eq!(font_runtime::scanning(captured), 0);
        assert_eq!(generation.load(Ordering::Relaxed), 13);
        assert_eq!(other_generation.load(Ordering::Relaxed), 21);
        assert_eq!(ALLOCATIONS.load(Ordering::Relaxed), 0);
        font_registry::xp_font_registry_drop(owner_ptr);
        font_registry::xp_font_registry_drop(other_ptr);
    }
}

#[no_mangle]
unsafe extern "C" fn fiFontMalloc(_: usize) -> *mut c_void {
    ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
    ptr::null_mut()
}
#[no_mangle]
unsafe extern "C" fn fiFontFree(bytes: *mut c_void) {
    assert!(bytes.is_null());
}
#[no_mangle]
unsafe extern "C" fn esp_timer_get_time() -> i64 {
    0
}
#[no_mangle]
unsafe extern "C" fn heap_caps_get_free_size(_: u32) -> usize {
    0
}

// Empty registries cannot reach the native font/SD/logger services in this test.
macro_rules! unused_native {
    ($name:ident($($arg:ident: $kind:ty),* $(,)?) $(-> $result:ty)?) => {
        #[no_mangle]
        unsafe extern "C" fn $name($($arg: $kind),*) $(-> $result)? {
            $(let _ = $arg;)*
            unreachable!(stringify!($name))
        }
    };
}
unused_native!(xp_native_font_compat_resolve(native: *mut c_void, font: i32, style: u8) -> u8);
unused_native!(
    xp_native_font_compat_prewarm(
        native: *mut c_void,
        font: i32,
        backend: u8,
        text: *const core::ffi::c_char,
        styles: u8,
        metadata: u8,
        kern: u8,
        accumulate: u8,
    ) -> i32
);
unused_native!(
    xp_native_font_compat_clear(native: *mut c_void, font: i32, backend: u8, resident: u8)
);
unused_native!(xp_native_font_compat_reset(native: *mut c_void, font: i32));
unused_native!(
    xp_native_font_cache_diagnostic(
        font: *const xp_core::font::raw::FontData,
        event: u32,
        a: u32,
        b: u32,
        c: u32,
        d: u32,
    )
);
unused_native!(
    xp_native_sd_font_bitmap(
        font: *const xp_core::font::raw::FontData,
        glyph: *const xp_core::font::raw::Glyph,
    ) -> *const u8
);
unused_native!(
    xp_native_sd_font_view(
        native: *mut c_void,
        font: i32,
        family: *mut xp_core::textm::native::Family,
        source: *mut xp_core::glyph::BitmapSource,
    )
);
unused_native!(
    xp_native_sd_font_resident_slice(
        native: *mut c_void,
        font: i32,
        style: u8,
        text: *const u8,
        len: usize,
        metadata: u8,
    )
);
unused_native!(xp_native_sd_font_load(native: *mut c_void, font: i32) -> u8);
unused_native!(xp_native_font_diagnostic(native: *mut c_void, event: u32, value: u32));
unused_native!(xp_native_bidi_acquire() -> *mut xp_core::bidi::Workspace);
unused_native!(xp_native_bidi_release());

#[no_mangle]
unsafe extern "C" fn xp_native_chapter_font_owner(context: *mut c_void) -> *const Owner {
    context.cast()
}
#[no_mangle]
unsafe extern "C" fn heap_caps_get_largest_free_block(_: u32) -> usize {
    catalog_cases::largest()
}

thread_local! {
    static SD_ALLOCATIONS: std::cell::RefCell<Vec<(*mut u8, std::alloc::Layout)>> = const {
        std::cell::RefCell::new(Vec::new())
    };
}
const SD_BYTES: [u8; 152] = {
    let mut bytes = [0; 152];
    let magic = *b"CPFONT\0\0";
    let mut i = 0;
    while i < 8 {
        bytes[i] = magic[i];
        i += 1;
    }
    bytes[8] = 4;
    bytes[12] = 2;
    let mut style = 0;
    while style < 2 {
        let toc = 32 + style * 32;
        bytes[toc] = if style == 0 { 0 } else { 3 };
        bytes[toc + 4] = 1;
        bytes[toc + 8] = 2;
        bytes[toc + 17] = 1;
        bytes[toc + 19] = 1;
        bytes[toc + 24] = 96;
        style += 1;
    }
    bytes[96] = 65;
    bytes[100] = 66;
    bytes
};

#[no_mangle]
unsafe extern "C" fn xp_native_sd_allocate(bytes: usize, domain: u8) -> *mut u8 {
    if catalog_cases::fail_alloc(domain) {
        return ptr::null_mut();
    }
    let layout =
        std::alloc::Layout::from_size_align(bytes.max(1), core::mem::align_of::<usize>()).unwrap();
    let data = std::alloc::alloc(layout);
    if !data.is_null() {
        SD_ALLOCATIONS.with(|live| live.borrow_mut().push((data, layout)));
    }
    data
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_free(data: *mut u8, _: bool) {
    if !data.is_null() {
        SD_ALLOCATIONS.with(|live| {
            let mut live = live.borrow_mut();
            let index = live.iter().position(|(p, _)| *p == data).unwrap();
            let (_, layout) = live.swap_remove(index);
            std::alloc::dealloc(data, layout);
        });
    }
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_handle_init(slot: *mut xp_core::sdfont::io::Slot) {
    slot.cast::<usize>().write(0);
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_handle_destroy(_: *mut xp_core::sdfont::io::Slot) {}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_open(
    slot: *mut xp_core::sdfont::io::Slot,
    _: *const core::ffi::c_char,
    size: *mut u32,
) -> u8 {
    if catalog_cases::fail_io() {
        return 0;
    }
    slot.cast::<usize>().write(0);
    *size = SD_BYTES.len() as u32;
    1
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_read(
    slot: *mut xp_core::sdfont::io::Slot,
    bytes: *mut u8,
    length: usize,
) -> i32 {
    if catalog_cases::fail_io() {
        return 0;
    }
    let position = slot.cast::<usize>().read();
    let count = length.min(SD_BYTES.len().saturating_sub(position));
    ptr::copy_nonoverlapping(SD_BYTES.as_ptr().add(position), bytes, count);
    slot.cast::<usize>().write(position + count);
    count as i32
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_seek(
    slot: *mut xp_core::sdfont::io::Slot,
    offset: u32,
) -> u8 {
    if catalog_cases::fail_io() || offset as usize > SD_BYTES.len() {
        return 0;
    }
    slot.cast::<usize>().write(offset as usize);
    1
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_close(_: *mut xp_core::sdfont::io::Slot) {}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_diagnostic(
    _: *const core::ffi::c_char,
    _: u32,
    _: *const u32,
    _: u32,
    _: u32,
    _: bool,
    _: *const core::ffi::c_char,
) {
}

#[test]
fn inline_family_name_survives_queries_and_extra_size_load() {
    retained_family_name(c"ShortFont");
}
#[test]
fn heap_family_name_survives_queries_and_extra_size_load() {
    retained_family_name(c"FamilyNameLongEnoughForSSOGrowth");
}
fn retained_family_name(expected: &core::ffi::CStr) {
    use core::ffi::CStr;
    use xp_core::font::{
        manager::Family,
        registry::{File, Files},
    };
    unsafe extern "C" fn file(_: *const c_void, index: usize) -> File {
        File {
            name: ptr::null(),
            size: [14, 18][index],
            style: 0,
        }
    }
    let generation = AtomicU32::new(0);
    let mut storage = Box::<Owner>::new_uninit();
    let owner = storage.as_mut_ptr();
    let family = Family {
        name: expected.as_ptr(),
        name_len: expected.to_bytes().len(),
        root: c"/fonts".as_ptr(),
        files: Files {
            ctx: ptr::null(),
            count: 2,
            get: file,
        },
        loose: false,
    };
    unsafe {
        xp_font_registry_init(owner, &generation, ptr::null_mut());
        assert_eq!(font_sd::xp_sd_font_load(owner, &family, 14, 101, 0), 101);
        let provider = font_registry::xp_font_registry_provider(owner);
        let slot = core::mem::transmute::<
            font_registry::QueryResult,
            *const *const xp_core::font::raw::FontData,
        >(font_registry::xp_font_registry_query(
            owner,
            3,
            101,
            0,
            ptr::null_mut(),
        ));
        let data = slot.read();
        assert!(!data.is_null());
        let mut points = 0;
        let name = font_sd::xp_sd_font_current(owner, &mut points);
        assert_eq!(points, 14);
        assert_eq!(CStr::from_ptr(name), expected);
        assert_eq!(
            font_sd::xp_sd_font_id(owner, name, expected.to_bytes().len(), 0),
            101
        );
        assert_eq!(font_sd::xp_sd_font_load(owner, &family, 18, 102, 1), 102);
        assert_eq!(CStr::from_ptr(name), expected);
        assert_eq!(
            font_sd::xp_sd_font_id(owner, name, expected.to_bytes().len(), 14),
            101
        );
        assert_eq!(
            font_sd::xp_sd_font_id(owner, name, expected.to_bytes().len(), 18),
            102
        );
        assert_eq!(provider.context, owner.cast());
        assert_eq!(provider.generation, &generation as *const AtomicU32);
        assert_eq!(slot.read(), data);
        let mut view = core::mem::zeroed();
        (provider.family)(provider.context, 101, &mut view);
        assert_eq!(view.styles[0].data, slot.read());
        assert_eq!(CStr::from_ptr(name), expected);
        font_registry::xp_font_registry_drop(owner);
    }
    SD_ALLOCATIONS.with(|live| assert!(live.borrow().is_empty()));
}

#[no_mangle]
unsafe extern "C" fn xp_native_sd_directory_open(
    slot: *mut xp_core::sdfont::io::Slot,
    path: *const core::ffi::c_char,
) -> i32 {
    catalog_cases::directory_open(slot, path)
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_directory_next(
    slot: *mut xp_core::sdfont::io::Slot,
    name: *mut u8,
    directory: *mut bool,
    _: bool,
) -> bool {
    catalog_cases::directory_next(slot, name, directory)
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_path_exists(_: *const core::ffi::c_char) -> bool {
    false
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_inspect(
    _: *const core::ffi::c_char,
    _: *mut u16,
    _: *mut bool,
) -> bool {
    false
}

#[cfg(feature = "sd-font-catalog")]
unused_native!(xp_plugin_path_candidate(path: *const u8, len: usize) -> bool);
