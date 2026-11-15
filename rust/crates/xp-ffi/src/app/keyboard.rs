use super::{abi, app, App, Owner, StableOwner, Transition};
use core::{
    alloc::Layout as Allocation,
    cell::Cell,
    ffi::c_void,
    ptr::{self, NonNull},
};
use xp_core::glyph::GlyphTarget;
use xp_ui::{
    ffi::{Paint, SceneView},
    keyboard::{
        owner::{self, storage::Memory, Host, Lock, Resources},
        Layouts,
    },
    keyboard_view::{Layout, Metrics, PaintKey},
    model::{Input, TOUCH_RELEASED},
    scene::{Command, Rect, Refresh, Scene},
};

pub(super) use abi::KeyboardLaunch as Launch;
extern "C" {
    fn xp_keyboard_launch(activity: *mut c_void, out: *mut Launch);
    fn xp_keyboard_release_launch(activity: *mut c_void);
    fn xp_keyboard_layouts() -> Layouts;
    fn xp_keyboard_metrics(activity: *mut c_void) -> Metrics;
    fn xp_keyboard_snapshot(
        activity: *mut c_void,
        layout: *mut Layout,
        configured: *mut u16,
        language: *mut u8,
    );
    fn xp_keyboard_text_lock(activity: *mut c_void, acquire: u8);
    fn xp_keyboard_draw_key(
        activity: *mut c_void,
        command: *const Command,
        key: *const PaintKey,
        layout: u8,
        symbols: u8,
        mode_abc: u8,
    );
    fn xp_keyboard_chrome(
        activity: *mut c_void,
        command: *const Command,
        clip: Rect,
        title: abi::Text,
    );
    fn xp_keyboard_deliver(
        legacy: *const abi::Legacy,
        parent: *mut c_void,
        text: abi::Text,
        cancelled: u8,
    );
}

pub(super) struct NativeMemory;
unsafe impl Memory for NativeMemory {
    #[cfg(test)]
    fn owner(layout: Allocation) -> *mut u8 {
        super::keyboard_tests::allocate_shell(layout)
    }
    fn allocate(layout: Allocation) -> *mut u8 {
        // These POD buffers need at most native malloc alignment.
        assert!(layout.align() <= core::mem::align_of::<usize>());
        let pointer =
            unsafe { (app().expect("keyboard App").resources.buffer_allocate)(0, layout.size()) };
        if !pointer.is_null() {
            unsafe { pointer.write_bytes(0, layout.size()) };
        }
        pointer
    }
    unsafe fn release(pointer: *mut u8, _: Allocation) {
        unsafe { (app().expect("keyboard App").resources.buffer_release)(0, pointer) };
    }
}

