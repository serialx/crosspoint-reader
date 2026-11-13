extern crate alloc;

mod abi;
#[allow(dead_code, unused_imports)]
mod bookmarks;
// App-owned clipping storage shared by reader and retiring native callers.
#[allow(dead_code)]
mod clippings;
#[path = "clippings/facade.rs"]
mod clippings_facade;
#[allow(dead_code)]
#[path = "clippings/native.rs"]
mod clippings_native;
#[allow(dead_code)]
#[path = "clippings/service.rs"]
mod clippings_service;
mod cover_cache;
mod dialog;
mod driver;
mod epub;
mod home;
mod home_books;
mod home_text;
mod keyboard;
mod network_modes;
mod resources;
mod result;
mod runtime_ports;
mod scene;
mod settings;
mod stable_owner;
mod sync;
#[cfg(test)]
mod test_resources;
mod wifi;

use abi::LaunchPayload;
use alloc::{alloc::alloc, boxed::Box};
use core::{
    alloc::Layout,
    cell::{Cell, RefCell, UnsafeCell},
    ffi::c_void,
    ptr::NonNull,
};
use epub::children::{
    ChildTarget, Definition, DefinitionSession, Physical, ReaderChild, ReaderChildResult,
};
mod definition;
use stable_owner::StableOwner;
use sync::ModelCell;
use xp_ui::{
    app::{
        input::{Feedback, HomeAction, HomeBindings, HomeContact, HomeInput},
        Activity, Application, Services, Transition, TransitionKind,
    },
    home::activity::InputResult,
    home::{Action as HomeRoute, Menu},
};

#[derive(Clone, Copy)]
enum Target {
    Home(NonNull<home::Home>),
    Keyboard(NonNull<keyboard::Keyboard>),
    NetworkModes(NonNull<network_modes::Modes>),
    Wifi(NonNull<wifi::Wifi>),
    Dialog(dialog::Target),
    Epub(NonNull<epub::EpubSession>),
    Legacy(NonNull<c_void>),
    Definition(NonNull<DefinitionSession>),
    ReaderChild {
        child: ChildTarget,
        parent: NonNull<epub::EpubSession>,
    },
    Settings(NonNull<settings::Settings>),
    SettingsSection(NonNull<c_void>),
}

enum ActivityResult {
    Keyboard(Option<keyboard::Result>),
    Dialog(Option<dialog::Result>),
    Legacy(*const abi::XpAppResultView),
    Child(ChildResultSlot),
    SectionComplete,
}

// Valid only during with_result's synchronous continuation. Its outlined producer
// owns the slot after child destruction; delivery takes the payload exactly once.
struct ChildResultSlot(NonNull<Option<ReaderChildResult>>);
#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<ActivityResult>() <= 24);
impl ChildResultSlot {
    unsafe fn take(mut self) -> Option<ReaderChildResult> {
        unsafe { self.0.as_mut() }.take()
    }
}

#[inline(never)]
unsafe fn receive_child_result(child: ChildTarget, consume: impl FnOnce(ActivityResult)) {
    let mut result = unsafe { child.take_result() };
    consume(ActivityResult::Child(ChildResultSlot(NonNull::from(
        &mut result,
    ))));
}

enum Owner {
    Definition(Definition),
    Home(StableOwner<home::Home>),
    Keyboard(StableOwner<keyboard::Keyboard>),
    NetworkModes(StableOwner<network_modes::Modes>),
    Wifi(StableOwner<wifi::Wifi>),
    Dialog(dialog::Owned),
    Epub(StableOwner<epub::EpubSession>),
    ReaderChild {
        child: ReaderChild,
        parent: NonNull<epub::EpubSession>,
    },
    Legacy {
        object: NonNull<c_void>,
        services: &'static abi::Legacy,
        reader: bool,
        section: bool,
    },
    Settings(StableOwner<settings::Settings>),
}

impl Activity for Owner {
    type Target = Target;
    fn target(&self) -> Target {
        match self {
            Self::Home(home) => Target::Home(home.pointer()),
            Self::Epub(epub) => Target::Epub(epub.pointer()),
            Self::Keyboard(keyboard) => Target::Keyboard(keyboard.pointer()),
            Self::NetworkModes(modes) => Target::NetworkModes(modes.pointer()),
            Self::Wifi(wifi) => Target::Wifi(wifi.pointer()),
            Self::Dialog(dialog) => Target::Dialog(dialog.target()),
            Self::Legacy {
                object, section, ..
            } => {
                if *section {
                    Target::SettingsSection(*object)
                } else {
                    Target::Legacy(*object)
                }
            }
            Self::Settings(root) => Target::Settings(root.pointer()),
            Self::Definition(viewer) => Target::Definition(viewer.pointer()),
            Self::ReaderChild { child, parent } => Target::ReaderChild {
                child: child.target(),
                parent: *parent,
            },
        }
    }
    fn is_reader(&self) -> bool {
        matches!(self, Self::Epub(_) | Self::Legacy { reader: true, .. })
            || matches!(self, Self::Dialog(dialog) if dialog.is_reader())
    }
}

impl Drop for Owner {
    fn drop(&mut self) {
        match self {
            Self::Epub(epub) => epub.destroy_owned(),
            Self::Legacy {
                object, services, ..
            } => unsafe { (services.destroy)(object.as_ptr()) },
            Self::Home(_)
            | Self::Keyboard(_)
            | Self::NetworkModes(_)
            | Self::Wifi(_)
            | Self::Dialog(_)
            | Self::ReaderChild { .. }
            | Self::Definition(_)
            | Self::Settings(_) => {}
        }
    }
}

struct Signals {
    target: Option<Target>,
    visit: u32,
    painted_visit: u32,
    requested: bool,
}

struct App {
    clippings: clippings_facade::ClippingService,
    navigation: Application<Owner>,
    input: RefCell<HomeInput>,
    driver: Cell<driver::State>,
    signals: ModelCell<Signals>,
    widths: UnsafeCell<xp_ui::home::view::Widths>,
    legacy: &'static abi::Legacy,
    epub: &'static epub::abi::XpEpubLeaves,
    #[cfg(feature = "app-test")]
    clipping_loaded_observer: Cell<Option<unsafe extern "C" fn(*const abi::Book, u8)>>,
    #[cfg(feature = "app-test")]
    clipping_unloaded_observer: Cell<Option<unsafe extern "C" fn()>>,
    #[cfg(feature = "app-test")]
    legacy_entry_fixture: Cell<Option<&'static epub::LegacyBookServices>>,
    child_drawing: Option<&'static Physical>,
    resources: &'static abi::Resources,
    runtime: &'static abi::Runtime,
}

