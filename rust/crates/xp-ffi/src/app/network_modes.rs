use super::{abi, app, App, StableOwner, Transition};
use core::{cell::Cell, ffi::c_void, ptr::NonNull};
use xp_core::glyph::GlyphTarget;
use xp_ui::{
    ffi::{Paint, SceneView},
    network::{
        modes_owner::{self, Host},
        usb_view::{FOOTER, HEADER},
    },
    scene::{Command, Rect, Refresh, Scene},
    settings::{
        settings_ffi::{DataRow, Services},
        view::{Layout, Resources},
    },
};

#[repr(C)]
struct Physical {
    layout: unsafe extern "C" fn(*mut c_void, *mut Layout),
    resources: unsafe extern "C" fn(*mut c_void) -> Services,
    paint: unsafe extern "C" fn(*mut c_void, *const Command, Rect),
    scroll_diagnostic: unsafe extern "C" fn(i32, u16),
    scene_diagnostic: unsafe extern "C" fn(u8),
    prepare_result_retire_n8: unsafe extern "C" fn(*mut c_void, u8, u8),
}
extern "C" {
    static XP_NETWORK_MODES_PHYSICAL: Physical;
}

pub(super) type Modes = modes_owner::Modes<NativeHost>;
#[repr(C)]
pub(super) struct NativeHost {
    app: &'static App,
    object: NonNull<c_void>,
    adopted: Cell<bool>,
}
impl Drop for NativeHost {
    fn drop(&mut self) {
        if self.adopted.get() {
            unsafe { (self.app.legacy.destroy)(self.object.as_ptr()) };
        }
    }
}
impl NativeHost {
    pub(super) fn object(&self) -> NonNull<c_void> {
        self.object
    }
    fn facts(&self) -> abi::LegacyFacts {
        unsafe { (self.app.legacy.facts)(self.object.as_ptr(), 0, 0) }
    }
    fn resources(&self) -> Services {
        unsafe { (XP_NETWORK_MODES_PHYSICAL.resources)(self.object.as_ptr()) }
    }
    fn valid(&self, handle: u32, generation: u32) -> bool {
        if generation != 1 {
            return false;
        }
        if matches!(handle, HEADER | FOOTER) {
            return true;
        }
        let Some(h) = handle.checked_sub(1) else {
            return false;
        };
        let resources = self.resources();
        if h / 4 >= u32::from(resources.count) {
            return false;
        }
        if h % 4 == 3 {
            return true;
        }
        let mut row = DataRow::default();
        let Some(read) = resources.row else {
            return false;
        };
        if unsafe { read(resources.context, (h / 4) as u16, &mut row) } == 0 {
            return false;
        }
        match h % 4 {
            0 => !row.label.is_null(),
            1 => !row.subtitle.is_null(),
            _ => false,
        }
    }
}
unsafe impl Host for NativeHost {
    #[cfg(test)]
    fn allocate_owner(layout: core::alloc::Layout) -> *mut u8 {
        tests::allocate_owner(layout)
    }

