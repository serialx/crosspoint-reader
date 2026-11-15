use super::abi;
use xp_ui::ffi::{Paint, SceneView};

/// The render lock keeps the current target and resource bindings live for this call.
pub(super) unsafe fn paint(
    _services: &abi::Resources,
    scene: *const SceneView,
    resources: *const Paint,
) -> u8 {
    // Historical owner fixtures observe scene publication through their frozen adapter.
    #[cfg(any(test, feature = "app-test"))]
    return unsafe { (_services.paint_scene)(scene, resources) };
    #[cfg(not(any(test, feature = "app-test")))]
    unsafe {
        paint_current(scene, resources)
    }
}

/// # Safety
/// The current renderer, scene and resources obey the synchronous scene loan contract.
pub(crate) unsafe fn paint_current(scene: *const SceneView, resources: *const Paint) -> u8 {
    let mut target = core::mem::MaybeUninit::uninit();
    let mut paint = core::mem::MaybeUninit::uninit();
    unsafe {
        let cache_bound = crate::page_render::direct::xp_native_render_target(
            target.as_mut_ptr(),
            paint.as_mut_ptr(),
        );
        let target = target.assume_init_mut();
        if cache_bound != 0 && !target.fonts.is_null() {
            target.glyphs.scanning = crate::font_runtime::scanning(target.fonts);
        }
        xp_ui::ffi::render(scene, &target.glyphs, paint.as_ptr(), resources)
    }
}

#[cfg(feature = "app-test")]
#[no_mangle]
/// # Safety
/// Same synchronous scene loans as paint_current.
pub unsafe extern "C" fn xp_app_test_scene_paint(
    scene: *const SceneView,
    resources: *const Paint,
) -> u8 {
    unsafe { paint_current(scene, resources) }
}
