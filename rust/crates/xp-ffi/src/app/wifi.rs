use super::{abi, app, keyboard, App, StableOwner, Transition};
use core::{
    cell::Cell,
    ffi::{c_void, CStr},
    ptr::NonNull,
};
use xp_core::i18n::{self, ids};
use xp_ui::{
    ffi::{Paint, SceneView},
    model::{Input, TOUCH_RELEASED},
    network::{
        wifi_owner::{self, Host},
        wifi_physical::Physical,
        wifi_resources::Global,
    },
    scene::{Refresh, Scene},
};
extern "C" {
    static XP_WIFI_PHYSICAL: Physical;
}
pub(super) type Wifi = wifi_owner::Wifi<NativeHost>;
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
    fn facts(&self) -> abi::LegacyFacts {
        let facts = unsafe { (self.app.legacy.facts)(self.object.as_ptr(), 0, 0) };
        debug_assert_eq!(facts.kind, abi::WIFI);
        facts
    }
}
unsafe impl Host for NativeHost {
    type Memory = Global;
    #[cfg(test)]
    fn allocate_owner(layout: core::alloc::Layout) -> *mut u8 {
        tests::allocate_owner(layout)
    }
    fn physical(&self) -> &Physical {
        unsafe { &XP_WIFI_PHYSICAL }
    }
    fn object(&self) -> *mut c_void {
        self.object.as_ptr()
    }
    fn model_lock(&self) {
        unsafe { (self.app.runtime.model_lock)() };
    }
    fn model_unlock(&self) {
        unsafe { (self.app.runtime.model_unlock)() };
    }
    fn render_lock(&self) -> *mut c_void {
        unsafe { (self.app.runtime.render_lock)() }
    }
    fn render_unlock(&self, token: *mut c_void) {
        unsafe { (self.app.runtime.render_unlock)(token) };
    }
    fn enter(&self) {
        unsafe { (self.app.legacy.enter)(self.object.as_ptr()) };
    }
    fn exit(&self) {
        unsafe { (self.app.legacy.exit)(self.object.as_ptr()) };
    }
    fn now(&self) -> u32 {
        unsafe { (self.app.runtime.now_ms)() }
    }
    fn delay(&self, ms: u32) {
        unsafe { (self.app.runtime.delay_ms)(ms) };
    }
    fn sample_input(&self, with_long_press: bool) -> (Input, bool) {
        let (mut x, mut y) = (0, 0);
        let long = with_long_press
            && unsafe { (self.app.epub.input_screen_point)(0, &mut x, &mut y) } != 0;
        let mut input = Input::default();
        unsafe { (self.app.epub.input_snapshot)(&mut input) };
        if long {
            input.touch_flags = TOUCH_RELEASED;
            input.touch_x = x as i16;
            input.touch_y = y as i16;
        }
        (input, long)
    }
    fn request_update(&self) {
        self.app.request(false);
    }
    fn haptic(&self, long: bool) {
        unsafe { (self.app.runtime.touch_feedback)(long.into()) };
    }
    fn complete(&self) {
        self.app.input.borrow_mut().reset();
        self.app.navigation.queue(Transition::Pop);
    }
    fn keyboard(&self, hidden: bool) -> bool {
        let mut layout = core::mem::MaybeUninit::uninit();
        unsafe { (self.physical().layout)(self.object(), layout.as_mut_ptr()) };
        let language = unsafe { layout.assume_init() }.language;
        let title = unsafe {
            CStr::from_ptr(i18n::get(
                language,
                if hidden {
                    ids::STR_ENTER_WIFI_SSID
                } else {
                    ids::STR_ENTER_WIFI_PASSWORD
                },
            ))
        }
        .to_bytes();
        keyboard::launch(&abi::KeyboardLaunch {
            title: abi::Text::from_bytes(title),
            initial: abi::Text::from_bytes(&[]),
            maximum: if hidden { 32 } else { 64 },
            input_type: 0,
        })
    }
    #[inline(never)]
    fn paint(&self, scene: &Scene<'_>, resources: &Paint) -> bool {
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
        unsafe { (self.app.resources.paint_scene)(&wire, resources) != 0 }
    }
    fn refresh(&self, refresh: Refresh) {
        unsafe { (self.app.resources.refresh)(refresh as u8) };
    }
}
#[inline(never)]
pub(super) fn create(object: NonNull<c_void>) -> Option<StableOwner<Wifi>> {
    let owner = Wifi::try_new(NativeHost {
        app: app()?,
        object,
        adopted: Cell::new(false),
    })?;
    let owner = StableOwner::from(owner);
    owner.host().adopted.set(true);
    Some(owner)
}
#[inline(never)]
pub(super) fn object(target: NonNull<Wifi>) -> NonNull<c_void> {
    unsafe { target.as_ref() }.host().object
}
#[inline(never)]
pub(super) fn facts(target: NonNull<Wifi>) -> abi::LegacyFacts {
    unsafe { target.as_ref() }.host().facts()
}
#[inline(never)]
pub(super) fn enter(target: NonNull<Wifi>) {
    unsafe { target.as_ref() }.enter();
}
#[inline(never)]
pub(super) fn exit(target: NonNull<Wifi>) {
    unsafe { target.as_ref() }.exit();
}
#[inline(never)]
pub(super) fn poll(target: NonNull<Wifi>) {
    unsafe { target.as_ref() }.poll();
}
#[inline(never)]
pub(super) fn render(target: NonNull<Wifi>) -> bool {
    unsafe { target.as_ref() }.render()
}
#[inline(never)]
pub(super) fn keyboard_returned(target: NonNull<Wifi>, result: Option<keyboard::Result>) {
    #[cfg(test)]
    tests::received(target, result.as_ref().and_then(keyboard::Result::text));
    unsafe { target.as_ref() }.keyboard_returned(result.as_ref().and_then(keyboard::Result::text));
}
#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<NativeHost>() == 12);
    assert!(core::mem::size_of::<Wifi>() == 744);
    assert!(core::mem::align_of::<Wifi>() == 4);
};
#[cfg(test)]
pub(super) mod tests;