    fn lock(&self) {
        unsafe { (self.app.runtime.model_lock)() };
    }
    fn unlock(&self) {
        unsafe { (self.app.runtime.model_unlock)() };
    }
    fn enter(&self) {
        unsafe { (self.app.legacy.enter)(self.object.as_ptr()) };
    }
    fn exit(&self) {
        unsafe { (self.app.legacy.exit)(self.object.as_ptr()) };
    }
    fn count(&self) -> u16 {
        self.resources().count
    }
    fn layout(&self) -> Layout {
        let mut out = core::mem::MaybeUninit::uninit();
        unsafe {
            (XP_NETWORK_MODES_PHYSICAL.layout)(self.object.as_ptr(), out.as_mut_ptr());
            out.assume_init()
        }
    }
    fn with_resources<R>(&self, consume: impl FnOnce(&mut dyn Resources) -> R) -> R {
        consume(&mut self.resources())
    }
    #[inline(never)]
    fn paint(&self, scene: &Scene<'_>) -> bool {
        unsafe extern "C" fn valid(context: *mut c_void, handle: u32, generation: u32) -> u8 {
            unsafe { &*context.cast::<NativeHost>() }
                .valid(handle, generation)
                .into()
        }
        unsafe extern "C" fn paint(
            context: *mut c_void,
            _: *const GlyphTarget,
            command: *const Command,
            clip: Rect,
        ) {
            let host = unsafe { &*context.cast::<NativeHost>() };
            unsafe { (XP_NETWORK_MODES_PHYSICAL.paint)(host.object.as_ptr(), command, clip) };
        }
        let wire = SceneView {
            commands: scene.commands.as_ptr(),
            text: core::ptr::null(),
            interactions: core::ptr::null(),
            generation: scene.generation,
            command_count: scene.commands.len() as u16,
            text_len: 0,
            interaction_count: 0,
            refresh: scene.refresh as u8,
            reserved: 0,
        };
        let resources = Paint {
            context: (self as *const Self).cast_mut().cast(),
            clear: None,
            text: None,
            resource_valid: Some(valid),
            resource: Some(paint),
        };
        unsafe { (self.app.resources.paint_scene)(&wire, &resources) != 0 }
    }
    fn refresh(&self, refresh: Refresh) {
        unsafe { (self.app.resources.refresh)(refresh as u8) };
    }
    fn request_update(&self) {
        self.app.request(false);
    }
    fn haptic(&self) {
        unsafe { (self.app.runtime.touch_feedback)(0) };
    }
    fn complete(&self, cancelled: bool, mode: u8) {
        unsafe {
            (XP_NETWORK_MODES_PHYSICAL.prepare_result_retire_n8)(
                self.object.as_ptr(),
                cancelled.into(),
                mode,
            )
        };
        self.app.input.borrow_mut().reset();
        self.app.navigation.queue(Transition::Pop);
    }
    fn scroll_diagnostic(&self, delta: i32) {
        unsafe { (XP_NETWORK_MODES_PHYSICAL.scroll_diagnostic)(delta, self.count()) };
    }
    fn scene_diagnostic(&self, invalid: bool) {
        unsafe { (XP_NETWORK_MODES_PHYSICAL.scene_diagnostic)(invalid.into()) };
    }
}

#[inline(never)]
pub(super) fn create(object: NonNull<c_void>) -> Option<StableOwner<Modes>> {
    let owner = Modes::try_new(NativeHost {
        app: app()?,
        object,
        adopted: Cell::new(false),
    })?;
    let owner = StableOwner::from(owner);
    owner.host().adopted.set(true);
    Some(owner)
}

#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<NativeHost>() == 12);
    assert!(core::mem::size_of::<Modes>() == 1040);
    assert!(core::mem::align_of::<Modes>() == 4);
    assert!(core::mem::size_of::<Physical>() == 24);
};

// Outlined dispatch keeps this screen's scratch out of shared App frames.
#[inline(never)]
pub(super) fn facts(target: NonNull<Modes>) -> abi::LegacyFacts {
    unsafe { target.as_ref() }.host().facts()
}
#[inline(never)]
pub(super) fn object(target: NonNull<Modes>) -> NonNull<c_void> {
    unsafe { target.as_ref() }.host().object()
}
#[inline(never)]
pub(super) fn enter(target: NonNull<Modes>) {
    unsafe { target.as_ref() }.enter();
}
#[inline(never)]
pub(super) fn exit(target: NonNull<Modes>) {
    unsafe { target.as_ref() }.exit();
}
#[inline(never)]
pub(super) fn input(target: NonNull<Modes>, input: xp_ui::model::Input) {
    unsafe { target.as_ref() }.input(input);
}
#[inline(never)]
pub(super) fn render(target: NonNull<Modes>) -> bool {
    unsafe { target.as_ref() }.render()
}

#[cfg(test)]
mod tests;
