use super::{abi, app, App, Owner, StableOwner, Transition};
use core::{
    alloc::Layout as Allocation,
    cell::Cell,
    ffi::{c_void, CStr},
    ptr::NonNull,
};
use xp_core::glyph::GlyphTarget;
use xp_ui::{
    ffi::{Metrics, Paint, SceneView},
    model::{Input, Metrics as TextMetrics},
    scene::{Command, Rect, Refresh, Scene},
    utility::{
        owner::{self, Host, Label, Log, Memory, Resources},
        Layout,
    },
};

#[cfg(test)]
pub(super) use super::abi::DialogInterval as Interval;
pub(super) use super::abi::DialogLaunch as Launch;
extern "C" {
    fn xp_dialog_launch(object: *mut c_void, out: *mut Launch);
    fn xp_dialog_release_launch(object: *mut c_void);
    fn xp_dialog_layout(object: *mut c_void, out: *mut Layout);
    fn xp_dialog_metrics(object: *mut c_void) -> Metrics;
    fn xp_dialog_footer(object: *mut c_void, clip: Rect, labels: *const abi::Text);
    fn xp_dialog_format(format: u16, value: i32, unsigned: u8, out: *mut u8, size: usize);
    fn xp_dialog_log(object: *mut c_void, event: u8, bytes: u32);
    fn xp_dialog_deliver(legacy: *const abi::Legacy, parent: *mut c_void, kind: u8, value: u32);
}
pub(super) struct NativeMemory;
unsafe impl Memory for NativeMemory {
    #[cfg(test)]
    fn owner(layout: Allocation) -> *mut u8 {
        super::dialog_tests::allocate_shell(layout)
    }
    fn allocate(layout: Allocation) -> *mut u8 {
        assert!(layout.align() <= core::mem::align_of::<usize>());
        let pointer =
            unsafe { (app().expect("dialog App").resources.buffer_allocate)(0, layout.size()) };
        if !pointer.is_null() {
            unsafe { pointer.write_bytes(0, layout.size()) };
        }
        pointer
    }
    unsafe fn release(pointer: *mut u8, _: Allocation) {
        unsafe { (app().expect("dialog App").resources.buffer_release)(0, pointer) };
    }
}
pub(super) struct NativeHost {
    app: &'static App,
    object: NonNull<c_void>,
    adopted: Cell<bool>,
    message_font: i32,
}
impl Drop for NativeHost {
    fn drop(&mut self) {
        if self.adopted.get() {
            unsafe { (self.app.legacy.destroy)(self.object.as_ptr()) };
        }
    }
}
fn native_text(value: &CStr) -> abi::Text {
    abi::Text::from_terminated(value.to_bytes_with_nul())
}
unsafe impl Host for NativeHost {
    type Memory = NativeMemory;
    fn lock(&self) {
        unsafe { (self.app.runtime.model_lock)() };
    }
    fn unlock(&self) {
        unsafe { (self.app.runtime.model_unlock)() };
    }
    fn layout(&self) -> Layout {
        let mut out = core::mem::MaybeUninit::uninit();
        unsafe {
            xp_dialog_layout(self.object.as_ptr(), out.as_mut_ptr());
            out.assume_init()
        }
    }
    fn with_translation<R>(&self, id: u16, consume: impl FnOnce(&CStr) -> R) -> R {
        let text = unsafe { (self.app.resources.translate)(id) };
        consume(if text.len == 0 {
            c""
        } else {
            unsafe { CStr::from_ptr(text.data.cast()) }
        })
    }
    fn format_number(&self, id: u16, value: i32, unsigned: bool, out: &mut [u8]) {
        unsafe { xp_dialog_format(id, value, unsigned.into(), out.as_mut_ptr(), out.len()) };
    }
    fn with_metrics<R>(&self, consume: impl FnOnce(&mut dyn TextMetrics) -> R) -> R {
        let metrics = unsafe { xp_dialog_metrics(self.object.as_ptr()) };
        consume(&mut &metrics)
    }
    fn text_height(&self, text: &CStr, font: i32, width: i16, style: u8, lines: u8) -> i16 {
        unsafe {
            (self.app.resources.text_box_height)(
                native_text(text),
                abi::TextLayout {
                    font,
                    width: width.into(),
                    style,
                    lines,
                },
            ) as i16
        }
    }
    #[inline(never)]
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool {
        unsafe extern "C" fn valid<R: Resources>(p: *mut c_void, h: u32, g: u32) -> u8 {
            unsafe { &mut *p.cast::<R>() }.valid(h, g).into()
        }
        unsafe extern "C" fn paint<R: Resources>(
            p: *mut c_void,
            _: *const GlyphTarget,
            c: *const Command,
            clip: Rect,
        ) {
            unsafe { &mut *p.cast::<R>() }.paint(unsafe { &*c }, clip);
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
            interactions: scene.interactions.as_ptr(),
            generation: scene.generation,
            command_count: scene.commands.len() as u16,
            text_len: scene.text.len() as u16,
            interaction_count: scene.interactions.len() as u16,
            refresh: scene.refresh as u8,
            reserved: 0,
        };
        unsafe { (self.app.resources.paint_scene)(&wire, &bindings(resources)) != 0 }
    }
    fn text(&self, command: &Command, clip: Rect, text: &CStr) {
        unsafe { (self.app.resources.text_draw)(command, clip, native_text(text)) };
    }
    fn footer(&self, _: &Command, clip: Rect, labels: [Label; 4]) {
        let labels = labels.map(|label| match label {
            Label::Translation(id) => unsafe { (self.app.resources.translate)(id) },
            Label::Literal(text) => native_text(text),
        });
        unsafe { xp_dialog_footer(self.object.as_ptr(), clip, labels.as_ptr()) };
    }
    fn refresh(&self, refresh: Refresh) {
        unsafe { (self.app.resources.refresh)(refresh as u8) };
    }
    fn request_update(&self, immediate: bool) {
        self.app.request(immediate);
    }
    fn haptic(&self) {
        unsafe { (self.app.runtime.touch_feedback)(0) };
    }
    fn finish(&self) {
        self.app.input.borrow_mut().reset();
        self.app.navigation.queue(Transition::Pop);
    }
    fn log(&self, event: Log) {
        let (event, bytes) = match event {
            Log::Enter => (0, 0),
            Log::Exit => (1, 0),
            Log::Arena(bytes) => (2, bytes as u32),
            Log::Build => (3, 0),
            Log::Paint => (4, 0),
            Log::MessageBuild => (5, 0),
        };
        unsafe { xp_dialog_log(self.object.as_ptr(), event, bytes) };
    }
}
type Confirmation = owner::Dialog<NativeHost, owner::Confirmation<NativeMemory>>;
type IntervalOwner = owner::Dialog<NativeHost, owner::Interval>;
type Message = owner::Message<NativeHost>;
pub(super) enum Owned {
    Confirmation(StableOwner<Confirmation>),
    Interval(StableOwner<IntervalOwner>),
    Message(StableOwner<Message>),
}
#[derive(Clone, Copy)]
pub(super) enum Target {
    Confirmation(NonNull<Confirmation>),
    Interval(NonNull<IntervalOwner>),
    Message(NonNull<Message>),
}
#[derive(Debug, PartialEq, Eq)]
pub(super) enum Result {
    Cancelled,
    Confirmed,
    Interval(u32),
    MessageDismissed,
}
impl Owned {
    pub(super) fn target(&self) -> Target {
        match self {
            Self::Confirmation(o) => Target::Confirmation(o.pointer()),
            Self::Interval(o) => Target::Interval(o.pointer()),
            Self::Message(o) => Target::Message(o.pointer()),
        }
    }
    pub(super) fn is_reader(&self) -> bool {
        matches!(self, Self::Interval(o) if o.is_reader())
    }
}
impl Target {
    // App owns the target and excludes destruction throughout each dispatch.
    fn host(&self) -> &NativeHost {
        unsafe {
            match self {
                Self::Confirmation(o) => o.as_ref().host(),
                Self::Interval(o) => o.as_ref().host(),
                Self::Message(o) => o.as_ref().host(),
            }
        }
    }
    pub(super) fn facts(&self) -> abi::LegacyFacts {
        let h = self.host();
        unsafe { (h.app.legacy.facts)(h.object.as_ptr(), 0, 0) }
    }
    #[inline(never)]
    pub(super) fn enter(&self) {
        unsafe {
            match self {
                Self::Confirmation(o) => {
                    o.as_ref().enter();
                }
                Self::Interval(o) => {
                    o.as_ref().enter();
                }
                Self::Message(o) => {
                    o.as_ref().enter(o.as_ref().host().message_font);
                }
            }
        }
    }
    #[inline(never)]
    pub(super) fn input(&self, input: Input) {
        unsafe {
            match self {
                Self::Confirmation(o) => o.as_ref().input(input),
                Self::Interval(o) => o.as_ref().input(input),
                Self::Message(o) => o.as_ref().input(input),
            }
        }
    }
    #[inline(never)]
    pub(super) fn render(&self) -> bool {
        unsafe {
            match self {
                Self::Confirmation(o) => o.as_ref().render(),
                Self::Interval(o) => o.as_ref().render(),
                Self::Message(o) => {
                    o.as_ref().render();
                    true
                }
            }
        }
    }
    #[inline(never)]
    pub(super) fn exit(&self) {
        unsafe {
            match self {
                Self::Confirmation(o) => o.as_ref().exit(),
                Self::Interval(o) => o.as_ref().exit(),
                Self::Message(o) => o.as_ref().exit(),
            }
        }
    }
    #[inline(never)]
    pub(super) fn with_result(self, consume: impl FnOnce(super::ActivityResult)) {
        let result = unsafe {
            match self {
                Self::Confirmation(o) => o.as_ref().take_result().map(|r| match r {
                    owner::ConfirmationResult::Confirmed => Result::Confirmed,
                    owner::ConfirmationResult::Cancelled => Result::Cancelled,
                }),
                Self::Interval(o) => o.as_ref().take_result().map(|r| match r {
                    owner::IntervalResult::Interval(v) => Result::Interval(v),
                    owner::IntervalResult::Cancelled => Result::Cancelled,
                }),
                Self::Message(o) => {
                    o.as_ref().dismiss();
                    o.as_ref().take_result().map(|_| Result::MessageDismissed)
                }
            }
        };
        // Extraction loans end before the continuation destroys the owner.
        consume(super::ActivityResult::Dialog(result));
    }
}
#[inline(never)]
pub(super) fn create(object: NonNull<c_void>) -> Option<Owned> {
    let app = app()?;
    let mut launch = core::mem::MaybeUninit::uninit();
    unsafe { xp_dialog_launch(object.as_ptr(), launch.as_mut_ptr()) };
    let launch = unsafe { launch.assume_init() };
    let host = NativeHost {
        app,
        object,
        adopted: Cell::new(false),
        message_font: launch.message_font,
    };
    let owner = match launch.kind {
        0 => Owned::Confirmation(StableOwner::from(Confirmation::confirmation(
            host,
            unsafe { launch.heading.bytes() }?,
            unsafe { launch.body.bytes() }?,
            unsafe { launch.cancel.bytes() }?,
            unsafe { launch.confirm.bytes() }?,
        )?)),
        1 => {
            let v = launch.interval;
            use xp_core::i18n::ids::STR_NONE_OPT;
            Owned::Interval(StableOwner::from(IntervalOwner::interval(
                host,
                owner::Interval {
                    title: v.title,
                    format: (v.format != STR_NONE_OPT).then_some(v.format),
                    boundary: (v.boundary != STR_NONE_OPT).then_some(v.boundary),
                    initial: v.initial,
                    minimum: v.minimum,
                    maximum: v.maximum,
                    small_step: v.small_step,
                    large_step: v.large_step,
                    edge_buttons: v.edge_buttons != 0,
                    reader: v.reader != 0,
                },
            )?))
        }
        2 => {
            let refresh = match launch.refresh {
                0 => Refresh::None,
                1 => Refresh::Fast,
                2 => Refresh::Half,
                3 => Refresh::Full,
                _ => return None,
            };
            Owned::Message(StableOwner::from(Message::try_new(
                host,
                unsafe { launch.body.bytes() }?,
                launch.style,
                refresh,
            )?))
        }
        _ => return None,
    };
    owner.target().host().adopted.set(true);
    unsafe { xp_dialog_release_launch(object.as_ptr()) };
    Some(owner)
}
#[inline(never)]
pub(super) fn create_launch(app: &App, launch: &abi::Launch) -> Option<Owner> {
    let object = NonNull::new(unsafe { (app.legacy.create)(launch) })?;
    match create(object) {
        Some(owner) => Some(Owner::Dialog(owner)),
        None => {
            unsafe { (app.legacy.destroy)(object.as_ptr()) };
            None
        }
    }
}
#[inline(never)]
pub(super) fn deliver(app: &App, parent: super::Target, result: Option<Result>) {
    if let super::Target::Legacy(parent) = parent {
        let (kind, value) = match result {
            Some(Result::Cancelled) => (0, 0),
            Some(Result::Interval(v)) => (2, v),
            Some(Result::MessageDismissed) => (3, 0),
            _ => (1, 0),
        };
        unsafe { xp_dialog_deliver(app.legacy, parent.as_ptr(), kind, value) };
    }
}
/// # Safety
/// Loop-only, unadopted native dialog wrapper. Acceptance transfers it once;
/// rejection leaves ownership with the caller.
#[no_mangle]
pub unsafe extern "C" fn xp_app_dialog_push(object: *mut c_void) -> u8 {
    let (Some(app), Some(object)) = (app(), NonNull::new(object)) else {
        return 0;
    };
    let Some(owner) = create(object) else {
        return 0;
    };
    app.input.borrow_mut().reset();
    app.navigation.queue(Transition::Push(Owner::Dialog(owner)));
    1
}

/// # Safety
/// Host pixel tests lend two aligned writable commands.
#[cfg(any(test, feature = "app-test"))]
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_message_commands(
    screen: Rect,
    font: i32,
    style: u8,
    text_len: u16,
    width: i32,
    height: i32,
    out: *mut Command,
) -> u8 {
    let Some(out) = (unsafe { out.cast::<[Command; 2]>().as_mut() }) else {
        return 0;
    };
    let Ok(commands) =
        xp_ui::model::message_commands(screen, font, style, usize::from(text_len), width, height)
    else {
        return 0;
    };
    *out = commands;
    1
}
