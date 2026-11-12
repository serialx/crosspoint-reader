//! Exact production manager/registry/cache with fake physical leaves for Miri.
#![cfg(test)]
#![allow(dead_code, unexpected_cfgs)]
#[path = "../../../rust/crates/xp-ffi/src/font.rs"]
mod font;
#[path = "../../../rust/crates/xp-ffi/src/font_registry.rs"]
mod font_registry;
#[path = "../../../rust/crates/xp-ffi/src/font_runtime.rs"]
mod font_runtime;
#[path = "../../../rust/crates/xp-ffi/src/font_sd.rs"]
mod font_sd;
#[path = "../../../rust/crates/xp-ffi/src/font_vector.rs"]
mod font_vector;

use core::{
    ffi::{c_char, c_void, CStr},
    mem, ptr,
    sync::atomic::{AtomicU32, Ordering},
};
use font_registry::Owner;
use std::{
    alloc::{alloc, dealloc, Layout},
    cell::RefCell,
};
use xp_core::{
    font::raw::{FontData, Glyph},
    sdfont::io::Slot,
};

#[derive(Default)]
struct Harness {
    blocks: Vec<(*mut u8, Layout, u8)>,
    handles: usize,
    impls: usize,
    impl_peak: usize,
    source_size: Option<u32>,
    fail_open: bool,
    fail_read: bool,
    faces: usize,
    opens: usize,
    closes: usize,
    reads: usize,
    forcing: bool,
    initial_bytes: usize,
    frees_under_pressure: usize,
    default_queries: usize,
    registry: *const Owner,
    dropping: bool,
    drop_epoch: u32,
}
thread_local! { static H: RefCell<Harness> = RefCell::new(Harness::default()); }
fn live(h: &Harness) -> usize {
    h.blocks.iter().map(|(_, l, _)| l.size()).sum()
}
fn allocate(bytes: usize, kind: u8) -> *mut u8 {
    let layout = Layout::from_size_align(bytes.max(1), 16).unwrap();
    let data = unsafe { alloc(layout) };
    assert!(!data.is_null());
    H.with_borrow_mut(|h| h.blocks.push((data, layout, kind)));
    data
}
unsafe fn release(data: *mut u8) {
    if data.is_null() {
        return;
    }
    let layout = H.with_borrow_mut(|h| {
        let index = h
            .blocks
            .iter()
            .position(|(p, _, _)| *p == data)
            .expect("live allocation");
        let (_, layout, _) = h.blocks.swap_remove(index);
        if h.forcing {
            h.frees_under_pressure += 1;
        }
        layout
    });
    dealloc(data, layout);
}
#[repr(C)]
struct File {
    kind: usize,
    cursor: usize,
    faces: usize,
}
unsafe fn file<'a>(p: *mut Slot) -> &'a mut File {
    &mut *p.cast()
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_handle_init(p: *mut Slot) {
    assert!(mem::size_of::<File>() <= mem::size_of::<Slot>());
    p.cast::<File>().write(File {
        kind: 0,
        cursor: 0,
        faces: 0,
    });
    H.with_borrow_mut(|h| h.handles += 1);
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_handle_destroy(p: *mut Slot) {
    if file(p).kind != 0 {
        xp_native_sd_file_close(p);
        H.with_borrow_mut(|h| h.impls -= 1);
    }
    H.with_borrow_mut(|h| h.handles -= 1);
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_close(p: *mut Slot) {
    let f = file(p);
    assert_ne!(f.kind, 0, "impl != nullptr");
    assert_eq!(f.faces, 0, "source closed with a live FT borrower");
    if f.kind == 3 {
        H.with_borrow_mut(|h| h.closes += 1);
    }
    f.kind = 4; // Closed, but Impl remains owned until destruction.
}
unsafe fn replace_file(p: *mut Slot, kind: usize) {
    if file(p).kind != 0 {
        xp_native_sd_file_close(p);
        H.with_borrow_mut(|h| h.impls -= 1);
    }
    *file(p) = File {
        kind,
        cursor: 0,
        faces: 0,
    };
    H.with_borrow_mut(|h| {
        h.impls += 1;
        h.impl_peak = h.impl_peak.max(h.impls);
    });
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_open(p: *mut Slot, _: *const c_char, size: *mut u32) -> u8 {
    let ok = H.with_borrow(|h| !h.fail_open);
    replace_file(p, if ok { 3 } else { 4 });
    if ok {
        *size = H.with_borrow(|h| h.source_size.unwrap_or(6 * 1024 * 1024 + 1));
        H.with_borrow_mut(|h| h.opens += 1);
    }
    u8::from(ok)
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_seek(p: *mut Slot, offset: u32) -> u8 {
    assert_eq!(file(p).kind, 3);
    file(p).cursor = offset as usize;
    1
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_file_read(p: *mut Slot, data: *mut u8, bytes: usize) -> i32 {
    assert_eq!(file(p).kind, 3);
    if H.with_borrow(|h| h.fail_read) {
        return -1;
    }
    ptr::write_bytes(data, 42, bytes);
    file(p).cursor += bytes;
    H.with_borrow_mut(|h| h.reads += 1);
    bytes as i32
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_directory_open(p: *mut Slot, path: *const c_char) -> i32 {
    let kind = match CStr::from_ptr(path).to_bytes() {
        b"/.fonts" => 1,
        b"/.fonts/PTVec" => 2,
        _ => 0,
    };
    replace_file(p, if kind == 0 { 4 } else { kind });
    i32::from(kind != 0)
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_directory_next(
    p: *mut Slot,
    name: *mut u8,
    directory: *mut bool,
    _: bool,
) -> bool {
    let f = file(p);
    assert_ne!(f.kind, 0, "impl != nullptr");
    if f.cursor != 0 || f.kind == 4 {
        return false;
    }
    f.cursor += 1;
    *directory = f.kind == 1;
    let text: &[u8] = if *directory {
        b"PTVec\0"
    } else {
        b"NotoSans-Regular.ttf\0"
    };
    ptr::copy_nonoverlapping(text.as_ptr(), name, text.len());
    true
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_path_exists(_: *const c_char) -> bool {
    false
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_inspect(
    _: *const c_char,
    weight: *mut u16,
    italic: *mut bool,
) -> bool {
    *weight = 400;
    *italic = false;
    true
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_allocate(bytes: usize, domain: u8) -> *mut u8 {
    allocate(bytes, domain)
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_free(data: *mut u8, _: bool) {
    release(data);
}
#[no_mangle]
unsafe extern "C" fn fiFontMalloc(bytes: usize) -> *mut c_void {
    allocate(bytes, 3).cast()
}
#[no_mangle]
unsafe extern "C" fn fiFontFree(data: *mut c_void) {
    release(data.cast());
}
#[no_mangle]
unsafe extern "C" fn heap_caps_get_largest_free_block(_: u32) -> usize {
    H.with_borrow(|h| {
        if h.forcing && h.initial_bytes.saturating_sub(live(h)) < 16 * 1024 {
            0
        } else {
            8 * 1024 * 1024
        }
    })
}
#[no_mangle]
unsafe extern "C" fn heap_caps_get_free_size(caps: u32) -> usize {
    H.with_borrow_mut(|h| {
        if h.forcing && caps == 1 << 12 {
            h.default_queries += 1;
            h.initial_bytes.saturating_sub(live(h))
        } else if h.forcing {
            0
        } else {
            8 * 1024 * 1024
        }
    })
}
#[no_mangle]
unsafe extern "C" fn esp_timer_get_time() -> i64 {
    0
}
#[no_mangle]
unsafe extern "C" fn xp_native_sd_diagnostic(
    _: *const c_char,
    _: u32,
    _: *const u32,
    _: u32,
    _: u32,
    _: bool,
    _: *const c_char,
) {
}
#[no_mangle]
unsafe extern "C" fn xp_native_font_cache_diagnostic(
    _: *const FontData,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
    _: u32,
) {
}
#[no_mangle]
unsafe extern "C" fn xp_native_font_diagnostic(_: *mut c_void, _: u32, _: u32) {}
#[no_mangle]
unsafe extern "C" fn xp_native_chapter_font_owner(context: *mut c_void) -> *const Owner {
    context.cast()
}
#[no_mangle]
unsafe extern "C" fn xp_native_bidi_acquire(_size: usize, _alignment: usize) -> *mut xp_core::bidi::Workspace {
    panic!("unused bidi leaf")
}
#[no_mangle]
unsafe extern "C" fn xp_native_bidi_release() {
    panic!("unused bidi leaf")
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Source {
    mode: u8,
    data: *const u8,
    length: u32,
    file: *mut Slot,
    size: u32,
}
struct Ft {
    source: Source,
    pixels: [u8; 64],
}
unsafe fn ft<'a>(p: *mut c_void) -> &'a mut Ft {
    &mut **p.cast::<*mut Ft>()
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_open(
    p: *mut c_void,
    source: *const Source,
    _: *const c_void,
) -> bool {
    assert_eq!(*(*source).data, 42);
    if (*source).mode == 2 {
        assert_eq!(file((*source).file).kind, 3);
        file((*source).file).faces += 1;
    } else {
        assert_eq!((*source).mode, 1);
        assert!((*source).file.is_null());
    }
    p.cast::<*mut Ft>().write(Box::into_raw(Box::new(Ft {
        source: *source,
        pixels: [255; 64],
    })));
    H.with_borrow_mut(|h| h.faces += 1);
    true
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_close(p: *mut c_void) {
    let raw = p.cast::<*mut Ft>().read();
    if (*raw).source.mode == 2 {
        let f = file((*raw).source.file);
        assert_eq!(f.kind, 3);
        f.faces -= 1;
    }
    H.with_borrow_mut(|h| {
        h.faces -= 1;
        if h.dropping {
            assert!(
                (*(*h.registry).registry.generation).load(Ordering::Relaxed) > h.drop_epoch,
                "registry must invalidate before destruction"
            );
        }
    });
    drop(Box::from_raw(raw));
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_glyph_id(p: *mut c_void, cp: u32) -> u32 {
    if ft(p).source.mode == 2 {
        assert_eq!(file(ft(p).source.file).kind, 3);
    }
    cp
}
#[repr(C)]
struct Raster {
    pixels: *const u8,
    width: u16,
    height: u16,
    left: i16,
    yoff: i16,
    advance: i32,
    metrics: i32,
    have_metrics: bool,
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_raster(p: *mut c_void, _: u32, _: u32, out: *mut Raster) {
    let face = ft(p);
    if face.source.mode == 2 {
        assert_eq!(file(face.source.file).kind, 3);
    }
    *out = Raster {
        pixels: face.pixels.as_ptr(),
        width: 8,
        height: 8,
        left: 0,
        yoff: -8,
        advance: 8,
        metrics: 512,
        have_metrics: true,
    };
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_kern(_: *mut c_void, _: u32, _: u32, _: u32) -> i32 {
    0
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_line_metrics(
    _: *mut c_void,
    _: u32,
    _: u16,
    ascent: *mut i32,
    height: *mut i32,
) {
    *ascent = 512;
    *height = 640;
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_ligature(_: *mut c_void, _: *const u32, _: usize) -> u32 {
    0
}
#[no_mangle]
unsafe extern "C" fn xp_native_ft_release_gsub(_: *mut c_void) {}

#[test]
fn production_tables_streams_scan_pressure_and_stale_handles() {
    let generation = AtomicU32::new(1);
    let mut storage = Box::<Owner>::new_uninit();
    let owner = storage.as_mut_ptr();
    unsafe {
        font_registry::xp_font_registry_init(owner, &generation, ptr::null_mut());
        H.with_borrow_mut(|h| h.registry = owner);
        font_sd::catalog::xp_sd_catalog_dirty(owner, 2, true);
        let family = font_sd::catalog::catalog(owner).find(c"PTVec").unwrap();
        assert!(font_vector::load(owner, family, 14, [1, 2, 3], false));
        let id = font_vector::loaded_id(owner, b"PTVec");
        assert_ne!(id, 0);
        let stable = Owner::slot(owner, id, 0);
        let data = stable.read();
        let glyph = font::xp_font_lookup(data, 65).cast::<Glyph>();
        assert!(!glyph.is_null());
        let mut old_source = mem::MaybeUninit::uninit();
        font_registry::xp_font_registry_source(owner, data, old_source.as_mut_ptr());
        let old_source = old_source.assume_init();
        // Deliberately omit begin's reserve(5), forcing the real variant Array to relocate.
        let mut first = 0;
        for scale in [1.5, 1.8, 2.2, 2.6, 2.8, 3.0] {
            let variant = font_vector::variant(owner, id, scale);
            assert_ne!(variant, 0);
            if first == 0 {
                first = variant;
            }
            assert_eq!(Owner::slot(owner, id, 0), stable);
            let data = Owner::slot(owner, variant, 0).read();
            assert!(!font::xp_font_lookup(data, 66).is_null());
        }
        assert!(!(*owner).registry.contains(first));
        assert!(font_vector::ensure(owner, first));
        assert_eq!(Owner::slot(owner, id, 0), stable);
        assert!(generation.load(Ordering::Relaxed) > old_source.captured_generation);
        assert!(old_source.resolve(glyph).is_none());
        font_runtime::begin(owner);
        let text = std::ffi::CString::new((33..128).collect::<Vec<u8>>()).unwrap();
        font_runtime::record_text(owner, id, text.as_ptr(), 0);
        assert_ne!(font_runtime::scanning(owner), 0);
        H.with_borrow_mut(|h| {
            h.initial_bytes = live(h);
            h.forcing = true;
        });
        font_runtime::finish(owner);
        H.with_borrow_mut(|h| {
            assert!(h.default_queries > 0);
            assert!(h.frees_under_pressure > 0);
            h.forcing = false;
        });
        font_runtime::end(owner);
        assert_eq!(font_runtime::scanning(owner), 0);
        // Refill other families before forcing growth with an active raster loan.
        font_runtime::release(owner, 1);
        for other in [first, font_vector::variant(owner, id, 1.8)] {
            let other_data = Owner::slot(owner, other, 0).read();
            assert!(!font::xp_font_lookup(other_data, 65).is_null());
        }
        for cp in 32..96 {
            assert!(!font::xp_font_lookup(data, cp).is_null());
        }
        H.with_borrow_mut(|h| {
            h.initial_bytes = live(h);
            h.forcing = true;
        });
        let next = font::xp_font_lookup(data, 96).cast::<Glyph>();
        assert!(!next.is_null());
        H.with_borrow_mut(|h| {
            assert!(h.default_queries > 0);
            assert!(h.frees_under_pressure > 0);
            h.forcing = false;
        });
        let bitmap = ((*data).vector_bitmap_handler.unwrap())((*data).glyph_miss_ctx, next);
        assert!(!bitmap.is_null());
        assert_eq!(*bitmap, 255);
        // Capture a source immediately before destruction; resolve must reject without dereferencing it.
        let mut stale = mem::MaybeUninit::uninit();
        font_registry::xp_font_registry_source(owner, data, stale.as_mut_ptr());
        let stale = stale.assume_init();
        H.with_borrow_mut(|h| {
            h.dropping = true;
            h.drop_epoch = generation.load(Ordering::Relaxed);
        });
        font_vector::unload(owner);
        assert!((*owner).registry.is_empty());
        assert!(stale.resolve(next).is_none());
        H.with_borrow(|h| {
            assert_eq!(h.faces, 0);
            assert_eq!(h.opens, h.closes);
            assert!(h.reads > 0);
        });
        font_registry::xp_font_registry_drop(owner);
    }
    H.with_borrow(|h| {
        assert_eq!(h.handles, 0);
        assert_eq!(h.impls, 0);
        assert!(h.blocks.is_empty());
    });
}

#[test]
fn unopened_sources_can_be_unloaded_repeatedly() {
    let generation = AtomicU32::new(1);
    let mut storage = Box::<Owner>::new_uninit();
    let owner = storage.as_mut_ptr();
    unsafe {
        font_registry::xp_font_registry_init(owner, &generation, ptr::null_mut());
        font_vector::unload(owner);
        font_vector::unload(owner);
        font_registry::xp_font_registry_drop(owner);
    }
    H.with_borrow(|h| {
        assert_eq!(h.handles, 0);
        assert_eq!(h.impls, 0);
        assert_eq!(h.opens, 0);
        assert_eq!(h.closes, 0);
        assert!(h.blocks.is_empty());
    });
}

#[cfg(not(miri))]
#[test]
fn fake_close_on_default_aborts() {
    if std::env::var_os("PTVEC_DEFAULT_CLOSE_CHILD").is_some() {
        let mut slot = mem::MaybeUninit::<Slot>::uninit();
        unsafe {
            xp_native_sd_handle_init(slot.as_mut_ptr());
            xp_native_sd_file_close(slot.as_mut_ptr());
        }
        return;
    }
    use std::os::unix::process::ExitStatusExt;
    let result = std::process::Command::new(std::env::current_exe().unwrap())
        .args(["--exact", "fake_close_on_default_aborts", "--nocapture"])
        .env("PTVEC_DEFAULT_CLOSE_CHILD", "1")
        .output()
        .unwrap();
    assert_eq!(result.status.signal(), Some(6));
    assert!(String::from_utf8_lossy(&result.stderr).contains("impl != nullptr"));
}

#[test]
fn resident_streamed_failed_and_empty_source_lifetimes() {
    let generation = AtomicU32::new(1);
    let mut storage = Box::<Owner>::new_uninit();
    let owner = storage.as_mut_ptr();
    unsafe {
        font_registry::xp_font_registry_init(owner, &generation, ptr::null_mut());
        font_sd::catalog::xp_sd_catalog_dirty(owner, 2, true);
        let family = font_sd::catalog::catalog(owner).find(c"PTVec").unwrap();
        let baseline = H.with_borrow(|h| h.impls);
        // A previous streamed Impl remains while local resident/failed files come and go.
        for (size, fail_open, fail_read, retained) in [
            (Some(1024), false, false, 0),
            (None, false, false, 1),
            (Some(1024), false, false, 1),
            (Some(0), false, false, 1),
            (Some(1024), true, false, 1),
            (Some(1024), false, true, 1),
            (None, false, false, 1),
        ] {
            H.with_borrow_mut(|h| {
                h.source_size = size;
                h.fail_open = fail_open;
                h.fail_read = fail_read;
                h.impl_peak = h.impls;
            });
            let expected = size != Some(0) && !fail_open && !fail_read;
            assert_eq!(
                font_vector::load(owner, family, 14, [1, 2, 3], true),
                expected
            );
            H.with_borrow(|h| {
                assert_eq!(h.impls, baseline + retained);
                assert!(h.impl_peak <= baseline + 2);
            });
            font_vector::unload(owner);
            font_vector::unload(owner);
            H.with_borrow(|h| {
                assert_eq!(h.impls, baseline + retained);
                assert_eq!(h.opens, h.closes);
                assert_eq!(h.faces, 0);
            });
        }
        font_registry::xp_font_registry_drop(owner);
    }
    H.with_borrow(|h| {
        assert_eq!(h.handles, 0);
        assert_eq!(h.impls, 0);
        assert!(h.blocks.is_empty());
    });
}
