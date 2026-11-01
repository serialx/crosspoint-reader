extern crate alloc;

mod abi;
mod cover_cache;
mod driver;
mod home;
mod home_books;
mod home_text;
mod resources;
mod result;
mod sync;
#[cfg(test)]
mod test_resources;

use alloc::{alloc::alloc, boxed::Box};
use core::{
    alloc::Layout,
    cell::{Cell, RefCell, UnsafeCell},
    ffi::c_void,
    ptr::NonNull,
};
use sync::ModelCell;
use xp_ui::{
    app::{
        input::{Feedback, HomeAction, HomeBindings, HomeContact, HomeInput},
        result::OwnedResult,
        Activity, Application, Services, Transition, TransitionKind,
    },
    home::activity::InputResult,
    model::EffectKind,
};

#[derive(Clone, Copy)]
enum Target {
    Home(NonNull<home::Home>),
    Legacy(NonNull<c_void>),
}

enum Owner {
    Home(Box<home::Home>),
    Legacy {
        object: NonNull<c_void>,
        services: &'static abi::Legacy,
        reader: bool,
    },
}

impl Activity for Owner {
    type Target = Target;
    fn target(&self) -> Target {
        match self {
            Self::Home(home) => Target::Home(NonNull::from(home.as_ref())),
            Self::Legacy { object, .. } => Target::Legacy(*object),
        }
    }
    fn is_reader(&self) -> bool {
        matches!(self, Self::Legacy { reader: true, .. })
    }
}