pub(super) type Keyboard = owner::Keyboard<NativeHost>;
pub(super) type Result = owner::Result<NativeMemory>;
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
unsafe impl Host for NativeHost {
    type Memory = NativeMemory;
    fn lock(&self, kind: Lock) {
        unsafe {
            match kind {
                Lock::Text => xp_keyboard_text_lock(self.object.as_ptr(), 1),
                Lock::Model => (self.app.runtime.model_lock)(),
            }
        }
    }
    fn unlock(&self, kind: Lock) {
        unsafe {
            match kind {
                Lock::Text => xp_keyboard_text_lock(self.object.as_ptr(), 0),
                Lock::Model => (self.app.runtime.model_unlock)(),
            }
        }
    }
    fn settings(&self) -> (u16, u8) {
        let (mut configured, mut language) = (0, 0);
        unsafe {
            xp_keyboard_snapshot(
                self.object.as_ptr(),
                ptr::null_mut(),
                &mut configured,
                &mut language,
            )
        };
        (configured, language)
    }
    fn layout(&self) -> Layout {
        let mut layout = core::mem::MaybeUninit::uninit();
        unsafe {
            xp_keyboard_snapshot(
                self.object.as_ptr(),
                layout.as_mut_ptr(),
                ptr::null_mut(),
                ptr::null_mut(),
            );
            layout.assume_init()
        }
    }
    fn with_layouts<R>(&self, consume: impl FnOnce(&Layouts) -> R) -> R {
        consume(&unsafe { xp_keyboard_layouts() })
    }
    fn with_metrics<R>(&self, consume: impl FnOnce(&Metrics) -> R) -> R {
        consume(&unsafe { xp_keyboard_metrics(self.object.as_ptr()) })
    }
    #[inline(never)]
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool {
        unsafe extern "C" fn valid<R: Resources>(
            context: *mut c_void,
            handle: u32,
            generation: u32,
        ) -> u8 {
            u8::from(unsafe { &mut *context.cast::<R>() }.valid(handle, generation))
        }
        unsafe extern "C" fn paint<R: Resources>(
            context: *mut c_void,
            _: *const GlyphTarget,
            command: *const Command,
            clip: Rect,
        ) {
            unsafe { &mut *context.cast::<R>() }.paint(unsafe { &*command }, clip);
        }
        fn bindings<R: Resources>(resources: &mut R) -> Paint {
            Paint {
                context: (resources as *mut R).cast(),
                clear: None,
                text: None,
                resource_valid: Some(valid::<R>),
                resource: Some(paint::<R>),
            }
        }
        let wire = SceneView {
            commands: scene.commands.as_ptr(),
            text: scene.text.as_ptr(),
            interactions: ptr::null(),
            generation: scene.generation,
            command_count: scene.commands.len() as u16,
            text_len: scene.text.len() as u16,
            interaction_count: 0,
            refresh: scene.refresh as u8,
            reserved: 0,
        };
        unsafe { crate::app::scene::paint(self.app.resources, &wire, &bindings(resources)) != 0 }
    }
    fn chrome(&self, command: &Command, clip: Rect, title: &[u8]) {
        unsafe {
            xp_keyboard_chrome(
                self.object.as_ptr(),
                command,
                clip,
                abi::Text::from_terminated(title),
            )
        };
    }
    fn key(&self, command: &Command, key: &PaintKey, layout: u8, symbols: bool, mode_abc: bool) {
        unsafe {
            xp_keyboard_draw_key(
                self.object.as_ptr(),
                command,
                key,
                layout,
                symbols.into(),
                mode_abc.into(),
            )
        };
    }
    fn text(&self, command: &Command, clip: Rect, terminated: &[u8]) {
        unsafe {
            (self.app.resources.text_draw)(command, clip, abi::Text::from_terminated(terminated))
        };
    }
    fn refresh(&self) {
        unsafe { (self.app.resources.refresh)(Refresh::Fast as u8) };
    }
    fn request_update(&self) {
        self.app.request(false);
    }
    fn haptic(&self, held: bool) {
        unsafe { (self.app.runtime.touch_feedback)(held.into()) };
    }
    #[inline(never)]
    fn error(&self, message: &'static str, bytes: u32) {
        // Only the two byte-count errors require formatting; no heap use on failure.
        let mut buffer = [0u8; 80];
        let message = match message {
            "Cannot build scene in %s" => b"Cannot build scene in KeyboardEntry\0".as_slice(),
            "Invalid scene in %s" => b"Invalid scene in KeyboardEntry\0".as_slice(),
            "Cannot allocate keyboard scene" => b"Cannot allocate keyboard scene\0".as_slice(),
            other => {
                let prefix = match other {
                    "Cannot allocate keyboard touch text: %u bytes" => {
                        b"Cannot allocate keyboard touch text: ".as_slice()
                    }
                    "Cannot grow keyboard text: %u bytes" => {
                        b"Cannot grow keyboard text: ".as_slice()
                    }
                    _ => unreachable!(),
                };
                buffer[..prefix.len()].copy_from_slice(prefix);
                let mut digits = [0; 10];
                let (mut number, mut start) = (bytes, digits.len());
                loop {
                    start -= 1;
                    digits[start] = b'0' + (number % 10) as u8;
                    number /= 10;
                    if number == 0 {
                        break;
                    }
                }
                let end = prefix.len() + digits.len() - start;
                buffer[prefix.len()..end].copy_from_slice(&digits[start..]);
                buffer[end..end + 6].copy_from_slice(b" bytes");
                &buffer[..end + 7]
            }
        };
        unsafe {
            (self.app.epub.log)(
                1,
                abi::Text::from_terminated(b"RUI\0"),
                abi::Text::from_terminated(message),
            )
        };
    }
}

