use super::abi;

impl abi::Runtime {
    // Keep the radio scene outside the already tight app-loop frame.
    #[inline(never)]
    pub(super) unsafe fn ble_tick(&self, view: &abi::BleScene) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.ble_tick)(view)
        }
        #[cfg(not(feature = "app-test"))]
        {
            let scene = crate::ble_runtime::Scene {
                location: view.r#where,
                visit: view.visit,
                page_shown: view.page_shown != 0,
                storage_busy: view.storage_busy != 0,
                wifi_on: (self.wifi_active)() != 0,
                ..Default::default()
            };
            u8::from(crate::ble_runtime::xp_ble_tick(&scene))
        }
    }

    pub(super) unsafe fn ble_before_screen_change(&self, reader: u8) -> u8 {
        #[cfg(feature = "app-test")]
        {
            (self.ble_before_screen_change)(reader)
        }
        #[cfg(not(feature = "app-test"))]
        {
            u8::from(crate::ble_runtime::xp_ble_before_screen(0, reader != 0))
        }
    }
}

// Only immutable boot bindings are read from the BLE start task. Font mutation
// is protected by the existing render lock; never wait for a render awaiting BLE.
#[cfg(not(feature = "app-test"))]
pub(crate) unsafe fn release() -> bool {
    let Some(app) = super::app() else {
        return false;
    };
    let token = (app.runtime.render_try_lock)();
    if token.is_null() {
        return false;
    }
    let _guard = super::RenderGuard {
        token,
        runtime: app.runtime,
    };
    crate::font_runtime::release(app.resources.font_owner.get().cast(), 1);
    true
}

// Called on the loop task. Event handling retains the application's admission,
// remote-click and transition policy rather than a second BLE implementation.
#[cfg(not(feature = "app-test"))]
pub(crate) unsafe fn deliver(action: u8) -> bool {
    // Action values follow BleKeyBinding.h; click payloads follow RemoteButtonInput.h.
    let (kind, value) = match action {
        1..=4 => (
            abi::REMOTE_TURN,
            abi::EventValue {
                remote_turn: abi::RemoteTurn {
                    forward: u8::from(action == 1 || action == 3),
                    chapter: u8::from(action >= 3),
                },
            },
        ),
        5 | 6 => (
            abi::REMOTE_SHORTCUT,
            abi::EventValue {
                reader_menu: u8::from(action == 5),
            },
        ),
        7 | 8 => (
            abi::REMOTE_CLICK,
            abi::EventValue {
                remote_button: if action == 7 { 1 } else { 2 },
            },
        ),
        _ => return false,
    };
    super::xp_app_event(&abi::Event { kind, value }) == abi::ACCEPTED
}

// The native callback has already published the CPU hold flag. The HAL context
// owns its mutex; do not borrow loop input or navigation from the start task.
#[cfg(not(feature = "app-test"))]
pub(crate) unsafe fn hold(held: bool) {
    if held {
        if let Some(app) = super::app() {
            app.runtime.power_saving(0);
        }
    }
}

#[cfg(not(feature = "app-test"))]
#[inline(never)]
pub(crate) unsafe fn restart() {
    let Some(app) = super::app() else {
        return;
    };
    if (app.runtime.ble_restart)(abi::BleRestartPhase::Allowed as u8, 0) == 0 {
        return;
    }
    let light = app.runtime.frontlight_state();
    (app.runtime.ble_restart)(abi::BleRestartPhase::Arm as u8, light);
    (app.resources.popup_draw)(
        app.resources
            .translate(xp_core::i18n::ids::STR_LOADING_POPUP),
    );
    (app.runtime.delay_ms)(50);
    (app.runtime.ble_restart)(abi::BleRestartPhase::Execute as u8, 0);
}
