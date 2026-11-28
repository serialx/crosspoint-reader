use super::super::abi::XpEpubLeaves;
use xp_layout::para::Allocator;

pub(super) unsafe fn table(_leaves: &XpEpubLeaves) -> &'static Allocator {
    #[cfg(feature = "app-test")]
    {
        &*(_leaves.build_allocator)()
    }
    #[cfg(not(feature = "app-test"))]
    {
        &ALLOCATOR
    }
}

#[cfg(not(feature = "app-test"))]
unsafe fn physical() -> &'static Allocator {
    // Reader paragraph lifetime is inside the initialized, process-lifetime app.
    &*(crate::app::app().unwrap_unchecked().epub.build_allocator)()
}

#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn allocate(bytes: usize, word: u8) -> *mut u8 {
    (physical().allocate)(bytes, word)
}
#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn free(data: *mut u8) {
    (physical().release)(data)
}
#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn diagnostic(
    context: *mut core::ffi::c_void,
    event: u32,
    bytes: u32,
    b: u32,
    c: u32,
    d: u32,
) {
    if let Some(log) = physical().diagnostic {
        log(context, event, bytes, b, c, d);
    }
}

#[cfg(not(feature = "app-test"))]
static ALLOCATOR: Allocator = Allocator {
    allocate,
    release: free,
    diagnostic: Some(diagnostic),
    reclaim: Some(reclaim),
};

#[cfg(not(feature = "app-test"))]
unsafe extern "C" fn reclaim(bytes: usize) {
    use crate::font_sd::heap_caps_get_free_size;
    // Same capability and sink order as the renderer's SDK MemoryManager sinks.
    let have = heap_caps_get_free_size(1 << 12);
    if have >= bytes {
        return;
    }
    let Some(app) = crate::app::app() else { return };
    let mut geometry = core::mem::MaybeUninit::uninit();
    (app.epub.render_geometry)(geometry.as_mut_ptr());
    xp_layout::para::reclaim_caches(
        bytes,
        have,
        geometry.assume_init().has_font_cache != 0,
        cfg!(feature = "vector-fonts"),
        |operation| crate::font_runtime::release(app.resources.font_owner.get().cast(), operation),
    );
    // ensureFree performs this final query even though its caller ignores the result.
    let _ = heap_caps_get_free_size(1 << 12);
}
