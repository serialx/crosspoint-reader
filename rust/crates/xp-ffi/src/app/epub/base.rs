use super::{abi, app_abi, book::with_text, EpubSession, ResourceAccess};
use core::{ffi::c_void, mem::MaybeUninit, ptr::NonNull};
use xp_ui::{
    reader::{
        input,
        render::{Kind, Op},
        values, SKIP_HOLD_MS,
    },
    scene::Refresh,
};

impl EpubSession {
    fn log_memory(&self, entering: bool) {
        let mut heap = MaybeUninit::<abi::XpEpubHeap>::uninit();
        unsafe { (self.leaves.heap_snapshot)(heap.as_mut_ptr()) };
        let heap = unsafe { heap.assume_init() };
        self.loop_log_formatted(
            2,
            b"MEM",
            format_args!(
                "reader {}: free={} max_block={}",
                if entering { "enter" } else { "exit" },
                heap.free_bytes,
                heap.largest_block
            ),
            &[],
        );
    }

    fn activity_log(&self, entering: bool) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe {
                if entering {
                    (hook.enter)(hook.context);
                } else {
                    (hook.exit)(hook.context);
                }
            }
            return;
        }
        unsafe {
            (self.leaves.log)(
                0,
                app_abi::Text::from_bytes(b"ACT"),
                app_abi::Text::from_bytes(if entering {
                    b"Entering activity: EpubReader"
                } else {
                    b"Exiting activity: EpubReader"
                }),
            )
        };
    }

    #[inline(never)]
    pub(in crate::app) fn enter(&self) {
        self.activity_log(true);
        self.log_memory(true);
        if unsafe { (self.resources.path_exists)(self.path.wire()) } == 0 {
            self.loop_log_at(1, b"READER", &[b"File does not exist: ", self.path.bytes()]);
            self.finish();
            return;
        }
        let clear = with_text(
            |sink, context| unsafe { (self.leaves.state_open_path)(sink, context) },
            |path| path.len != 0,
        )
        .unwrap_or(false);
        if clear {
            unsafe {
                (self.leaves.state_set_open_path)(app_abi::Text::from_bytes(&b"\0"[..0]));
                (self.leaves.state_save)();
            }
        }
        unsafe { (self.leaves.fonts_ensure_loaded)() };
        self.apply_reader_initial_orientation();
        if !self.entry_load_book() {
            if !self.handle_load_failure() {
                self.finish();
            }
            return;
        }
        self.request_update();
    }

    pub(super) fn remember_book(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            unsafe { (hooks.remember)(hooks.context) };
            return;
        }
        let remember = self.model(|state| {
            if state.base.remembered || !state.base.rendered {
                return false;
            }
            state.base.remembered = true;
            true
        });
        if !remember {
            return;
        }
        unsafe {
            (self.leaves.state_set_open_path)(self.path.wire());
            (self.leaves.state_save)();
            if let Some(book) = self.loop_book() {
                (self.leaves.recents_add)(book.as_ptr());
            }
            (self.leaves.emit_open)(self.path.wire());
        }
    }

    pub(in crate::app) fn flush_session(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            unsafe { (hooks.flush)(hooks.context) };
            return;
        }
        let (session, worthy) = self.base_session_snapshot();
        if !worthy || unsafe { (self.leaves.has_session_subscriber)() } == 0 {
            self.reset_session();
            return;
        }
        let consumed = with_text(
            |sink, context| unsafe { (self.leaves.document_id)(self.path.wire(), sink, context) },
            |document| {
                let bytes = unsafe { document.bytes() }.unwrap_or_default();
                if bytes.len() == 32
                    && bytes
                        .iter()
                        .all(|byte| matches!(byte, b'0'..=b'9' | b'a'..=b'f'))
                {
                    let event = abi::XpEpubSessionEvent {
                        start_time: session.start_epoch,
                        end_time: session
                            .start_epoch
                            .wrapping_add(i64::from(session.duration)),
                        duration_seconds: session.duration,
                        start_progress: session.start_progress,
                        end_progress: session.end_progress,
                    };
                    unsafe { (self.leaves.emit_session)(self.path.wire(), document, &event) };
                }
                // The native document remains borrowed until after reset.
                self.reset_session();
            },
        );
        if consumed.is_none() {
            self.reset_session();
        }
    }

    pub(super) fn exit(&self, access: &ResourceAccess<'_>) {
        self.activity_log(false);
        unsafe { (self.leaves.fonts_release_caches)() };
        self.log_memory(false);
        self.flush_session();
        if unsafe { (self.leaves.has_exit_subscriber)() } != 0 {
            let percent = self.base_exit_percent();
            unsafe { (self.leaves.emit_exit)(self.path.wire(), percent) };
        }
        unsafe {
            (self.leaves.render_orientation)(0);
            (self.leaves.state_reset_reader_count)();
            (self.leaves.state_save)();
        }
        self.drop_end_options(access);
        self.model(|state| state.base.end_ready = false);
    }

    fn base_exit_percent(&self) -> i32 {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.percent)(hook.context) };
        }
        self.screenshot_info().progress_percent
    }

    fn base_session_snapshot(&self) -> (super::state::SessionAccounting, bool) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            let wire = unsafe { (hook.snapshot)(hook.context) };
            let worthy = unsafe { (hook.worthy)(hook.context) != 0 };
            return (
                super::state::SessionAccounting {
                    anchor_ms: wire.anchor_ms,
                    anchor_epoch: wire.anchor_epoch,
                    start_epoch: wire.start_epoch,
                    duration: wire.duration,
                    anchor_progress: wire.anchor_progress,
                    start_progress: wire.start_progress,
                    end_progress: wire.end_progress,
                    has_anchor: wire.has_anchor != 0,
                    pending_forward: wire.pending_forward != 0,
                },
                worthy,
            );
        }
        let session = self.snapshot_session();
        (session, session.worthy())
    }

    fn drop_end_options(&self, access: &ResourceAccess<'_>) {
        if let Some(end) = unsafe { (*self.guarded(access)).end_options.take() } {
            unsafe { (self.leaves.end_close)(end.as_ptr()) };
        }
    }

    pub(super) fn clear_end_options(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            unsafe { (hooks.clear_end)(hooks.context) };
            return;
        }
        if self.is_at_end() || !self.model(|state| state.base.end_ready) {
            return;
        }
        let access = self.lock_resources();
        self.model(|state| state.base.end_ready = false);
        self.drop_end_options(&access);
    }

    fn open_reader_path(&self, path: app_abi::Text) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.navigate)(hook.context, path) };
            return;
        }
        let app = super::super::app().expect("application initialized");
        app.navigate(app_abi::Launch::new(
            app_abi::READER,
            app_abi::LaunchPayload::Reader(app_abi::ReaderLaunch {
                path,
                fast_initial: 0,
            }),
        ));
    }

    pub(super) fn go_home(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.home)(hooks.context) };
            return;
        }
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            unsafe { (hook.home)(hook.context) };
            return;
        }
        let app = super::super::app().expect("application initialized");
        app.go_home(0, false);
    }

    pub(super) fn input_config(&self) -> input::Config {
        let settings = self.settings(false);
        input::Config {
            long_press: settings.long_button,
            short_power: settings.short_power,
            tilt: settings.tilt,
            touch_controls: settings.touch_controls,
            next_gesture: settings.page_gesture,
            prev_gesture: settings.previous_gesture,
            menu_gesture: settings.show_menu,
            back_short_browser: settings.back_to_browser,
        }
    }

    pub(super) fn touch_turn(&self) -> input::Turn {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.touch)(hook.context) };
        }
        let mut geometry = MaybeUninit::<abi::XpEpubGeometry>::uninit();
        unsafe { (self.leaves.render_geometry)(geometry.as_mut_ptr()) };
        let geometry = unsafe { geometry.assume_init() };
        unsafe {
            input::touch(
                *(self.leaves.input_services)(),
                self.input_config(),
                geometry.width,
                geometry.height,
                false,
            )
        }
    }

    pub(super) fn button_turn(&self) -> input::Turn {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.buttons)(hook.context) };
        }
        unsafe { input::buttons(*(self.leaves.input_services)(), self.input_config()) }
    }

    pub(super) fn handle_back(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.back)(hook.context) != 0 };
        }
        match unsafe {
            input::back(
                *(self.leaves.input_services)(),
                self.settings(false).back_to_browser != 0,
            )
        } {
            1 => {
                let app = super::super::app().expect("application initialized");
                app.navigate(app_abi::Launch::new(
                    app_abi::FILES,
                    app_abi::LaunchPayload::Files(self.path.wire()),
                ));
                true
            }
            2 => {
                self.go_home();
                true
            }
            _ => false,
        }
    }

    #[inline(never)]
    pub(super) fn loop_base(&self) {
        self.remember_book();
        self.clear_end_options();
        if self.handle_end_menu(false) || self.format_input() || self.handle_back() {
            return;
        }
        let touch = self.touch_turn();
        let buttons = self.button_turn();
        let previous = buttons.prev != 0 || touch.prev != 0;
        let next = buttons.next != 0 || touch.next != 0;
        if !previous && !next {
            return;
        }
        if self.handle_end_turn(previous, next) {
            return;
        }
        let touched = touch.prev != 0 || touch.next != 0;
        let held = if touched {
            touch.held_ms
        } else {
            let services = unsafe { &*(self.leaves.input_services)() };
            unsafe {
                services.read.expect("reader input")(services.context, input::Query::HeldMs as u8)
            }
        };
        let skip = buttons.from_tilt == 0
            && u32::from(self.settings(false).long_button) == values::LONG_BUTTON_SKIP
            && held >= SKIP_HOLD_MS;
        let changed = if skip {
            self.skip_pages(if previous { -10 } else { 10 })
        } else {
            self.page_turn(!previous)
        };
        self.note_page_turn(!skip && !previous, changed);
        if changed && touched {
            unsafe { (self.runtime.touch_feedback)(u8::from(skip)) };
        }
        self.request_update();
    }

    fn format_input(&self) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.base_hooks {
            return unsafe { (hook.format)(hook.context) != 0 };
        }
        false
    }

    pub(super) fn handle_end_menu(&self, suppressed: bool) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return unsafe { (hooks.end_menu)(hooks.context) != 0 };
        }
        if suppressed || !self.end_menu_active() {
            return false;
        }
        struct Selection<'a> {
            owner: &'a EpubSession,
            handled: bool,
        }
        unsafe extern "C" fn selected(context: *mut c_void, action: u8, path: app_abi::Text) {
            let selection = &mut *context.cast::<Selection<'_>>();
            selection.handled = match u32::from(action) {
                values::END_OPEN_BOOK => {
                    selection.owner.open_reader_path(path);
                    true
                }
                values::END_GO_HOME => {
                    selection.owner.go_home();
                    true
                }
                values::END_LAST_PAGE => {
                    selection.owner.return_from_end();
                    selection.owner.request_update();
                    true
                }
                values::END_REDRAW => {
                    selection.owner.request_update();
                    true
                }
                _ => false,
            };
        }
        // end_menu_active acquired the widget's completed-list publication.
        let end = unsafe { (*self.guarded.get()).end_options.expect("end menu") };
        let mut selection = Selection {
            owner: self,
            handled: false,
        };
        unsafe {
            (self.leaves.end_input)(
                end.as_ptr(),
                selected,
                (&mut selection as *mut Selection<'_>).cast(),
            )
        };
        selection.handled
    }

    pub(super) fn handle_end_turn(&self, previous: bool, next: bool) -> bool {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.loop_hooks {
            return unsafe {
                (hooks.end_turn)(hooks.context, u8::from(previous), u8::from(next)) != 0
            };
        }
        if !self.is_at_end() {
            return false;
        }
        if self.model(|state| state.base.end_ready) {
            let end = unsafe {
                (*self.guarded.get())
                    .end_options
                    .expect("ready end options")
            };
            if unsafe { (self.leaves.end_menu_active)(end.as_ptr()) } != 0 {
                return true;
            }
        }
        if next {
            self.go_home();
        } else if previous {
            self.return_from_end();
            self.request_update();
        }
        true
    }

    pub(super) fn render_base(&self, access: &ResourceAccess<'_>, render_book: impl FnOnce()) {
        if self.is_at_end() {
            if unsafe { (*self.guarded(access)).end_options.is_none() } {
                let end = NonNull::new(unsafe { (self.leaves.end_open)() });
                unsafe { (*self.guarded(access)).end_options = end };
                if end.is_none() {
                    unsafe {
                        (self.leaves.log)(
                            1,
                            app_abi::Text::from_bytes(b"READER"),
                            app_abi::Text::from_bytes(b"OOM: EndOfBookOptions"),
                        )
                    };
                }
            }
            self.render_operation(
                access,
                Op {
                    kind: Kind::Clear as u8,
                    value: 255,
                    ..Op::default()
                },
            );
            if let Some(end) = unsafe { (*self.guarded(access)).end_options } {
                unsafe { (self.leaves.end_load)(end.as_ptr(), self.path.wire()) };
                self.mark_end_ready(access);
                unsafe { (self.leaves.end_paint)(end.as_ptr()) };
            }
            self.render_operation(
                access,
                Op {
                    kind: Kind::Display as u8,
                    mode: Refresh::Fast as u8,
                    ..Op::default()
                },
            );
            self.end_rendered();
            self.mark_page_rendered(access);
            self.complete_session(access);
        } else {
            render_book();
            self.complete_session(access);
        }
    }

    pub(in crate::app) fn force_refresh(&self) -> bool {
        self.queue_forced_refresh();
        self.request_update();
        true
    }
}