struct Storage(UnsafeCell<Option<App>>);
// Init precedes task creation. Navigation/input/driver are loop-only; render uses
// the lifetime-locked target and widths, while signals use the model critical section.
unsafe impl Sync for Storage {}
static APP: Storage = Storage(UnsafeCell::new(None));

fn app() -> Option<&'static App> {
    unsafe { &*APP.0.get() }.as_ref()
}

struct RenderGuard {
    token: *mut c_void,
    runtime: &'static abi::Runtime,
}
impl Drop for RenderGuard {
    fn drop(&mut self) {
        unsafe { (self.runtime.render_unlock)(self.token) };
    }
}

fn request_update() {
    if let Some(app) = app() {
        app.signals.with(|signals| signals.requested = true);
    }
}

impl App {
    #[inline(never)]
    unsafe fn deliver_child_result(&self, parent: Target, slot: ChildResultSlot) {
        let result = unsafe { slot.take() };
        match (parent, result) {
            (Target::Epub(epub), Some(result)) => unsafe {
                epub.as_ref().deliver_reader_child(result)
            },
            (
                Target::ReaderChild {
                    child: child @ ChildTarget::Lookup(_),
                    ..
                },
                Some(ReaderChildResult::DefinitionClosed { cancelled }),
            ) => self.definition_returned(&child, cancelled),
            (
                Target::ReaderChild {
                    child: ChildTarget::Clippings(_),
                    ..
                },
                Some(ReaderChildResult::DefinitionClosed { .. }),
            ) => self.request(false),
            (Target::Legacy(parent), Some(ReaderChildResult::DefinitionClosed { .. })) => unsafe {
                (self.legacy.deliver_result)(parent.as_ptr(), core::ptr::null())
            },
            _ => {}
        }
    }
    fn legacy_facts(&self, target: NonNull<c_void>) -> abi::LegacyFacts {
        unsafe { (self.legacy.facts)(target.as_ptr(), 0, 0) }
    }

    fn skip_delay(&self) -> bool {
        match self.navigation.current() {
            Some(Target::Epub(epub)) => unsafe { epub.as_ref() }.skip_loop_delay(),
            Some(Target::Legacy(target) | Target::SettingsSection(target)) => unsafe {
                (self.legacy.facts)(target.as_ptr(), 1, 0).skip_delay != 0
            },
            _ => false,
        }
    }

    fn target_facts(&self) -> Option<abi::LegacyFacts> {
        match self.navigation.current()? {
            Target::Legacy(target) | Target::SettingsSection(target) => {
                Some(self.legacy_facts(target))
            }
            Target::Definition(_) => Some(definition::facts()),
            Target::Epub(epub) => Some(unsafe { epub.as_ref() }.app_facts(false)),
            Target::ReaderChild { child, .. } => Some(unsafe { child.facts() }),
            Target::Keyboard(keyboard) => Some(keyboard::facts(unsafe { keyboard.as_ref() })),
            Target::Dialog(dialog) => Some(dialog.facts()),
            Target::NetworkModes(modes) => Some(network_modes::facts(modes)),
            Target::Wifi(wifi) => Some(wifi::facts(wifi)),
            Target::Home(_) => None,
            Target::Settings(_) => Some(settings::facts()),
        }
    }

    fn requested(&self) -> bool {
        self.signals.with(|signals| signals.requested)
    }

    fn request(&self, immediate: bool) {
        if immediate {
            unsafe { (self.runtime.notify_render)() };
        } else {
            request_update();
        }
    }

    fn flush_update(&self) {
        if self
            .signals
            .with(|signals| core::mem::replace(&mut signals.requested, false))
        {
            unsafe { (self.runtime.notify_render)() };
        }
    }

    fn new_home(&self, menu: u8, clean_initial: bool) -> Option<Owner> {
        let pointer = NonNull::new(
            unsafe { alloc(Layout::new::<home::Home>()) }
                .cast::<core::mem::MaybeUninit<home::Home>>(),
        )?;
        unsafe {
            home::Home::initialize(
                pointer.as_ptr().cast(),
                self.resources,
                self.runtime,
                request_update,
                menu,
                clean_initial,
            );
            let home = Box::from_raw(pointer.as_ptr()).assume_init();
            Some(Owner::Home(StableOwner::from(home)))
        }
    }

    fn adopt(&self, object: *mut c_void) -> Option<Owner> {
        let object = NonNull::new(object)?;
        let facts = self.legacy_facts(object);
        if facts.kind == abi::NETWORK_MODE {
            return network_modes::create(object).map(Owner::NetworkModes);
        }
        if facts.kind == abi::WIFI {
            return wifi::create(object).map(Owner::Wifi);
        }
        let reader = facts.reader != 0;
        Some(Owner::Legacy {
            object,
            services: self.legacy,
            reader,
            section: false,
        })
    }

    fn create(&self, launch: &abi::Launch) -> Option<Owner> {
        if launch.kind == abi::SETTINGS {
            return settings::create(app()?, unsafe { launch.value.settings_section });
        }
        if launch.kind == abi::SETTINGS_SECTION {
            return settings::create_section(self, launch);
        }
        if matches!(launch.kind, abi::MESSAGE | abi::DIALOG) {
            return dialog::create_launch(self, launch);
        }
        if launch.kind == abi::READER {
            let reader = unsafe { launch.value.reader };
            let path = unsafe { reader.path.bytes() }?;
            if home_books::book_kind(path) != Some(abi::BOOK_XTC) {
                #[cfg(feature = "app-test")]
                {
                    if let Some(fixture) = self.legacy_entry_fixture.get() {
                        return epub::EpubSession::try_owned_legacy_render(
                            path,
                            reader.fast_initial != 0,
                            self.epub,
                            self.runtime,
                            self.resources,
                            &fixture.section.page,
                            Some(fixture.children),
                        )
                        .map(StableOwner::from)
                        .map(Owner::Epub);
                    }
                    if let Some(Target::Epub(previous)) = self.navigation.current() {
                        let previous = unsafe { previous.as_ref() };
                        if let Some(fixture) = previous.legacy_book_fixture() {
                            return epub::EpubSession::try_owned_legacy(
                                path,
                                reader.fast_initial != 0,
                                self.epub,
                                self.runtime,
                                self.resources,
                                fixture,
                            )
                            .map(StableOwner::from)
                            .map(Owner::Epub);
                        }
                        if let Some(fixture) = previous.legacy_page_fixture() {
                            return epub::EpubSession::try_owned_legacy_render(
                                path,
                                reader.fast_initial != 0,
                                self.epub,
                                self.runtime,
                                self.resources,
                                fixture,
                                previous.legacy_child_fixture(),
                            )
                            .map(StableOwner::from)
                            .map(Owner::Epub);
                        }
                    }
                }
                return epub::EpubSession::try_owned(
                    path,
                    reader.fast_initial != 0,
                    self.epub,
                    self.runtime,
                    self.resources,
                )
                .map(StableOwner::from)
                .map(Owner::Epub);
            }
        }
        if !matches!(
            launch.kind,
            abi::FILES
                | abi::LIBRARY
                | abi::OPDS
                | abi::OPDS_PICKER
                | abi::PLUGINS
                | abi::FILE_TRANSFER
                | abi::USB_DRIVE
                | abi::READER
                | abi::BITMAP
                | abi::BOOT
                | abi::SLEEP
                | abi::MESSAGE
                | abi::CRASH
                | abi::FRONTLIGHT_PANEL
        ) {
            return None;
        }
        self.adopt(unsafe { (self.legacy.create)(launch) })
    }

