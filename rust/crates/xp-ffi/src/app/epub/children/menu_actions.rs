use super::{
    child_result::ReaderChildResult,
    owned::{Menu, MenuOption},
};
use crate::app::{abi, epub::EpubSession};
use core::{ffi::c_void, mem::MaybeUninit, ptr::NonNull};
use xp_core::i18n::ids;
use xp_ui::{
    model::{EffectKind, Input},
    reader_chrome::{list::Screen, menu},
};

unsafe extern "C" fn label(
    context: *mut c_void,
    index: u32,
    sink: abi::TextSink,
    sink_context: *mut c_void,
) -> u8 {
    let owner = unsafe { &*context.cast::<Menu>() };
    let popup = unsafe { &*owner.popup.get() };
    let parent = unsafe { &*popup.parent.cast::<EpubSession>() };
    let text = match popup.option {
        MenuOption::Orientation => {
            let Some(key) = [
                ids::STR_PORTRAIT,
                ids::STR_LANDSCAPE_CW,
                ids::STR_INVERTED,
                ids::STR_LANDSCAPE_CCW,
            ]
            .get(index as usize) else {
                return 0;
            };
            unsafe { parent.resources.translate(*key) }
        }
        MenuOption::AutoTurn => match index {
            0 => unsafe { parent.resources.translate(ids::STR_STATE_OFF) },
            1 => abi::Text::from_terminated(b"1\0"),
            2 => abi::Text::from_terminated(b"3\0"),
            3 => abi::Text::from_terminated(b"6\0"),
            4 => abi::Text::from_terminated(b"12\0"),
            _ => return 0,
        },
    };
    unsafe { sink(sink_context, text) }
}

unsafe extern "C" fn choose(context: *mut c_void, index: i32) {
    let owner = unsafe { &*context.cast::<Menu>() };
    let (parent, option) = unsafe {
        let popup = &*owner.popup.get();
        (&*popup.parent.cast::<EpubSession>(), popup.option)
    };
    let effect = owner
        .model
        .with(|model| model.menu.choose(option as u8, index));
    assert!(!unsafe { owner.apply_effect(parent, effect) });
}

impl Menu {
    unsafe fn show_options(&self, parent: &EpubSession, option: MenuOption, selected: u8) {
        let _access = parent.lock_resources();
        let popup = unsafe { &mut *self.popup.get() };
        if popup.handle.is_none() {
            popup.handle = NonNull::new(unsafe { (parent.leaves.popup_open)() });
        }
        let Some(handle) = popup.handle else {
            parent.request_update();
            return;
        };
        popup.parent = (parent as *const EpubSession).cast();
        popup.option = option;
        let (title, count) = match option {
            MenuOption::Orientation => (ids::STR_ORIENTATION, 4),
            MenuOption::AutoTurn => (ids::STR_AUTO_TURN_PAGES_PER_MIN, 5),
        };
        unsafe {
            (parent.leaves.popup_show)(
                handle.as_ptr(),
                parent.resources.translate(title),
                abi::Text::from_terminated(b"\0"),
                count,
                i32::from(selected),
                label,
                (self as *const Self).cast_mut().cast(),
                choose,
                (self as *const Self).cast_mut().cast(),
            );
        }
        parent.request_update();
    }

    // Returns true only after an owned result is ready for Application::pop.
    pub(super) unsafe fn apply_effect(&self, parent: &EpubSession, effect: menu::Result) -> bool {
        match effect.effect {
            value if value == menu::Effect::None as u8 => {}
            value if value == menu::Effect::ShowOrientation as u8 => unsafe {
                self.show_options(parent, MenuOption::Orientation, effect.orientation);
            },
            value if value == menu::Effect::ShowAutoTurn as u8 => unsafe {
                self.show_options(parent, MenuOption::AutoTurn, effect.page_turn);
            },
            value if value == menu::Effect::ToggleNight as u8 => {
                let enabled = parent.settings(false).screen_inverted == 0;
                unsafe {
                    (parent.leaves.settings_inversion)(u8::from(enabled));
                    (parent.leaves.settings_save)();
                }
                parent.request_update();
            }
            value if value == menu::Effect::ToggleBluetooth as u8 => {
                let enabled = parent.settings(false).ble_enabled == 0;
                unsafe {
                    (parent.leaves.settings_ble_enabled)(u8::from(enabled));
                    if !enabled {
                        parent.leaves.radio_switch_off();
                    }
                    (parent.leaves.settings_save)();
                    if enabled {
                        parent.leaves.radio_request_connect();
                    }
                }
                parent.request_update();
            }
            value if value == menu::Effect::ToggleFrontlight as u8 => {
                let enabled = unsafe { parent.runtime.frontlight_state() == 0 };
                unsafe {
                    parent.runtime.frontlight_set(u8::from(enabled));
                    let mut settings = MaybeUninit::uninit();
                    (parent.runtime.loop_settings)(settings.as_mut_ptr());
                    let mut settings = settings.assume_init();
                    settings.frontlight_on = u8::from(enabled);
                    (parent.runtime.settings_save)(&settings);
                }
                parent.request_update();
            }
            value if value == menu::Effect::Finish as u8 => {
                let action = if effect.cancelled != 0 {
                    None
                } else {
                    self.items
                        .actions()
                        .iter()
                        .copied()
                        .find(|action| *action as i32 == effect.action)
                };
                unsafe {
                    *self.result.get() = Some(ReaderChildResult::Menu {
                        action,
                        orientation: effect.orientation,
                        page_turn: effect.page_turn,
                    });
                }
                return true;
            }
            value if value == menu::Effect::ApplyOrientation as u8 => {
                let _access = parent.lock_resources();
                unsafe {
                    (parent.leaves.render_orientation)(effect.orientation);
                    (parent.runtime.notify_render)();
                }
            }
            value if value == menu::Effect::Redraw as u8 => parent.request_update(),
            _ => unreachable!("menu policy returned an unknown effect"),
        }
        false
    }

    pub(super) unsafe fn input(
        &self,
        parent: &EpubSession,
        input: Input,
        home_gesture: bool,
    ) -> bool {
        if home_gesture {
            let effect = self.model.with(|model| model.menu.cancel());
            return unsafe { self.apply_effect(parent, effect) };
        }
        if let Some(handle) = unsafe { (*self.popup.get()).handle } {
            let mut result = MaybeUninit::uninit();
            unsafe {
                (parent.leaves.popup_input)(handle.as_ptr(), result.as_mut_ptr());
            }
            let result = unsafe { result.assume_init() };
            if result.changed != 0 {
                parent.request_update();
            }
            if result.consumed != 0 {
                return false;
            }
        }
        let count = self.items.actions().len() as u16;
        let update = self
            .model
            .with(|model| model.list.input(input, count, true, Screen::Menu));
        if update.retry || update.effect.invalidate != 0 {
            parent.request_update();
        }
        if update.haptic {
            unsafe {
                (parent.runtime.touch_feedback)(0);
            }
        }
        let effect = self.model.with(|model| match update.effect.kind {
            value if value == EffectKind::Cancel as u8 => model.menu.cancel(),
            value if value == EffectKind::Activate as u8 => {
                let Some(action) = usize::try_from(update.effect.value)
                    .ok()
                    .and_then(|index| self.items.actions().get(index))
                else {
                    return model.menu.invalid();
                };
                model.list.state.list.select(update.effect.value);
                model.menu.activate(*action as i32)
            }
            _ => model.menu.invalid(),
        });
        unsafe { self.apply_effect(parent, effect) }
    }
}
