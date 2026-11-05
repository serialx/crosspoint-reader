use super::{view, *};
use crate::{
    ffi::{Buffer, SceneView},
    scene::{Arena, Refresh, MAX_COMMAND_BYTES},
};
use core::ffi::c_void;
#[repr(C)]
pub struct Metrics {
    pub context: *mut c_void,
    pub width: Option<unsafe extern "C" fn(*mut c_void, u32, i32, u8) -> i32>,
}
#[repr(C)]
pub struct XtcDiagnostics {
    pub context: *mut c_void,
    pub outside: Option<unsafe extern "C" fn(*mut c_void, i32, i32, u8)>,
}
/// # Safety
/// Live, aligned descriptors; writable command storage and output do not alias inputs.
pub unsafe fn view(
    layout: *const view::Layout,
    metrics: *const Metrics,
    buffer: *const Buffer,
    out: *mut SceneView,
) -> u8 {
    let (Some(l), Some(m), Some(b), Some(o)) = (
        unsafe { layout.as_ref() },
        unsafe { metrics.as_ref() },
        unsafe { buffer.as_ref() },
        unsafe { out.as_mut() },
    ) else {
        return 0;
    };
    if b.commands.is_null() || usize::from(b.command_capacity) > MAX_COMMAND_BYTES / 32 {
        return 0;
    }
    let Some(width) = m.width else {
        return 0;
    };
    let commands =
        unsafe { core::slice::from_raw_parts_mut(b.commands, usize::from(b.command_capacity)) };
    let Ok(mut arena) = Arena::new(commands, &mut [], &mut []) else {
        return 0;
    };
    if view::view(&mut arena, l, &mut |h, f, s| unsafe {
        width(m.context, h, f, s)
    })
    .is_err()
    {
        return 0;
    }
    let refresh = match l.refresh {
        0 => Refresh::None,
        1 => Refresh::Fast,
        2 => Refresh::Half,
        3 => Refresh::Full,
        _ => return 0,
    };
    let Ok(scene) = arena.finish(refresh, 1) else {
        return 0;
    };
    *o = SceneView {
        commands: b.commands,
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 1,
        command_count: scene.commands.len() as u16,
        text_len: 0,
        interaction_count: 0,
        refresh: l.refresh,
        reserved: 0,
    };
    1
}
const _: () = {
    assert!(view::XTC_PIXELS == 12 && view::XTC_PAGE == 8);
    assert!(core::mem::size_of::<SleepPlan>() == 8);
    assert!(core::mem::size_of::<ImageState>() == 12);
    assert!(core::mem::size_of::<XtcState>() == 4);
    assert!(core::mem::size_of::<Placement>() == 16);
    assert!(core::mem::align_of::<Placement>() == 4);
    assert!(core::mem::offset_of!(Placement, crop_x) == 8);
    assert!(core::mem::offset_of!(Placement, crop_y) == 12);
    assert!(core::mem::size_of::<view::Layout>() == 36);
    assert!(core::mem::offset_of!(view::Layout, view) == 30);
};
const _: () = {
    assert!(core::mem::size_of::<Metrics>() == 2 * core::mem::size_of::<*const ()>());
    assert!(core::mem::offset_of!(Metrics, width) == core::mem::size_of::<*const ()>());
    assert!(core::mem::size_of::<XtcOverlay>() == 16);
    assert!(core::mem::size_of::<XtcDiagnostics>() == 2 * core::mem::size_of::<*const ()>());
    assert!(core::mem::align_of::<XtcDiagnostics>() == core::mem::align_of::<*const ()>());
    assert!(core::mem::offset_of!(XtcDiagnostics, outside) == core::mem::size_of::<*const ()>());
    assert!(core::mem::offset_of!(XtcOverlay, draw) == 12);
};