    fn replace(&self, owner: Owner) {
        self.input.borrow_mut().reset();
        self.navigation.replace(owner, self);
    }

    fn go_home(&self, mut menu: u8, clean_initial: bool) -> bool {
        if menu == 0 {
            if let Some(facts) = self.target_facts() {
                menu = match facts.kind {
                    abi::FILES => 1,
                    abi::LIBRARY => 2,
                    abi::OPDS | abi::PLUGINS => 3,
                    abi::FILE_TRANSFER => 4,
                    abi::SETTINGS => 5,
                    _ => 0,
                };
            }
        }
        let Some(owner) = self.new_home(menu, clean_initial) else {
            return false;
        };
        self.replace(owner);
        true
    }

    fn navigate(&self, mut launch: abi::Launch) -> bool {
        if launch.kind == abi::READER {
            let Some(path) = (unsafe { launch.value.reader.path.bytes() }) else {
                return false;
            };
            if path.is_empty() {
                launch = abi::Launch::new(
                    abi::FILES,
                    LaunchPayload::Files(abi::Text::from_bytes(b"/")),
                );
            } else if image_path(path) {
                launch = abi::Launch::new(
                    abi::BITMAP,
                    LaunchPayload::Bitmap(unsafe { launch.value.reader.path }),
                );
            }
        }
        if launch.kind == abi::OPDS_PICKER && unsafe { (self.resources.opds_count)() } == 1 {
            launch = abi::Launch::new(abi::OPDS, LaunchPayload::Opds(0));
        }
        let Some(owner) = self.create(&launch) else {
            return false;
        };
        self.replace(owner);
        true
    }

