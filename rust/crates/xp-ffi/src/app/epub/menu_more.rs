use super::{
    app_abi,
    settings::{label_at, LabelSource, Labels},
    state::{Notice, Overlay, PopupIntent, TimedNotice},
    EpubSession,
};
use core::{ffi::c_void, mem::MaybeUninit};
use xp_core::i18n::ids;
use xp_ui::{
    reader::{menu_parent::Action, render::Command},
    scene::Refresh,
};

const ORIENTATIONS: [u16; 4] = [
    ids::STR_PORTRAIT,
    ids::STR_LANDSCAPE_CW,
    ids::STR_ORIENTATION_INVERTED,
    ids::STR_LANDSCAPE_CCW,
];

unsafe extern "C" fn menu_choice(context: *mut c_void, index: i32) {
    (&*context.cast::<EpubSession>()).choose_menu_option(index);
}

impl EpubSession {
    fn show_menu_choices(
        &self,
        intent: PopupIntent,
        title: u16,
        selected: i32,
        labels: Labels<'_>,
    ) {
        let count = match labels {
            Labels::Sizes(rows) => rows.len(),
            Labels::Translated(rows) => rows.len(),
            Labels::Borrowed(rows) => rows.len(),
        };
        {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            let Some(popup) = (unsafe { (*self.guarded(&access)).overlay_popup }) else {
                return;
            };
            let mut source = LabelSource {
                owner: self,
                labels,
            };
            self.model(|state| state.popup = intent);
            unsafe {
                (self.leaves.popup_show)(
                    popup.as_ptr(),
                    (self.resources.translate)(title),
                    app_abi::Text::from_bytes(&b"\0"[..0]),
                    count as u32,
                    selected,
                    label_at,
                    (&mut source as *mut LabelSource<'_>).cast(),
                    menu_choice,
                    (self as *const Self).cast_mut().cast(),
                );
                (*self.guarded(&access)).font_sizes = Default::default();
            }
        }
        self.paint_overlay_popup();
    }

    pub(super) fn choose_menu_option(&self, index: i32) {
        match self.model(|state| state.popup) {
            PopupIntent::Orientation => {
                if index == i32::from(self.settings(false).orientation) {
                    return;
                }
                self.apply_orientation(index as u8);
                {
                    let access = self.lock_resources();
                    self.discard_overlay_page(&access);
                }
                self.request_update();
            }
            PopupIntent::AutoTurn => {
                self.model(|state| state.auto_option = index as u8);
                self.toggle_auto_page_turn(index as u8);
            }
            _ => {}
        }
    }

    fn toggle_menu_bluetooth(&self) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.menu_hooks {
            unsafe { (hooks.bluetooth)(hooks.context) };
            return;
        }
        let enabled = self.settings(false).ble_enabled == 0;
        unsafe {
            (self.leaves.settings_ble_enabled)(u8::from(enabled));
            if !enabled {
                (self.leaves.radio_switch_off)();
            }
            (self.leaves.settings_save)();
            if enabled {
                (self.leaves.radio_request_connect)();
            }
        }
    }

    fn toggle_menu_frontlight(&self) {
        let on = unsafe { (self.runtime.frontlight_state)() == 0 };
        unsafe { (self.runtime.frontlight_set)(u8::from(on)) };
        let mut settings = MaybeUninit::<app_abi::Settings>::uninit();
        unsafe { (self.runtime.loop_settings)(settings.as_mut_ptr()) };
        let mut settings = unsafe { settings.assume_init() };
        settings.frontlight_on = u8::from(on);
        unsafe { (self.runtime.settings_save)(&settings) };
        let access = self.lock_resources();
        self.render_overlay(&access);
        self.render_operation(&access, Command::Display(Refresh::Fast));
    }

    pub(super) fn activate_more_row(&self, row: i32) {
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.overlay_input_hooks {
            unsafe {
                (hooks.more)(hooks.context, row);
            }
            return;
        }
        let action = unsafe {
            let guarded = &*self.guarded.get();
            if row < 0 || row >= i32::from(guarded.more_count) {
                return;
            }
            guarded.more_actions[row as usize]
        };
        {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            self.settle_overlay_refresh(&access);
        }
        match action {
            x if x == Action::Orientation as u8 => {
                let selected = usize::from(self.settings(false).orientation) % ORIENTATIONS.len();
                self.show_menu_choices(
                    PopupIntent::Orientation,
                    ids::STR_ORIENTATION,
                    selected as i32,
                    Labels::Translated(&ORIENTATIONS),
                );
                return;
            }
            x if x == Action::Auto as u8 => {
                let labels = [
                    unsafe { (self.resources.translate)(ids::STR_STATE_OFF) },
                    app_abi::Text::from_bytes(b"1"),
                    app_abi::Text::from_bytes(b"3"),
                    app_abi::Text::from_bytes(b"6"),
                    app_abi::Text::from_bytes(b"12"),
                ];
                self.show_menu_choices(
                    PopupIntent::AutoTurn,
                    ids::STR_AUTO_TURN_PAGES_PER_MIN,
                    self.model(|state| i32::from(state.auto_option)),
                    Labels::Borrowed(&labels),
                );
                return;
            }
            x if x == Action::Bluetooth as u8 => {
                self.toggle_menu_bluetooth();
                let access = self.lock_resources();
                self.render_overlay(&access);
                self.push_overlay_refresh(&access);
                return;
            }
            x if x == Action::Night as u8 => {
                unsafe {
                    (self.leaves.settings_inversion)(u8::from(
                        self.settings(false).screen_inverted == 0,
                    ));
                    (self.leaves.settings_save)();
                }
                {
                    let access = self.lock_resources();
                    self.discard_overlay_page(&access);
                }
                self.request_update();
                return;
            }
            x if x == Action::Frontlight as u8 => {
                self.toggle_menu_frontlight();
                return;
            }
            _ => {}
        }
        {
            let access = self.lock_resources();
            if self.model(|state| state.scene_gate.failed) {
                drop(access);
                self.request_update();
                return;
            }
            self.model(|state| state.overlay = Overlay::Page);
            if action == Action::Percent as u8 && self.model(|state| state.page_stored) {
                self.settle_overlay_refresh(&access);
                unsafe { (self.leaves.render_restore_bw)(0) };
                self.model(|state| state.page_stored = false);
            } else {
                self.discard_overlay_page(&access);
            }
        }
        if action == Action::ToggleBookmark as u8 {
            self.add_bookmark();
            let since = unsafe { (self.runtime.now_ms)() };
            self.model(|state| {
                state.bookmark_notice = Some(TimedNotice {
                    message: if state.bookmark_removed {
                        Notice::BookmarkRemoved
                    } else {
                        Notice::BookmarkAdded
                    },
                    since,
                })
            });
            self.request_update();
            return;
        }
        self.confirm_reader_menu(i32::from(action), super::menu::MenuOrigin::Direct);
        if action != Action::Home as u8 && action != Action::DeleteCache as u8 {
            self.request_update();
        }
    }
}
