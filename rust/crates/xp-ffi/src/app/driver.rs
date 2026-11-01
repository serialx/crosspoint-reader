use super::{abi, facts, App, RenderGuard};
use xp_ui::{app::input::HomeAction, model::POWER};

#[derive(Clone, Copy)]
pub(super) struct State {
    pub allow_sleep_at: u32,
    pub wake_release_pending: bool,
    last_activity: Option<u32>,
    last_memory: u32,
    maximum_loop: u32,
    gauge_pending: bool,
    screenshot_released: bool,
    screenshot_active: bool,
    power_released_since_wake: bool,
    last_power_click: u32,
    applied_slide_action: Option<u8>,
    applied_slide_on: bool,
}

impl Default for State {
    fn default() -> Self {
        Self {
            allow_sleep_at: 0,
            wake_release_pending: false,
            last_activity: None,
            last_memory: 0,
            maximum_loop: 0,
            gauge_pending: true,
            screenshot_released: true,
            screenshot_active: false,
            power_released_since_wake: false,
            last_power_click: 0,
            applied_slide_action: None,
            applied_slide_on: false,
        }
    }
}

pub(super) fn run(app: &App) {
    let mut state = app.driver.get();
    iteration(app, &mut state);
    app.driver.set(state);
}

fn now(app: &App) -> u32 {
    unsafe { (app.runtime.now_ms)() }
}
fn elapsed(app: &App, since: u32) -> u32 {
    now(app).wrapping_sub(since)
}
fn sleep(app: &App, timeout: bool) {
    unsafe { (app.runtime.sleep_enter)(u8::from(timeout)) };
}

fn toggle_light(app: &App, settings: &mut abi::Settings) {
    if unsafe { (app.runtime.frontlight_present)() } == 0 {
        return;
    }
    let on = unsafe { (app.runtime.frontlight_state)() } == 0;
    settings.frontlight_on = u8::from(on);
    unsafe {
        (app.runtime.frontlight_set)(u8::from(on));
        (app.runtime.settings_save)(settings);
        (app.runtime.diagnostic)(11, u32::from(on), 0);
    }
}

fn slide_switch(
    app: &App,
    state: &mut State,
    frame: &abi::MappedSnapshot,
    settings: &mut abi::Settings,
) {
    if frame.has_switch == 0 {
        return;
    }
    let on = frame.switch_on != 0;
    if state.applied_slide_action == Some(settings.slide_action) && state.applied_slide_on == on {
        return;
    }
    state.applied_slide_action = Some(settings.slide_action);
    state.applied_slide_on = on;
    match settings.slide_action {
        1 => {
            if (settings.screen_inverted != 0) == on {
                return;
            }
            settings.screen_inverted = u8::from(on);
        }
        2 => {
            if (settings.touch_reader != 0) == on {
                return;
            }
            settings.touch_reader = u8::from(on);
        }
        _ => {
            if unsafe { (app.runtime.frontlight_present)() } == 0
                || (unsafe { (app.runtime.frontlight_state)() } != 0) == on
            {
                return;
            }
            unsafe { (app.runtime.frontlight_set)(u8::from(on)) };
            settings.frontlight_on = u8::from(on);
        }
    }
    unsafe { (app.runtime.settings_save)(settings) };
    app.request(false);
    unsafe { (app.runtime.diagnostic)(12, u32::from(on), 0) };
}