pub(super) fn create(app: &'static App, object: NonNull<c_void>) -> Option<StableOwner<Keyboard>> {
    let keyboard = Keyboard::try_shell(NativeHost {
        app,
        object,
        adopted: Cell::new(false),
    })?;
    let keyboard = StableOwner::from(keyboard);
    keyboard.host().adopted.set(true);
    Some(keyboard)
}
#[inline(never)]
pub(super) fn launch(descriptor: &Launch) -> bool {
    let Some(app) = app() else { return false };
    let launch = abi::Launch::new(abi::KEYBOARD, abi::LaunchPayload::Keyboard(descriptor));
    let Some(object) = NonNull::new(unsafe { (app.legacy.create)(&launch) }) else {
        return false;
    };
    let Some(keyboard) = create(app, object) else {
        unsafe { (app.legacy.destroy)(object.as_ptr()) };
        return false;
    };
    app.input.borrow_mut().reset();
    app.navigation
        .queue(Transition::Push(Owner::Keyboard(keyboard)));
    true
}
#[inline(never)]
pub(super) fn enter(keyboard: &Keyboard) -> bool {
    let host = keyboard.host();
    unsafe { (host.app.legacy.enter)(host.object.as_ptr()) };
    let mut launch = core::mem::MaybeUninit::uninit();
    let initialized = unsafe {
        xp_keyboard_launch(host.object.as_ptr(), launch.as_mut_ptr());
        let launch = launch.assume_init();
        match (launch.title.bytes(), launch.initial.bytes()) {
            (Some(title), Some(initial)) => {
                keyboard.initialize(title, initial, launch.maximum, launch.input_type)
            }
            _ => {
                host.error("Cannot allocate keyboard scene", 0);
                false
            }
        }
    };
    if initialized {
        unsafe { xp_keyboard_release_launch(host.object.as_ptr()) };
    }
    initialized
}
pub(super) fn exit(keyboard: &Keyboard) {
    keyboard.exit();
    let host = keyboard.host();
    unsafe { (host.app.legacy.exit)(host.object.as_ptr()) };
}
pub(super) fn facts(keyboard: &Keyboard) -> abi::LegacyFacts {
    let host = keyboard.host();
    unsafe { (host.app.legacy.facts)(host.object.as_ptr(), 0, 0) }
}
#[inline(never)]
pub(super) fn input(keyboard: &Keyboard, input: Input) -> bool {
    let tap = (input.touch_flags & TOUCH_RELEASED != 0 && input.touch_x >= 0 && input.touch_y >= 0)
        .then_some((input.touch_x, input.touch_y));
    keyboard.input(input, tap);
    keyboard.has_result()
}
#[inline(never)]
pub(super) fn render(keyboard: &Keyboard) -> bool {
    keyboard.render()
}
#[inline(never)]
pub(super) fn input_complete(app: &App, keyboard: &Keyboard, frame: Input) {
    if input(keyboard, frame) {
        app.input.borrow_mut().reset();
        app.navigation.queue(Transition::Pop);
    }
}
/// # Safety
/// The target is valid on entry. The extraction borrow ends before consume,
/// which may destroy the owner.
#[inline(never)]
pub(super) unsafe fn with_result(
    keyboard: NonNull<Keyboard>,
    consume: impl FnOnce(super::ActivityResult),
) {
    // The continuation destroys the owner; end the extraction borrow first.
    let result = unsafe { keyboard.as_ref() }.take_result();
    consume(super::ActivityResult::Keyboard(result));
}
#[inline(never)]
pub(super) fn deliver(app: &App, parent: super::Target, mut result: Option<Result>) {
    match parent {
        super::Target::ReaderChild { .. } => deliver_bookmarks(parent, &mut result),
        super::Target::Wifi(wifi) => super::wifi::keyboard_returned(wifi, &mut result),
        super::Target::Legacy(parent) | super::Target::SettingsSection(parent) => {
            deliver_native(app, parent, &mut result)
        }
        _ => {}
    }
}
#[inline(never)]
fn deliver_bookmarks(parent: super::Target, result: &mut Option<Result>) {
    if let super::Target::ReaderChild { child, parent } = parent {
        unsafe { child.keyboard_returned(parent.as_ref(), result.take()) };
    }
}
#[inline(never)]
fn deliver_native(app: &App, parent: NonNull<c_void>, result: &mut Option<Result>) {
    let text = result.as_ref().and_then(Result::text);
    unsafe {
        xp_keyboard_deliver(
            app.legacy,
            parent.as_ptr(),
            abi::Text::from_bytes(text.unwrap_or(&[])),
            u8::from(text.is_none()),
        )
    };
}

/// # Safety
/// Loop-only. The pointer is an unadopted OwnedLegacyActivity containing the
/// keyboard facade. Acceptance transfers it exactly once; rejection does not.
#[no_mangle]
pub unsafe extern "C" fn xp_app_keyboard_push(activity: *mut c_void) -> u8 {
    let (Some(app), Some(object)) = (app(), NonNull::new(activity)) else {
        return 0;
    };
    let Some(keyboard) = create(app, object) else {
        return 0;
    };
    app.input.borrow_mut().reset();
    app.navigation
        .queue(Transition::Push(Owner::Keyboard(keyboard)));
    1
}
