use super::{abi, app, App, Owner, StableOwner, Transition};
use core::{
    alloc::Layout as Allocation,
    ffi::{c_void, CStr},
};
use xp_core::{glyph::GlyphTarget, i18n::ids::*};
use xp_ui::{
    ffi::{Paint, SceneView},
    model::Input,
    scene::{Command, Rect, Refresh, Scene},
    settings::{
        root::{self, Host, Log, Memory, Resources, Section},
        view::Layout,
    },
};
extern "C" {
    fn xp_settings_shell_snapshot(layout: *mut Layout);
    fn xp_settings_shell_paint(
        command: *const Command,
        clip: Rect,
        title: *const core::ffi::c_char,
        icon: u8,
    );
    fn xp_settings_shell_theme_reload();
}
pub(super) struct NativeMemory;
unsafe impl Memory for NativeMemory {
    #[cfg(test)]
    fn owner(layout: Allocation) -> *mut u8 {
        super::settings_tests::allocate_shell(layout)
    }
    fn allocate(layout: Allocation) -> *mut u8 {
        assert!(layout.align() <= core::mem::align_of::<usize>());
        let p = unsafe {
            (app().expect("settings App").resources.buffer_allocate)(
                abi::MEMORY_DEFAULT,
                layout.size(),
            )
        };
        if !p.is_null() {
            unsafe { p.write_bytes(0, layout.size()) };
        }
        p
    }
    unsafe fn release(p: *mut u8, _: Allocation) {
        unsafe { (app().expect("settings App").resources.buffer_release)(abi::MEMORY_DEFAULT, p) };
    }
}
pub(super) type Settings = root::Root<NativeHost>;
pub(super) struct NativeHost {
    app: &'static App,
}
fn text(value: &CStr) -> abi::Text {
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
    fn translate(&self, id: u16) -> &'static CStr {
        // I18n::get delegates to the immutable built-in Rust translation table.
        let value = unsafe { (self.app.resources.translate)(id) };
        if value.len == 0 {
            c""
        } else {
            unsafe { CStr::from_ptr(value.data.cast()) }
        }
    }
    fn layout(&self, out: &mut Layout) {
        unsafe { xp_settings_shell_snapshot(out) };
    }
    fn width(&self, value: &CStr, font: i32, style: u8) -> i32 {
        unsafe { (self.app.resources.text_width)(text(value), font, style) }
    }
    fn height(&self, value: &CStr, font: i32, style: u8, lines: u8, width: i16) -> i32 {
        unsafe {
            (self.app.resources.text_box_height)(
                text(value),
                abi::TextLayout {
                    font,
                    style,
                    lines,
                    width: width.into(),
                },
            )
        }
    }
    fn line(&self, font: i32) -> i32 {
        unsafe { (self.app.resources.line_height)(font) }
    }
    #[inline(never)]
    fn paint(&self, scene: &Scene<'_>, resources: &mut impl Resources) -> bool {
        unsafe extern "C" fn valid<R: Resources>(p: *mut c_void, h: u32, g: u32) -> u8 {
            unsafe { &*p.cast::<R>() }.valid(h, g).into()
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
    fn text(&self, command: &Command, clip: Rect, value: &CStr) {
        unsafe { (self.app.resources.text_draw)(command, clip, text(value)) };
    }
    fn chrome(&self, command: &Command, clip: Rect, title: &CStr, icon: u8) {
        unsafe { xp_settings_shell_paint(command, clip, title.as_ptr(), icon) };
    }
    fn footer(&self) {
        let labels =
            [STR_BACK, STR_SELECT, STR_DIR_UP, STR_DIR_DOWN].map(|id| text(self.translate(id)));
        unsafe { (self.app.resources.footer_draw)(labels.as_ptr()) };
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
    fn home(&self) {
        self.app.go_home(0, false);
    }
    fn open_section(&self, section: Section) {
        let launch = abi::Launch::new(
            abi::SETTINGS_SECTION,
            abi::LaunchPayload::SettingsSection(section as u8),
        );
        let Some(owner) = self.app.create(&launch) else {
            return;
        };
        self.app.input.borrow_mut().reset();
        self.app.navigation.queue(Transition::Push(owner));
    }
    fn theme_reload(&self) {
        unsafe { xp_settings_shell_theme_reload() };
    }
    #[inline(never)]
    fn log(&self, event: Log) {
        let mut buffer = [0u8; 80];
        let (level, tag, message) = match event {
            Log::Enter => (0, c"ACT", text(c"Entering activity: SettingsList")),
            Log::Exit => (0, c"ACT", text(c"Exiting activity: SettingsList")),
            Log::Build => (1, c"RUI", text(c"Cannot build scene in SettingsList")),
            Log::Paint => (1, c"RUI", text(c"Invalid scene in SettingsList")),
            Log::Arena(bytes) => (
                1,
                c"RUI",
                decimal_message(
                    &mut buffer,
                    b"Cannot allocate ",
                    bytes,
                    false,
                    b" command bytes\0",
                ),
            ),
            Log::Scroll(delta) => (
                0,
                c"LIST",
                decimal_message(
                    &mut buffer,
                    b"SettingsList swipe delta=",
                    delta.unsigned_abs() as usize,
                    delta < 0,
                    b" count=6\0",
                ),
            ),
        };
        unsafe { (self.app.epub.log)(level, text(tag), message) };
    }
}
fn decimal_message(
    out: &mut [u8; 80],
    prefix: &[u8],
    mut value: usize,
    negative: bool,
    suffix: &[u8],
) -> abi::Text {
    // At most 20 digits; both fixed messages fit the existing 80-byte scratch.
    let mut start = out.len();
    loop {
        start -= 1;
        out[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            break;
        }
    }
    let digits = out.len() - start;
    out[..prefix.len()].copy_from_slice(prefix);
    let number = prefix.len() + usize::from(negative);
    if negative {
        out[prefix.len()] = b'-';
    }
    out.copy_within(start.., number);
    let end = number + digits;
    out[end..end + suffix.len()].copy_from_slice(suffix);
    abi::Text {
        data: out.as_ptr(),
        len: end + suffix.len() - 1,
    }
}
// Dormant until the CP2 launch switch; tests install this production owner directly.
#[allow(dead_code)]
#[inline(never)]
pub(super) fn create(app: &'static App, section: u8) -> Option<Owner> {
    Settings::try_new(NativeHost { app }, section)
        .map(StableOwner::from)
        .map(Owner::Settings)
}
pub(super) fn facts() -> abi::LegacyFacts {
    abi::LegacyFacts {
        kind: abi::SETTINGS,
        ..unsafe { core::mem::zeroed() }
    }
}
#[inline(never)]
pub(super) fn enter(root: &Settings) {
    root.enter();
}
#[inline(never)]
pub(super) fn input(root: &Settings, input: Input) {
    root.input(input);
}
#[inline(never)]
pub(super) fn render(root: &Settings) -> bool {
    root.render()
}

#[inline(never)]
pub(super) fn create_section(app: &App, launch: &abi::Launch) -> Option<Owner> {
    Section::from_byte(unsafe { launch.value.settings_section })?;
    let mut owner = app.adopt(unsafe { (app.legacy.create)(launch) })?;
    if let Owner::Legacy { section, .. } = &mut owner {
        *section = true;
    }
    Some(owner)
}