fn iteration(app: &App, state: &mut State) {
    let started = now(app);
    let mut settings = app.settings();
    unsafe { (app.runtime.input_shared_power_mode)(u8::from(settings.short_power == 1)) };
    let frame = app.snapshot(true, false, false);
    let current = unsafe { facts() };
    let scene = abi::BleScene {
        visit: current.visit,
        r#where: if current.remote_context != 0 {
            1
        } else if current.foreground_home_shown != 0 {
            3
        } else {
            0
        },
        page_shown: u8::from(current.remote_context != 0 || current.foreground_home_shown != 0),
        storage_busy: current.exclusive_storage,
        wifi_on: 0,
    };
    let remote_acted = unsafe { (app.runtime.ble_tick)(&scene) } != 0;
    if current.exclusive_storage != 0 {
        app.activity_loop();
        let prevent_sleep = unsafe { facts() }.prevent_sleep != 0;
        unsafe {
            (app.runtime.power_saving)(u8::from(!prevent_sleep));
            (app.runtime.delay_ms)(if prevent_sleep { 10 } else { 50 });
        }
        return;
    }
    slide_switch(app, state, &frame, &mut settings);
    let tilt_activity = unsafe {
        (app.runtime.tilt_update)(
            settings.tilt,
            settings.reader_orientation,
            current.reader_in_stack,
        )
    } != 0;
    if state.gauge_pending {
        let token = unsafe { (app.runtime.render_try_lock)() };
        if !token.is_null() {
            let _guard = RenderGuard {
                token,
                runtime: app.runtime,
            };
            state.gauge_pending = unsafe { (app.runtime.battery_capacity_step)() } != 0;
        }
    }
    unsafe { (app.runtime.fading_fix)(settings.fading_fix) };
    if elapsed(app, state.last_memory) >= 10_000 {
        unsafe { (app.runtime.memory_diagnostic)() };
        state.last_memory = now(app);
    }
    unsafe { (app.runtime.console_service)() };
    let last_activity = state.last_activity.get_or_insert_with(|| now(app));
    if frame.any_pressed != 0
        || frame.any_released != 0
        || frame.touch_activity != 0
        || tilt_activity
        || remote_acted
        || unsafe { facts() }.prevent_sleep != 0
    {
        *last_activity = now(app);
        unsafe { (app.runtime.power_saving)(0) };
    }
    if state.wake_release_pending && frame.power_held == 0 {
        state.wake_release_pending = false;
        return;
    }
    if frame.power_held != 0 && frame.physical_down != 0 {
        state.screenshot_active = true;
        if state.screenshot_released {
            state.screenshot_released = false;
            let _guard = <App as xp_ui::app::Services<super::Owner>>::render_lock(app);
            unsafe { (app.runtime.screenshot)() };
        }
        return;
    }
    if state.screenshot_active {
        if frame.power_held != 0 {
            return;
        }
        state.screenshot_released = true;
        state.screenshot_active = false;
        if frame.power_released != 0 {
            return;
        }
    }
    let double_click = settings.is_x4pro != 0 && settings.double_power_light != 0;
    if double_click && frame.power_released != 0 {
        let now = now(app);
        if frame.power_held_ms > 300 {
            state.last_power_click = 0;
        } else if state.last_power_click == 0 || now.wrapping_sub(state.last_power_click) > 500 {
            state.last_power_click = now;
        } else {
            state.last_power_click = 0;
            toggle_light(app, &mut settings);
            return;
        }
    }
    unsafe { (app.runtime.input_power_confirm)(0) };
    if settings.short_power == 5 && double_click {
        if state.last_power_click != 0 && elapsed(app, state.last_power_click) > 500 {
            state.last_power_click = 0;
            unsafe { (app.runtime.input_power_confirm)(1) };
        }
        if frame.input.released & POWER != 0
            && frame.power_held_ms > 300
            && frame.power_held_ms <= settings.power_duration
        {
            unsafe { (app.runtime.input_power_confirm)(1) };
        }
    }
    if settings.short_power == 1
        && double_click
        && state.last_power_click != 0
        && elapsed(app, state.last_power_click) > 500
    {
        state.last_power_click = 0;
        sleep(app, false);
        return;
    }
    if settings.sleep_timeout > 0 && elapsed(app, *last_activity) >= settings.sleep_timeout {
        unsafe { (app.runtime.diagnostic)(6, settings.sleep_timeout, 0) };
        sleep(app, true);
        return;
    }
    if frame.power_held == 0 {
        state.power_released_since_wake = true;
    }
    let awaiting_click = double_click && settings.short_power == 1 && frame.power_held_ms <= 300;
    if !awaiting_click
        && state.power_released_since_wake
        && now(app) >= state.allow_sleep_at
        && frame.power_held != 0
        && frame.power_held_ms > settings.power_duration
    {
        if frame.physical_down != 0 {
            return;
        }
        unsafe { (app.runtime.diagnostic)(7, frame.power_held_ms, 0) };
        sleep(app, false);
        return;
    }
    if settings.is_papermono != 0
        && matches!(settings.short_power, 0 | 1)
        && now(app) >= state.allow_sleep_at
        && frame.input.released & POWER != 0
    {
        sleep(app, false);
        return;
    }
    let home_action = app.input.borrow().action();
    if home_action == HomeAction::ToggleFrontlight {
        toggle_light(app, &mut settings);
    }
    if home_action == HomeAction::Refresh
        || settings.short_power == 3 && frame.input.released & POWER != 0
    {
        unsafe { (app.runtime.diagnostic)(8, 0, 0) };
        if !app.forced_refresh() {
            let _guard = <App as xp_ui::app::Services<super::Owner>>::render_lock(app);
            unsafe { (app.resources.refresh)(2) };
        }
    }
    if frame.usb_changed != 0 && unsafe { facts() }.reader_in_stack == 0 {
        app.request(false);
    }
    let activity_started = now(app);
    app.activity_loop();
    let activity_duration = elapsed(app, activity_started);
    let duration = elapsed(app, started);
    if duration > state.maximum_loop {
        state.maximum_loop = duration;
        if duration > 50 {
            unsafe { (app.runtime.diagnostic)(5, duration, activity_duration) };
        }
    }
    let token = unsafe { (app.runtime.render_try_lock)() };
    if token.is_null() {
        unsafe { (app.runtime.delay_ms)(10) };
        return;
    }
    let guard = RenderGuard {
        token,
        runtime: app.runtime,
    };
    let skip_delay = app.skip_delay();
    drop(guard);
    if skip_delay {
        unsafe {
            (app.runtime.power_saving)(0);
            (app.runtime.yield_task)();
        }
    } else if elapsed(app, *last_activity) >= settings.idle_power_saving
        && app.snapshot(false, false, false).cpu_hold == 0
    {
        unsafe {
            (app.runtime.power_saving)(1);
            (app.runtime.idle_wait)(50, 10);
        }
    } else {
        unsafe { (app.runtime.delay_ms)(10) };
    }
}
