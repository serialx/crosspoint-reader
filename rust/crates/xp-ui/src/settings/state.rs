use super::protocol::*;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct QuickResume {
    pub preserve: u8,
    pub automatic: u8,
}
impl QuickResume {
    pub fn sync(
        &mut self,
        timeout: u8,
        quick_screen: bool,
        screen_changed: bool,
        timeout_changed: bool,
    ) -> u8 {
        if timeout_changed {
            self.preserve = u8::from(timeout == 1);
            self.automatic = 0;
        }
        if quick_screen {
            if timeout != 1 {
                self.automatic = u8::from(self.preserve == 0);
            } else if screen_changed && self.preserve == 0 {
                self.automatic = 1;
            }
            return 1;
        }
        if screen_changed && self.automatic != 0 && self.preserve == 0 {
            self.automatic = 0;
            return 0;
        }
        timeout
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct LayoutSelection {
    pub mask: u16,
    pub edited: u8,
    pub reserved: u8,
}
impl LayoutSelection {
    pub fn locked(&self, index: u8) -> bool {
        let Some(bit) = 1u16.checked_shl(u32::from(index)) else {
            return false;
        };
        self.mask & bit != 0 && self.mask & !bit & 15 == 0
    }
    pub fn toggle(&mut self, index: u8) {
        if index >= 10 || self.locked(index) {
            return;
        }
        self.mask ^= 1u16 << index;
        self.edited = 1;
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use self::std::vec::Vec;
    use super::*;
    #[test]
    fn frozen_policy() {
        let source = include_str!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../../test/ui_settings/input.golden"
        ));
        for line in source.lines() {
            let fields: Vec<_> = line.split_whitespace().collect();
            let number = |i: usize| fields[i].parse::<u8>().unwrap();
            match fields[0] {
                "layout" => {
                    let mask = fields[1].parse::<u16>().unwrap();
                    let state = LayoutSelection {
                        mask,
                        ..LayoutSelection::default()
                    };
                    let locked =
                        (0..10).fold(0u16, |bits, i| bits | (u16::from(state.locked(i)) << i));
                    assert_eq!(locked, fields[2].parse::<u16>().unwrap(), "{line}");
                }
                "resume" => {
                    let mut state = QuickResume {
                        preserve: number(1),
                        automatic: number(2),
                    };
                    let flags = number(5);
                    assert_eq!(
                        state.sync(number(4), number(3) != 0, flags & 1 != 0, flags & 2 != 0),
                        number(6),
                        "{line}"
                    );
                    assert_eq!(
                        (state.preserve, state.automatic),
                        (number(7), number(8)),
                        "{line}"
                    );
                }
                "remap" => {
                    let step = number(1);
                    let button = number(2);
                    let mut state = Remap {
                        step,
                        ..Remap::default()
                    };
                    for i in 0..step {
                        state.mapping[usize::from(i)] = i;
                    }
                    state.update(
                        crate::model::Input {
                            now_ms: 100,
                            ..crate::model::Input::default()
                        },
                        i32::from(button),
                    );
                    assert_eq!(u8::from(state.error == 0), number(3), "{line}");
                    assert_eq!(
                        state.error_until,
                        fields[4].parse::<u32>().unwrap(),
                        "{line}"
                    );
                }
                _ => {}
            }
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Remap {
    pub error_until: u32,
    pub mapping: [u8; 4],
    pub step: u8,
    pub error: u8,
    pub reserved: [u8; 2],
}
impl Default for Remap {
    fn default() -> Self {
        Self {
            error_until: 0,
            mapping: [255; 4],
            step: 0,
            error: 0,
            reserved: [0; 2],
        }
    }
}
impl Remap {
    pub fn update(&mut self, input: crate::model::Input, front: i32) -> crate::model::Effect {
        use crate::model::{Effect, DOWN, UP};
        if self.error_until > 0 && input.now_ms > self.error_until {
            self.error = 0;
            self.error_until = 0;
            return Effect {
                invalidate: 1,
                ..Effect::default()
            };
        }
        if input.pressed & UP != 0 {
            return Effect {
                kind: 4,
                ..Effect::default()
            };
        }
        if input.pressed & DOWN != 0 {
            return Effect {
                kind: 1,
                ..Effect::default()
            };
        }
        if !(0..4).contains(&front) || self.step >= 4 {
            return Effect::default();
        }
        if self
            .mapping
            .iter()
            .enumerate()
            .any(|(i, &v)| v == front as u8 && i != usize::from(self.step))
        {
            self.error = 1;
            self.error_until = input.now_ms.wrapping_add(1500);
        } else {
            self.mapping[usize::from(self.step)] = front as u8;
            self.step += 1;
            if self.step == 4 {
                return Effect {
                    kind: 5,
                    ..Effect::default()
                };
            }
        }
        Effect {
            invalidate: 1,
            ..Effect::default()
        }
    }
}

pub use super::protocol::{OPEN, POPUP, SAVE_VALUE};
pub fn menu_edit(kind: u8, index: i32, value: u8) -> crate::model::Effect {
    use crate::model::Effect;
    let Ok(item) = u8::try_from(index) else {
        return Effect::default();
    };
    let (operation, next) = match (kind, item) {
        (CLOCK, CLOCK_TIMEZONE | CLOCK_SYNC) => (OPEN, index as u8),
        (CLOCK, CLOCK_DST) => (SAVE_VALUE, ((u16::from(value) + 1) % 3) as u8),
        (CLOCK, CLOCK_FORMAT | CLOCK_SHOW_ON_HOME) => (SAVE_VALUE, value.wrapping_add(1) % 2),
        (
            STATUS_BAR,
            STATUS_CHAPTER_PAGE_COUNT | STATUS_BOOK_PROGRESS_PERCENTAGE | STATUS_BATTERY,
        ) => (SAVE_VALUE, value.wrapping_add(1) % 2),
        (STATUS_BAR, STATUS_CLOCK) => (SAVE_VALUE, ((u16::from(value) + 1) % 3) as u8),
        (
            STATUS_BAR,
            STATUS_PROGRESS_BAR
            | STATUS_PROGRESS_BAR_THICKNESS
            | STATUS_TITLE
            | STATUS_XTC_STATUS_BAR,
        ) => (POPUP, value),
        _ => (0, 0),
    };
    Effect {
        value: i32::from(next),
        kind: operation,
        ..Effect::default()
    }
}
pub fn status_value(index: u8, value: u8) -> u8 {
    if value < 3 {
        return value;
    }
    match index {
        STATUS_PROGRESS_BAR | STATUS_TITLE => 2,
        STATUS_PROGRESS_BAR_THICKNESS => 1,
        STATUS_XTC_STATUS_BAR | STATUS_CLOCK => 0,
        _ => value,
    }
}

// Categories use the settings schema's system/display/reader/controls identities.
pub fn section(category: u8, field: u8) -> u8 {
    match field {
        1 => SECTION_LIBRARY,
        2 => SECTION_DISPLAY,
        _ => match category {
            0 => SECTION_GENERAL,
            1 => SECTION_DISPLAY,
            2 => SECTION_READER,
            3 => SECTION_CONTROLS,
            _ => SECTION_COUNT,
        },
    }
}
pub fn visible(category: u8, field: u8, text_setting: u8, caps: u8, footnotes: u8) -> bool {
    section(category, field) != SECTION_COUNT
        && field != 5
        && !(field == 3 && caps & 1 != 0)
        && !(category == 2 && text_setting != 0)
        && !(field == 4 && footnotes == 0)
}
// 255 marks the shared settings rows; zero terminates each section's actions.
pub fn extra(section: u8, caps: u8, index: u8) -> u8 {
    let general = [
        ACTION_LANGUAGE,
        ACTION_KEYBOARD_LAYOUTS,
        SHARED_ROWS,
        if caps & 2 != 0 {
            ACTION_CLOCK_SETTINGS
        } else {
            ACTION_NONE
        },
        if caps & 4 == 0 {
            ACTION_CHECK_FOR_UPDATES
        } else {
            ACTION_NONE
        },
        if caps & 4 == 0 {
            ACTION_SD_FIRMWARE_UPDATE
        } else {
            ACTION_NONE
        },
        ACTION_ABOUT,
    ];
    let display = [
        SHARED_ROWS,
        ACTION_NONE,
        ACTION_NONE,
        ACTION_NONE,
        ACTION_NONE,
        ACTION_NONE,
        ACTION_NONE,
    ];
    let reader = [
        ACTION_TEXT_SETTINGS,
        ACTION_DOWNLOAD_FONTS,
        SHARED_ROWS,
        ACTION_CUSTOMISE_STATUS_BAR,
        ACTION_CLEAR_CACHE,
        ACTION_NONE,
        ACTION_NONE,
    ];
    let controls = [
        if caps & 8 != 0 {
            ACTION_HOME_BUTTON
        } else {
            ACTION_NONE
        },
        if caps & 16 == 0 {
            ACTION_REMAP_FRONT_BUTTONS
        } else {
            ACTION_NONE
        },
        SHARED_ROWS,
        ACTION_NONE,
        ACTION_NONE,
        ACTION_NONE,
        ACTION_NONE,
    ];
    let library = display;
    let network = [
        SHARED_ROWS,
        ACTION_NETWORK,
        if caps & 32 != 0 {
            ACTION_BLUETOOTH
        } else {
            ACTION_NONE
        },
        ACTION_KOREADER_SYNC,
        ACTION_OPDS_BROWSER,
        ACTION_PLUGINS,
        ACTION_NONE,
    ];
    let actions = match section {
        SECTION_GENERAL => general,
        SECTION_DISPLAY => display,
        SECTION_READER => reader,
        SECTION_CONTROLS => controls,
        SECTION_LIBRARY => library,
        SECTION_NETWORK => network,
        _ => return 0,
    };
    actions
        .iter()
        .copied()
        .filter(|a| *a != 0)
        .nth(usize::from(index))
        .unwrap_or(0)
}

pub fn setting_edit(
    kind: u8,
    value: u8,
    count: u8,
    min: u8,
    max: u8,
    step: u8,
) -> crate::model::Effect {
    use crate::model::Effect;
    let (operation, next) = match kind {
        0 => (SAVE_VALUE, u8::from(value == 0)),
        1 if count > 2 => (POPUP, value),
        1 if count > 0 => (
            SAVE_VALUE,
            ((u16::from(value) + 1) % u16::from(count)) as u8,
        ),
        2 => (OPEN, value),
        3 => {
            let next = i16::from(value as i8) + i16::from(step);
            (
                SAVE_VALUE,
                if next > i16::from(max) {
                    min
                } else {
                    next as u8
                },
            )
        }
        _ => (0, value),
    };
    Effect {
        kind: operation,
        value: i32::from(next),
        ..Effect::default()
    }
}