impl Drop for Owner {
    fn drop(&mut self) {
        if let Self::Legacy {
            object, services, ..
        } = self
        {
            unsafe { (services.destroy)(object.as_ptr()) };
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
    navigation: Application<Owner>,
    input: RefCell<HomeInput>,
    driver: Cell<driver::State>,
    signals: ModelCell<Signals>,
    widths: UnsafeCell<xp_ui::home::view::Widths>,
    legacy: &'static abi::Legacy,
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

fn try_box<T>(value: T) -> Option<Box<T>> {
    let pointer = NonNull::new(unsafe { alloc(Layout::new::<T>()) }.cast::<T>())?;
    unsafe {
        pointer.as_ptr().write(value);
        Some(Box::from_raw(pointer.as_ptr()))
    }
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
    fn legacy_facts(&self, target: NonNull<c_void>) -> abi::LegacyFacts {
        unsafe { (self.legacy.facts)(target.as_ptr()) }
    }

    fn target_facts(&self) -> Option<abi::LegacyFacts> {
        match self.navigation.current()? {
            Target::Legacy(target) => Some(self.legacy_facts(target)),
            Target::Home(_) => None,
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
        try_box(home::Home::new(
            self.resources,
            self.runtime,
            request_update,
            menu,
            clean_initial,
        ))
        .map(Owner::Home)
    }

    fn adopt(&self, object: *mut c_void) -> Option<Owner> {
        let object = NonNull::new(object)?;
        let reader = self.legacy_facts(object).reader != 0;
        Some(Owner::Legacy {
            object,
            services: self.legacy,
            reader,
        })
    }

    fn create(&self, launch: &abi::Launch) -> Option<Owner> {
        if !matches!(
            launch.kind,
            abi::FILES
                | abi::LIBRARY
                | abi::OPDS
                | abi::OPDS_PICKER
                | abi::PLUGINS
                | abi::FILE_TRANSFER
                | abi::USB_DRIVE
                | abi::SETTINGS
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
                launch = abi::Launch {
                    kind: abi::FILES,
                    value: abi::LaunchValue {
                        file_path: abi::Text::from_bytes(b"/"),
                    },
                };
            } else if image_path(path) {
                launch = abi::Launch {
                    kind: abi::BITMAP,
                    value: abi::LaunchValue {
                        bitmap_path: unsafe { launch.value.reader.path },
                    },
                };
            }
        }
        if launch.kind == abi::OPDS_PICKER && unsafe { (self.resources.opds_count)() } == 1 {
            launch = abi::Launch {
                kind: abi::OPDS,
                value: abi::LaunchValue { opds_server: 0 },
            };
        }
        let Some(owner) = self.create(&launch) else {
            return false;
        };
        self.replace(owner);
        true
    }

    fn snapshot(&self, poll: bool, defer_home: bool, consume_release: bool) -> abi::MappedSnapshot {
        let mut frame = core::mem::MaybeUninit::uninit();
        unsafe {
            (self.runtime.input_mapped_snapshot)(
                u8::from(poll),
                u8::from(defer_home),
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
        let frame = self.snapshot(false, defer, false);
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
        let frame = self.snapshot(false, false, true);
        if frame.suppressed_release != 0 {
            return;
        }
        if let Some(target) = self.navigation.current() {
            let facts = match target {
                Target::Legacy(target) => Some(self.legacy_facts(target)),
                Target::Home(_) => None,
            };
            if facts.is_some_and(|f| f.exclusive_storage != 0) {
                if let Target::Legacy(target) = target {
                    unsafe { (self.legacy.r#loop)(target.as_ptr()) };
                }
                self.flush_update();
                return;
            }
            if let Target::Legacy(target) = target {
                if frame.home_gesture != 0 {
                    if unsafe { (self.legacy.home_gesture)(target.as_ptr()) } == 0 {
                        self.go_home(0, false);
                    }
                    return;
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
                if let Some(panel) = self.create(&abi::Launch {
                    kind: abi::FRONTLIGHT_PANEL,
                    value: abi::LaunchValue { opds_server: 0 },
                }) {
                    self.input.borrow_mut().reset();
                    self.navigation.queue(Transition::Push(panel));
                }
                return;
            }
            match target {
                Target::Legacy(target) => unsafe { (self.legacy.r#loop)(target.as_ptr()) },
                Target::Home(home) => match unsafe { home.as_ref() }.input(frame.input) {
                    InputResult::Idle => {}
                    InputResult::RetryPaint => self.request(false),
                    InputResult::Update(update) => {
                        if update.effect.invalidate != 0 {
                            self.request(false);
                        }
                        if update.haptic != 0 {
                            unsafe { (self.runtime.touch_feedback)(0) };
                        }
                        if update.effect.kind == EffectKind::Activate as u8 {
                            let value = update.effect.value;
                            if (0..100).contains(&value) {
                                if let Some(path) = unsafe { home.as_ref().path(value as usize) } {
                                    self.navigate(abi::Launch {
                                        kind: abi::READER,
                                        value: abi::LaunchValue {
                                            reader: abi::ReaderLaunch {
                                                path,
                                                fast_initial: 0,
                                            },
                                        },
                                    });
                                }
                            } else {
                                let kind = match value {
                                    xp_ui::home::FILES => abi::FILES,
                                    xp_ui::home::LIBRARY => abi::LIBRARY,
                                    xp_ui::home::OPDS => abi::OPDS_PICKER,
                                    xp_ui::home::PLUGINS => abi::PLUGINS,
                                    xp_ui::home::TRANSFER => abi::FILE_TRANSFER,
                                    xp_ui::home::SETTINGS => abi::SETTINGS,
                                    _ => 255,
                                };
                                let flag = if kind == abi::PLUGINS {
                                    u8::from(unsafe { (self.resources.opds_count)() } != 0)
                                } else if kind == abi::SETTINGS {
                                    6
                                } else {
                                    0
                                };
                                self.navigate(abi::Launch {
                                    kind,
                                    value: abi::LaunchValue {
                                        settings_section: flag,
                                    },
                                });
                            }
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
            Some(Target::Legacy(target)) => unsafe {
                (self.legacy.forced_refresh)(target.as_ptr()) != 0
            },
            _ => false,
        }
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
    type Result = OwnedResult;
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
            Target::Legacy(target) => unsafe { (self.legacy.enter)(target.as_ptr()) },
        }
    }
    fn exit(&self, target: Target) {
        match target {
            Target::Home(home) => unsafe { home.as_ref() }.exit(),
            Target::Legacy(target) => unsafe { (self.legacy.exit)(target.as_ptr()) },
        }
    }
    fn take_result(&self, target: Target) -> OwnedResult {
        match target {
            Target::Home(_) => xp_ui::app::result::ActivityResult {
                cancelled: false,
                data: xp_ui::app::result::Data::None,
            },
            Target::Legacy(target) => {
                let mut transfer = result::Transfer::new();
                let accepted = unsafe {
                    (self.legacy.take_result)(
                        target.as_ptr(),
                        result::Transfer::receive,
                        (&mut transfer as *mut result::Transfer).cast(),
                    )
                } != 0;
                let (result, failure) = transfer.finish(accepted);
                if let Some(kind) = failure {
                    unsafe { (self.runtime.diagnostic)(10, kind.into(), 0) };
                }
                result
            }
        }
    }
    fn deliver_result(&self, parent: Target, result: OwnedResult) {
        if let Target::Legacy(parent) = parent {
            unsafe { (self.legacy.deliver_result)(parent.as_ptr(), &result::view(&result)) };
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
        unsafe { (self.runtime.diagnostic)(2, 1, 0) };
    }
    fn prepare_sleep(&self, target: Target) {
        if let Target::Legacy(target) = target {
            unsafe { (self.legacy.prepare_sleep)(target.as_ptr()) };
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
        skip_delay: native.map_or(0, |f| f.skip_delay),
    }
}

unsafe extern "C" fn home_action() -> u8 {
    app().map_or(HomeAction::Ignore as u8, |app| {
        app.input.borrow().action() as u8
    })
}

unsafe extern "C" fn screenshot_info(sink: abi::ScreenshotSink, context: *mut c_void) {
    if let Some(app) = app() {
        if let Some(Target::Legacy(target)) = app.navigation.current() {
            unsafe { (app.legacy.screenshot_info)(target.as_ptr(), sink, context) };
            return;
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
    let (Some(legacy), Some(resources), Some(runtime)) = (
        unsafe { platform.legacy.as_ref() },
        unsafe { platform.resources.as_ref() },
        unsafe { platform.runtime.as_ref() },
    ) else {
        return abi::INVALID;
    };
    let Ok(navigation) = Application::new() else {
        return abi::OUT_OF_MEMORY;
    };
    let owner = App {
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
        legacy,
        resources,
        runtime,
    };
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
        Some(Target::Legacy(target)) => {
            unsafe { (app.legacy.render)(target.as_ptr(), token) };
            true
        }
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
            let launch = abi::Launch {
                kind: abi::SLEEP,
                value: abi::LaunchValue {
                    sleep_from_timeout: unsafe { event.value.sleep_from_timeout },
                },
            };
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
            let Some(Target::Legacy(target)) = app.navigation.current() else {
                return abi::REJECTED;
            };
            let facts = app.legacy_facts(target);
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
                unsafe {
                    (app.legacy.reader_shortcut)(target.as_ptr(), event.value.reader_menu) != 0
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
                    let turned = unsafe {
                        (app.legacy.reader_turn)(target.as_ptr(), turn.forward, turn.chapter)
                    } != 0;
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