    fn snapshot(
        &self,
        poll: bool,
        full_buttons: bool,
        consume_release: bool,
    ) -> abi::MappedSnapshot {
        let mut frame = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.runtime.input_mapped_snapshot)(
                u8::from(poll),
                u8::from(full_buttons),
                u8::from(consume_release),
                frame.as_mut_ptr(),
            );
            frame.assume_init()
        }
    }

    fn settings(&self) -> abi::Settings {
        let mut settings = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.runtime.loop_settings)(settings.as_mut_ptr());
            settings.assume_init()
        }
    }

    fn input_polled(&self, defer: bool) {
        let frame = self.snapshot(false, false, false);
        let settings = self.settings();
        let action = |value| HomeAction::try_from(value).unwrap_or(HomeAction::Ignore);
        let bindings = HomeBindings {
            tap: action(settings.home_tap),
            double_tap: action(settings.home_double),
            long_press: action(settings.home_long),
        };
        let contact = (frame.has_home_key != 0).then_some(HomeContact {
            tapped: frame.home_key_tapped != 0,
            long_pressed: frame.home_key_long_pressed != 0,
            swiped: frame.input.swipe != 0,
            pressed: frame.home_key_pressed != 0,
        });
        let feedback =
            self.input
                .borrow_mut()
                .sample_contact(frame.input.now_ms, contact, bindings);
        match feedback {
            Feedback::None => {}
            Feedback::Tap => unsafe { (self.runtime.touch_feedback)(0) },
            Feedback::LongPress => unsafe { (self.runtime.touch_feedback)(1) },
        }
        let power = if frame.power_released != 0 {
            match settings.short_power {
                7 => HomeAction::Bookmark,
                8 => HomeAction::Sync,
                9 => HomeAction::ReaderMenu,
                _ => HomeAction::Ignore,
            }
        } else {
            HomeAction::Ignore
        };
        self.input.borrow_mut().finish_frame(power, defer);
    }

    fn activity_loop(&self) {
        let full_buttons = match self.navigation.current() {
            Some(
                Target::Home(_)
                | Target::Keyboard(_)
                | Target::Dialog(_)
                | Target::NetworkModes(_)
                | Target::Settings(_),
            ) => true,
            Some(Target::ReaderChild { child, .. }) => !matches!(child, ChildTarget::Lookup(_)),
            _ => false,
        };
        let frame = self.snapshot(false, full_buttons, true);
        if frame.suppressed_release != 0 {
            return;
        }
        if let Some(target) = self.navigation.current() {
            let facts = match target {
                Target::Legacy(target) | Target::SettingsSection(target) => {
                    Some(self.legacy_facts(target))
                }
                Target::Definition(target) => {
                    self.definition_input(
                        target,
                        frame.home_gesture != 0,
                        frame.light_panel_gesture != 0,
                    );
                    return;
                }
                Target::Epub(epub) => Some(unsafe { epub.as_ref() }.app_facts(false)),
                Target::ReaderChild { child, .. } => Some(unsafe { child.facts() }),
                Target::Keyboard(keyboard) => Some(keyboard::facts(unsafe { keyboard.as_ref() })),
                Target::Dialog(dialog) => Some(dialog.facts()),
                Target::NetworkModes(modes) => Some(network_modes::facts(modes)),
                Target::Wifi(wifi) => Some(wifi::facts(wifi)),
                Target::Home(_) => None,
                Target::Settings(_) => Some(settings::facts()),
            };
            if facts.is_some_and(|f| f.exclusive_storage != 0) {
                if let Target::Legacy(target) | Target::SettingsSection(target) = target {
                    unsafe { (self.legacy.r#loop)(target.as_ptr()) };
                }
                self.flush_update();
                return;
            }
            if frame.home_gesture != 0 {
                match target {
                    Target::Legacy(target) | Target::SettingsSection(target) => {
                        if unsafe { (self.legacy.home_gesture)(target.as_ptr()) } == 0 {
                            self.go_home(0, false);
                        }
                        return;
                    }
                    Target::Epub(_)
                    | Target::Keyboard(_)
                    | Target::Dialog(_)
                    | Target::Definition(_)
                    | Target::NetworkModes(_)
                    | Target::Wifi(_)
                    | Target::Settings(_) => {
                        self.go_home(0, false);
                        return;
                    }
                    Target::ReaderChild {
                        child: child @ ChildTarget::Menu(_),
                        parent,
                    } => {
                        if unsafe { child.input(parent.as_ref(), frame.input, true) } {
                            self.input.borrow_mut().reset();
                            self.navigation.queue(Transition::Pop);
                        }
                        return;
                    }
                    Target::ReaderChild { .. } => {
                        self.go_home(0, false);
                        return;
                    }
                    Target::Home(_) => {}
                }
            }
            let status_screen = matches!(target, Target::Home(_))
                || facts.is_some_and(|f| {
                    matches!(f.kind, abi::FILES | abi::SETTINGS | abi::NETWORK_MODE)
                });
            if unsafe { (self.runtime.frontlight_present)() } != 0
                && !facts.is_some_and(|f| f.kind == abi::FRONTLIGHT_PANEL)
                && (frame.light_panel_gesture != 0 || status_screen && frame.status_bar_tap != 0)
            {
                unsafe { (self.runtime.touch_feedback)(0) };
                if let Some(panel) = self.create(&abi::Launch::new(
                    abi::FRONTLIGHT_PANEL,
                    LaunchPayload::None,
                )) {
                    self.input.borrow_mut().reset();
                    self.navigation.queue(Transition::Push(panel));
                }
                return;
            }
            match target {
                Target::Legacy(target) | Target::SettingsSection(target) => unsafe {
                    (self.legacy.r#loop)(target.as_ptr())
                },
                Target::Definition(_) => unreachable!("Definition input already dispatched"),
                Target::Epub(epub) => unsafe { epub.as_ref() }.loop_reader(),
                Target::Dialog(dialog) => dialog.input(frame.input),
                Target::NetworkModes(modes) => network_modes::input(modes, frame.input),
                Target::Wifi(wifi) => wifi::poll(wifi),
                Target::Settings(root) => settings::input(unsafe { root.as_ref() }, frame.input),
                Target::Keyboard(keyboard) => {
                    keyboard::input_complete(self, unsafe { keyboard.as_ref() }, frame.input)
                }
                Target::ReaderChild { child, parent } => {
                    if unsafe { child.input(parent.as_ref(), frame.input, false) } {
                        self.input.borrow_mut().reset();
                        self.navigation.queue(Transition::Pop);
                    }
                }
                Target::Home(home) => match unsafe { home.as_ref() }.input(frame.input) {
                    InputResult::Idle => {}
                    InputResult::RetryPaint => self.request(false),
                    InputResult::Update(update) => {
                        if update.invalidate != 0 {
                            self.request(false);
                        }
                        if update.haptic {
                            unsafe { (self.runtime.touch_feedback)(0) };
                        }
                        match update.action {
                            Some(HomeRoute::Book(index)) => {
                                if let Some(path) = unsafe { home.as_ref().path(index.into()) } {
                                    self.navigate(abi::Launch::new(
                                        abi::READER,
                                        LaunchPayload::Reader(abi::ReaderLaunch {
                                            path,
                                            fast_initial: 0,
                                        }),
                                    ));
                                }
                            }
                            Some(HomeRoute::Menu(menu)) => {
                                let kind = match menu {
                                    Menu::Files => abi::FILES,
                                    Menu::Library => abi::LIBRARY,
                                    Menu::Opds => abi::OPDS_PICKER,
                                    Menu::Plugins => abi::PLUGINS,
                                    Menu::Transfer => abi::FILE_TRANSFER,
                                    Menu::Settings => abi::SETTINGS,
                                };
                                let payload = match menu {
                                    Menu::Files => LaunchPayload::Files(abi::Text::from_bytes(b"")),
                                    Menu::Library | Menu::Opds => LaunchPayload::None,
                                    Menu::Plugins => LaunchPayload::Plugins(u8::from(
                                        unsafe { (self.resources.opds_count)() } != 0,
                                    )),
                                    Menu::Transfer => LaunchPayload::Transfer(0),
                                    Menu::Settings => LaunchPayload::Settings(6),
                                };
                                self.navigate(abi::Launch::new(kind, payload));
                            }
                            None => {}
                        }
                    }
                },
            }
        }
        if !self.navigation.drain(self) {
            return;
        }
        self.flush_update();
    }

    fn remote_click(&self, button: u8) -> bool {
        if button > 4
            || self.navigation.pending_kind() != TransitionKind::None
            || !self.target_facts().is_some_and(|f| f.remote_input != 0)
            || self.requested()
        {
            return false;
        }
        let token = unsafe { (self.runtime.render_try_lock)() };
        if token.is_null() {
            return false;
        }
        drop(RenderGuard {
            token,
            runtime: self.runtime,
        });
        unsafe { (self.runtime.input_remote_click)(button) != 0 }
    }

    fn forced_refresh(&self) -> bool {
        match self.navigation.current() {
            Some(Target::Epub(epub)) => unsafe { epub.as_ref() }.force_refresh(),
            Some(Target::Legacy(target) | Target::SettingsSection(target)) => unsafe {
                (self.legacy.forced_refresh)(target.as_ptr()) != 0
            },
            _ => false,
        }
    }
}

impl App {
    #[inline(never)]
    fn definition_returned(&self, child: &ChildTarget, cancelled: bool) {
        let ChildTarget::Lookup(lookup) = child else {
            unreachable!()
        };
        unsafe { lookup.as_ref().definition_closed(cancelled) };
        self.input.borrow_mut().reset();
        self.navigation.queue(Transition::Pop);
    }

    #[inline(never)]
    fn definition_install_failed(&self, child: &ChildTarget) {
        let ChildTarget::Lookup(lookup) = child else {
            unreachable!()
        };
        unsafe {
            lookup
                .as_ref()
                .definition_install_failed((self.runtime.now_ms)())
        };
        self.request(false);
    }
}

fn image_path(path: &[u8]) -> bool {
    [b".bmp".as_slice(), b".png", b".jpg", b".jpeg"]
        .iter()
        .any(|extension| {
            path.get(path.len().saturating_sub(extension.len())..)
                .is_some_and(|tail| tail.eq_ignore_ascii_case(extension))
        })
}

impl Services<Owner> for App {
    type RenderGuard = RenderGuard;
    type Result = ActivityResult;
    fn render_lock(&self) -> RenderGuard {
        RenderGuard {
            token: unsafe { (self.runtime.render_lock)() },
            runtime: self.runtime,
        }
    }
    fn before_screen_change(&self, reader: bool) -> bool {
        unsafe { (self.runtime.ble_before_screen_change)(u8::from(reader)) != 0 }
    }
    fn publish(&self, target: Option<Target>, visit: u32) {
        self.signals.with(|signals| {
            signals.target = target;
            signals.visit = visit;
        });
    }
    fn clear_header(&self) {
        unsafe { (self.runtime.header_back_clear)() };
    }
    fn enter(&self, target: Target) {
        match target {
            Target::Home(home) => unsafe { home.as_ref() }.enter(),
            Target::Dialog(dialog) => dialog.enter(),
            Target::NetworkModes(modes) => network_modes::enter(modes),
            Target::Wifi(wifi) => wifi::enter(wifi),
            Target::Settings(root) => settings::enter(unsafe { root.as_ref() }),
            Target::Keyboard(keyboard) => {
                if !keyboard::enter(unsafe { keyboard.as_ref() }) {
                    self.navigation.queue(Transition::Pop);
                }
            }
            Target::Definition(target) => self.definition_enter(target),
            Target::Epub(epub) => unsafe { epub.as_ref() }.enter(),
            Target::Legacy(target) | Target::SettingsSection(target) => unsafe {
                (self.legacy.enter)(target.as_ptr())
            },
            Target::ReaderChild { child, parent } => {
                let ready = unsafe {
                    child.enter(
                        parent.as_ref(),
                        self.child_drawing.expect("child drawing installed"),
                    )
                };
                if !ready {
                    self.navigation.queue(Transition::Pop);
                }
            }
        }
    }
    fn exit(&self, target: Target) {
        match target {
            Target::Home(home) => unsafe { home.as_ref() }.exit(),
            Target::Dialog(dialog) => dialog.exit(),
            Target::NetworkModes(modes) => network_modes::exit(modes),
            Target::Wifi(wifi) => wifi::exit(wifi),
            Target::Settings(root) => unsafe { root.as_ref() }.exit(),
            Target::Keyboard(keyboard) => keyboard::exit(unsafe { keyboard.as_ref() }),
            Target::Definition(target) => self.definition_exit(target),
            Target::Epub(epub) => unsafe { epub.as_ref().exit_owned() },
            Target::Legacy(target) | Target::SettingsSection(target) => unsafe {
                (self.legacy.exit)(target.as_ptr())
            },
            Target::ReaderChild { child, parent } => unsafe { child.exit(parent.as_ref()) },
        }
    }
    fn with_result(&self, target: Target, consume: impl FnOnce(ActivityResult)) {
        unsafe extern "C" fn receive<F: FnOnce(*const abi::XpAppResultView)>(
            context: *mut c_void,
            result: *const abi::XpAppResultView,
        ) {
            let consume = unsafe { &mut *context.cast::<Option<F>>() };
            consume.take().expect("result continuation called twice")(result);
        }
        fn invoke<F: FnOnce(*const abi::XpAppResultView)>(
            app: &App,
            target: NonNull<c_void>,
            consume: F,
        ) {
            let mut consume = Some(consume);
            unsafe {
                (app.legacy.with_result)(
                    target.as_ptr(),
                    receive::<F>,
                    (&mut consume as *mut Option<F>).cast(),
                );
            }
            assert!(consume.is_none(), "missing result continuation");
        }
        match target {
            Target::Dialog(dialog) => dialog.with_result(consume),
            Target::NetworkModes(modes) => {
                let object = network_modes::object(modes);
                invoke(self, object, |result| {
                    consume(ActivityResult::Legacy(result))
                });
            }
            Target::Wifi(wifi) => {
                invoke(self, wifi::object(wifi), |result| {
                    consume(ActivityResult::Legacy(result))
                });
            }
            Target::Home(_) | Target::Epub(_) | Target::Settings(_) => {
                consume(ActivityResult::Legacy(core::ptr::null()))
            }
            Target::Keyboard(keyboard) => {
                // SAFETY: the current target is live; extraction ends before consume destroys it.
                unsafe { keyboard::with_result(keyboard, consume) }
            }
            Target::Legacy(target) => invoke(self, target, |result| {
                consume(ActivityResult::Legacy(result))
            }),
            Target::SettingsSection(target) => {
                invoke(self, target, |_| consume(ActivityResult::SectionComplete))
            }
            Target::Definition(target) => definition::with_result(target, consume),
            Target::ReaderChild { child, .. } => unsafe { receive_child_result(child, consume) },
        }
    }
    fn deliver_result(&self, parent: Target, result: ActivityResult) {
        match (parent, result) {
            (Target::Settings(root), ActivityResult::SectionComplete) => {
                unsafe { root.as_ref() }.returned()
            }
            (_, ActivityResult::Keyboard(result)) => keyboard::deliver(self, parent, result),
            (_, ActivityResult::Dialog(result)) => dialog::deliver(self, parent, result),
            (Target::Epub(epub), ActivityResult::Legacy(result)) => unsafe {
                epub.as_ref().child_result(result.as_ref())
            },
            (parent, ActivityResult::Child(result)) => unsafe {
                self.deliver_child_result(parent, result)
            },
            (Target::Definition(parent), ActivityResult::Legacy(result)) => {
                self.definition_result(parent, result)
            }
            (
                Target::Legacy(parent) | Target::SettingsSection(parent),
                ActivityResult::Legacy(result),
            ) => {
                let original =
                    unsafe { result.as_ref() }.map_or(core::ptr::null(), |view| view.original);
                unsafe { (self.legacy.deliver_result)(parent.as_ptr(), original) };
            }
            _ => {}
        }
    }
    fn failed_push_dropped(&self) {
        if let Some(Target::Definition(parent)) = self.navigation.current() {
            self.definition_selection_failed(parent);
        }
    }

    fn home(&self) -> Option<Owner> {
        self.new_home(0, false)
    }
    fn request_update(&self) {
        self.request(false);
    }
    fn missing_pop(&self) {
        unsafe { (self.runtime.diagnostic)(9, 0, 0) };
    }
    fn stack_allocation_failed(&self) {
        match self.navigation.current() {
            Some(Target::Epub(epub)) => unsafe { epub.as_ref().child_install_failed() },
            Some(Target::ReaderChild {
                child: child @ ChildTarget::Lookup(_),
                ..
            }) => self.definition_install_failed(&child),
            Some(Target::ReaderChild {
                child: ChildTarget::Clippings(_),
                ..
            }) => self.request(false),
            _ => {}
        }
        unsafe { (self.runtime.diagnostic)(2, 1, 0) };
    }
    fn prepare_sleep(&self, target: Target) {
        match target {
            Target::Epub(epub) => unsafe { epub.as_ref() }.flush_session(),
            Target::Legacy(target) | Target::SettingsSection(target) => unsafe {
                (self.legacy.prepare_sleep)(target.as_ptr())
            },
            Target::Home(_)
            | Target::NetworkModes(_)
            | Target::Wifi(_)
            | Target::Keyboard(_)
            | Target::Dialog(_)
            | Target::ReaderChild { .. }
            | Target::Definition(_)
            | Target::Settings(_) => {}
        }
    }
}

unsafe extern "C" fn facts() -> abi::Facts {
    let Some(app) = app() else {
        return unsafe { core::mem::zeroed() };
    };
    let pending = app.navigation.pending_kind() != TransitionKind::None;
    let current = app.navigation.current();
    let native = app.target_facts();
    let (visit, painted_visit) = app
        .signals
        .with(|signals| (signals.visit, signals.painted_visit));
    abi::Facts {
        visit,
        painted_visit,
        current_kind: native.map_or(255, |f| f.kind),
        pending: u8::from(pending),
        reader_in_stack: u8::from(app.navigation.has_reader()),
        foreground_reader: u8::from(!pending && native.is_some_and(|f| f.reader != 0)),
        remote_context: u8::from(!pending && native.is_some_and(|f| f.remote_input != 0)),
        foreground_home_shown: u8::from(
            !pending && matches!(current, Some(Target::Home(_))) && visit == painted_visit,
        ),
        prevent_sleep: native.map_or(0, |f| f.prevent_sleep),
        exclusive_storage: native.map_or(0, |f| f.exclusive_storage),
    }
}

unsafe extern "C" fn home_action() -> u8 {
    app().map_or(HomeAction::Ignore as u8, |app| {
        app.input.borrow().action() as u8
    })
}

unsafe extern "C" fn screenshot_info(sink: abi::ScreenshotSink, context: *mut c_void) {
    if let Some(app) = app() {
        match app.navigation.current() {
            Some(Target::Legacy(target) | Target::SettingsSection(target)) => {
                unsafe { (app.legacy.screenshot_info)(target.as_ptr(), sink, context) };
                return;
            }
            Some(Target::NetworkModes(modes)) => {
                unsafe {
                    (app.legacy.screenshot_info)(
                        network_modes::object(modes).as_ptr(),
                        sink,
                        context,
                    )
                };
                return;
            }
            Some(Target::Wifi(wifi)) => {
                unsafe { (app.legacy.screenshot_info)(wifi::object(wifi).as_ptr(), sink, context) };
                return;
            }
            Some(Target::Epub(epub)) => {
                let info = unsafe { epub.as_ref() }.screenshot_info();
                unsafe { sink(context, &info) };
                return;
            }
            _ => {}
        }
    }
    let empty = abi::Screenshot {
        reader_type: 0,
        title: [0; 64],
        spine: -1,
        current_page: 0,
        total_pages: 0,
        progress_percent: 0,
    };
    unsafe { sink(context, &empty) };
}

/// # Safety
/// Called once before tasks start. Tables and all callbacks remain valid for the
/// application lifetime; compatibility is aligned writable storage.
#[no_mangle]
pub unsafe extern "C" fn xp_app_init(
    platform: *const abi::Platform,
    compatibility: *mut abi::Compatibility,
) -> abi::Status {
    if app().is_some() {
        return abi::WRONG_CONTEXT;
    }
    let (Some(platform), Some(compatibility)) = (unsafe { platform.as_ref() }, unsafe {
        compatibility.as_mut()
    }) else {
        return abi::INVALID;
    };
    let (Some(legacy), Some(resources), Some(runtime), Some(epub)) = (
        unsafe { platform.legacy.as_ref() },
        unsafe { platform.resources.as_ref() },
        unsafe { platform.runtime.as_ref() },
        unsafe { platform.epub.as_ref() },
    ) else {
        return abi::INVALID;
    };
    let navigation = Application::new();
    let owner = App {
        clippings: clippings_service::Service::new(ModelCell::new(
            clippings_service::Phase::Idle,
            runtime,
        )),
        navigation,
        input: RefCell::new(HomeInput::default()),
        driver: Cell::new(driver::State::default()),
        signals: ModelCell::new(
            Signals {
                target: None,
                visit: 0,
                painted_visit: 0,
                requested: false,
            },
            runtime,
        ),
        widths: UnsafeCell::new(xp_ui::home::view::Widths::default()),
        #[cfg(feature = "app-test")]
        clipping_loaded_observer: Cell::new(None),
        #[cfg(feature = "app-test")]
        clipping_unloaded_observer: Cell::new(None),
        #[cfg(feature = "app-test")]
        legacy_entry_fixture: Cell::new(None),
        legacy,
        epub,
        resources,
        runtime,
        child_drawing: Some(&epub.child_drawing),
    };
    if let Some(physical) = unsafe { epub.clipping_physical.as_ref() } {
        owner.clippings.install(|| {
            clippings_facade::NativeStore::try_new(clippings_native::Native {
                leaves: epub,
                resources,
                physical,
            })
        });
    }
    unsafe { *APP.0.get() = Some(owner) };
    *compatibility = abi::Compatibility {
        activity_facts: facts,
        home_action,
        screenshot_info,
    };
    abi::OK
}

#[no_mangle]
pub extern "C" fn xp_app_loop() {
    if let Some(app) = app() {
        driver::run(app);
    }
}

/// # Safety
/// Token borrows the native RenderLock and power scope. The caller retains the
/// lock until return, except when a legacy render explicitly moves/unlocks it.
#[no_mangle]
pub unsafe extern "C" fn xp_app_render(token: *mut abi::RenderToken) {
    let (Some(app), Some(token)) = (app(), unsafe { token.as_mut() }) else {
        return;
    };
    let (target, visit) = app.signals.with(|signals| (signals.target, signals.visit));
    let painted = match target {
        Some(Target::Home(home)) => {
            unsafe { home.as_ref().render(&mut *app.widths.get()) }.is_some()
        }
        Some(Target::Legacy(target) | Target::SettingsSection(target)) => {
            unsafe { (app.legacy.render)(target.as_ptr(), token) };
            true
        }
        Some(Target::Definition(target)) => app.definition_render(target),
        Some(Target::Epub(epub)) => unsafe { epub.as_ref().render() },
        Some(Target::Keyboard(keyboard)) => keyboard::render(unsafe { keyboard.as_ref() }),
        Some(Target::Dialog(dialog)) => dialog.render(),
        Some(Target::NetworkModes(modes)) => network_modes::render(modes),
        Some(Target::Wifi(wifi)) => wifi::render(wifi),
        Some(Target::Settings(root)) => settings::render(unsafe { root.as_ref() }),
        Some(Target::ReaderChild { child, parent }) => unsafe {
            child.render(
                parent.as_ref(),
                app.child_drawing.expect("child drawing installed"),
            )
        },
        None => false,
    };
    if painted {
        app.signals.with(|signals| signals.painted_visit = visit);
    }
}

/// # Safety
/// Event is aligned and its tagged payload is valid for the call. Navigation and
/// input events run on the loop task; render may only request another frame.
#[no_mangle]
pub unsafe extern "C" fn xp_app_event(event: *const abi::Event) -> abi::EventResult {
    let (Some(app), Some(event)) = (app(), unsafe { event.as_ref() }) else {
        return abi::EVENT_INVALID;
    };
    let accepted = match event.kind {
        abi::ADOPT_REPLACE | abi::ADOPT_PUSH => {
            let pointer = unsafe {
                if event.kind == abi::ADOPT_PUSH {
                    event.value.adopt_push
                } else {
                    event.value.adopt_replace
                }
            };
            let Some(owner) = app.adopt(pointer) else {
                return abi::REJECTED;
            };
            app.input.borrow_mut().reset();
            if event.kind == abi::ADOPT_PUSH {
                app.navigation.queue(Transition::Push(owner));
            } else {
                app.navigation.replace(owner, app);
            }
            true
        }
        abi::POP => {
            app.input.borrow_mut().reset();
            app.navigation.queue(Transition::Pop);
            true
        }
        abi::HOME_REQUESTED => {
            let home = unsafe { event.value.home };
            app.go_home(home.menu, home.clean_initial != 0)
        }
        abi::NAVIGATE => app.navigate(unsafe { event.value.navigate }),
        abi::RENDER_REQUESTED => {
            app.request(unsafe { event.value.render_immediate } != 0);
            true
        }
        abi::SLEEP_REQUESTED => {
            let launch = abi::Launch::new(
                abi::SLEEP,
                LaunchPayload::Sleep(unsafe { event.value.sleep_from_timeout }),
            );
            let Some(owner) = app.create(&launch) else {
                return abi::EVENT_OUT_OF_MEMORY;
            };
            app.input.borrow_mut().reset();
            if app.navigation.current().is_some() {
                app.navigation.queue(Transition::Sleep(owner));
                app.navigation.drain(app);
            } else {
                app.navigation.replace(owner, app);
            }
            app.flush_update();
            true
        }
        abi::PREPARE_SLEEP => {
            app.navigation.prepare_sleep(app);
            true
        }
        abi::REMOTE_CLICK => app.remote_click(unsafe { event.value.remote_button }),
        abi::REMOTE_TURN | abi::REMOTE_SHORTCUT => {
            if app.navigation.pending_kind() != TransitionKind::None {
                return abi::REJECTED;
            }
            let Some(target) = app.navigation.current() else {
                return abi::REJECTED;
            };
            let facts = match target {
                Target::Legacy(target) | Target::SettingsSection(target) => unsafe {
                    (app.legacy.facts)(target.as_ptr(), 0, 1)
                },
                Target::Definition(_) => definition::facts(),
                Target::Epub(epub) => unsafe { epub.as_ref() }.app_facts(true),
                Target::ReaderChild { child, .. } => unsafe { child.facts() },
                Target::Home(_)
                | Target::Keyboard(_)
                | Target::Dialog(_)
                | Target::NetworkModes(_)
                | Target::Wifi(_)
                | Target::Settings(_) => return abi::REJECTED,
            };
            if event.kind == abi::REMOTE_SHORTCUT {
                if facts.reader == 0 || facts.remote_input == 0 || facts.input_over_page != 0 {
                    return abi::REJECTED;
                }
                let token = unsafe { (app.runtime.render_try_lock)() };
                if token.is_null() {
                    return abi::REJECTED;
                }
                drop(RenderGuard {
                    token,
                    runtime: app.runtime,
                });
                match target {
                    Target::Legacy(_) | Target::Definition(_) | Target::SettingsSection(_) => false,
                    Target::Epub(epub) => unsafe { epub.as_ref() }
                        .dispatch_remote_shortcut(unsafe { event.value.reader_menu } != 0),
                    Target::Home(_)
                    | Target::NetworkModes(_)
                    | Target::Wifi(_)
                    | Target::Keyboard(_)
                    | Target::Dialog(_)
                    | Target::Settings(_)
                    | Target::ReaderChild { .. } => false,
                }
            } else {
                let turn = unsafe { event.value.remote_turn };
                if facts.remote_input == 0 {
                    return abi::REJECTED;
                }
                if facts.reader == 0 || facts.input_over_page != 0 {
                    turn.chapter == 0 && app.remote_click(if turn.forward != 0 { 3 } else { 4 })
                } else {
                    let token = unsafe { (app.runtime.render_try_lock)() };
                    if token.is_null() {
                        return abi::REJECTED;
                    }
                    drop(RenderGuard {
                        token,
                        runtime: app.runtime,
                    });
                    let turned = match target {
                        Target::Legacy(target) | Target::SettingsSection(target) => unsafe {
                            (app.legacy.reader_turn)(target.as_ptr(), turn.forward, turn.chapter)
                                != 0
                        },
                        Target::Epub(epub) => unsafe { epub.as_ref() }
                            .app_remote_turn(turn.forward != 0, turn.chapter != 0),
                        Target::Home(_)
                        | Target::NetworkModes(_)
                        | Target::Wifi(_)
                        | Target::Keyboard(_)
                        | Target::Dialog(_)
                        | Target::ReaderChild { .. }
                        | Target::Definition(_)
                        | Target::Settings(_) => false,
                    };
                    if turned {
                        app.request(false);
                    }
                    turned
                }
            }
        }
        abi::FORCED_REFRESH => {
            return if app.forced_refresh() {
                abi::ACCEPTED
            } else {
                abi::REJECTED
            }
        }
        abi::INPUT_POLLED => {
            app.input_polled(unsafe { event.value.defer_home } != 0);
            true
        }
        abi::RESET_HOME_INPUT => {
            app.input.borrow_mut().reset();
            true
        }
        abi::BOOT_INPUT_STATE => {
            let input = unsafe { event.value.boot_input };
            let mut state = app.driver.get();
            state.allow_sleep_at = input.allow_sleep_at;
            state.wake_release_pending = input.wake_release_pending != 0;
            app.driver.set(state);
            true
        }
        _ => return abi::EVENT_INVALID,
    };
    if accepted {
        abi::ACCEPTED
    } else {
        abi::REJECTED
    }
}

// Host-oracle entry points. Firmware builds never enable app-test.
#[cfg(feature = "app-test")]
mod probe {
    use super::*;

    #[no_mangle]
    pub unsafe extern "C" fn xp_app_test_install_definition(
        title: abi::Text,
        length: u32,
        fill: unsafe extern "C" fn(*mut c_void, *mut u8, u32) -> u8,
        context: *mut c_void,
    ) -> u8 {
        let Some(app) = app() else { return 0 };
        let physical = &app.epub.child_selection_physical;
        let Some(owner) = (unsafe {
            Definition::try_plain(
                app.runtime,
                &physical.definition,
                title,
                length,
                fill,
                context,
            )
        }) else {
            return 0;
        };
        app.navigation
            .queue(Transition::Push(Owner::Definition(owner)));
        1
    }
    #[no_mangle]
    pub extern "C" fn xp_app_test_published_legacy() -> *mut c_void {
        app().map_or(core::ptr::null_mut(), |app| {
            app.signals.with(|signals| match signals.target {
                Some(Target::Legacy(pointer)) => pointer.as_ptr(),
                _ => core::ptr::null_mut(),
            })
        })
    }

    // Fixture installs physical leaves before any activity or render task exists.
    #[no_mangle]
    pub unsafe extern "C" fn xp_app_test_child_drawing(drawing: *const Physical) -> u8 {
        let Some(app) = (unsafe { &mut *APP.0.get() }).as_mut() else {
            return 0;
        };
        if app.navigation.current().is_some()
            || app.navigation.pending_kind() != TransitionKind::None
        {
            return 0;
        }
        app.child_drawing = unsafe { drawing.as_ref() };
        u8::from(app.child_drawing.is_some())
    }

    #[no_mangle]
    pub extern "C" fn xp_app_test_activity_loop() {
        if let Some(app) = app() {
            app.activity_loop();
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn xp_app_test_current_child(kind: *mut u8) -> *mut c_void {
        let Some(Target::ReaderChild { child, .. }) =
            app().and_then(|app| app.navigation.current())
        else {
            return core::ptr::null_mut();
        };
        let (tag, pointer) = match child {
            ChildTarget::Menu(p) => (1, p.as_ptr().cast()),
            ChildTarget::Chapters(p) => (2, p.as_ptr().cast()),
            ChildTarget::Percent(p) => (3, p.as_ptr().cast()),
            ChildTarget::Qr(p) => (4, p.as_ptr().cast()),
            ChildTarget::Footnote(p) => (5, p.as_ptr().cast()),
            ChildTarget::Lookup(p) => (6, p.as_ptr().cast()),
            ChildTarget::Clippings(p) => (7, p.as_ptr().cast()),
            ChildTarget::Bookmarks(p) => (8, p.as_ptr().cast()),
        };
        if let Some(kind) = unsafe { kind.as_mut() } {
            *kind = tag;
        }
        pointer
    }

    #[no_mangle]
    pub extern "C" fn xp_app_test_drain() -> u8 {
        let Some(app) = app() else { return 0 };
        let drained = app.navigation.drain(app);
        app.flush_update();
        u8::from(drained)
    }

    // Host fixtures inject independently faulted physical resource tables.
    #[no_mangle]
    pub unsafe extern "C" fn xp_app_test_install_epub(
        path: abi::Text,
        fast_initial: u8,
        leaves: *const epub::abi::XpEpubLeaves,
    ) -> u8 {
        let (Some(app), Some(path), Some(leaves)) =
            (app(), unsafe { path.bytes() }, unsafe { leaves.as_ref() })
        else {
            return 0;
        };
        let Some(owner) = epub::EpubSession::try_owned(
            path,
            fast_initial != 0,
            leaves,
            app.runtime,
            app.resources,
        ) else {
            return 0;
        };
        app.replace(Owner::Epub(owner.into()));
        1
    }

    #[no_mangle]
    pub extern "C" fn xp_app_test_current_epub() -> *mut c_void {
        match app().and_then(|app| app.navigation.current()) {
            Some(Target::Epub(epub)) => epub.as_ptr().cast(),
            _ => core::ptr::null_mut(),
        }
    }

    #[no_mangle]
    pub extern "C" fn xp_app_test_published_epub() -> *mut c_void {
        app().map_or(core::ptr::null_mut(), |app| {
            app.signals.with(|signals| match signals.target {
                Some(Target::Epub(epub)) => epub.as_ptr().cast(),
                _ => core::ptr::null_mut(),
            })
        })
    }

    #[no_mangle]
    pub extern "C" fn xp_app_test_current_legacy() -> *mut c_void {
        match app().and_then(|app| app.navigation.current()) {
            Some(Target::Legacy(target) | Target::SettingsSection(target)) => target.as_ptr(),
            _ => core::ptr::null_mut(),
        }
    }
    #[no_mangle]
    pub extern "C" fn xp_app_test_is_home() -> u8 {
        u8::from(matches!(
            app().and_then(|app| app.navigation.current()),
            Some(Target::Home(_))
        ))
    }
    #[no_mangle]
    pub extern "C" fn xp_app_test_parent_count() -> usize {
        app().map_or(0, |app| app.navigation.parent_count())
    }
    #[no_mangle]
    pub extern "C" fn xp_app_test_pending_kind() -> u8 {
        match app().map_or(TransitionKind::None, |app| app.navigation.pending_kind()) {
            TransitionKind::None => 0,
            TransitionKind::Push => 1,
            TransitionKind::Pop => 2,
            TransitionKind::Replace => 3,
            TransitionKind::Sleep => 4,
        }
    }
}

#[cfg(test)]
mod keyboard_tests;

#[cfg(test)]
mod dialog_tests;

#[cfg(test)]
mod settings_tests;

#[cfg(test)]
mod home_tests;

#[cfg(test)]
mod home_test_allocator;
