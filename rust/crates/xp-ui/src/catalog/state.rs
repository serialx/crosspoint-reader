use crate::model::{self, Effect, Input, Interest, ListState};
pub const SERVERS: u8 = 0;
pub const EDITOR: u8 = 1;
pub const BROWSER: u8 = 2;
pub const PLUGINS: u8 = 3;
pub const HUB: u8 = 4;
pub const CHECK_WIFI: u8 = 0;
pub const WIFI_SELECTION: u8 = 1;
pub const SEARCH_INPUT: u8 = 2;
pub const LOADING: u8 = 3;
pub const BROWSING: u8 = 4;
pub const DOWNLOADING: u8 = 5;
pub const ERROR: u8 = 6;
pub const PLUGIN_PICKER: u8 = 7;
pub const LIST_PICKER: u8 = 8;
pub const DONE: u8 = 9;
pub const NO_TOKEN: u8 = 10;
pub const AUTH: u8 = 11;
pub const SEARCH: u8 = 4;
pub const RETRY: u8 = 5;
pub const PAGE: u8 = 6;
pub const CANCEL_TRANSFER: u8 = 7;
pub const COMPLETE: u8 = 8;
pub const REDRAW: u8 = 9;
pub const POLL_AUTH: u8 = 10;
pub const UNINSTALL: u8 = 11;
pub const HUB_OPTION: u8 = 12;
pub const POPUP_CHROME: u32 = 13;
pub const XML: u8 = 1;
pub const MORE: u8 = 2;
pub const DEVICE_CODE: u8 = 4;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub list: ListState,
    pub progress: u32,
    pub total: u32,
    pub last_progress_ms: u32,
    pub last_percent: i32,
    pub server_index: i32,
    pub kind: u8,
    pub mode: u8,
    pub search_pending: u8,
    pub cancel: u8,
    pub picker: u8,
    pub new_server: u8,
    pub save_error: u8,
    pub reserved: u8,
    pub page: i32,
    pub current_list: i32,
    pub auth_next_poll_ms: u32,
    pub plugin_flags: u8,
    pub popup: u8,
    pub ignore_back: u8,
    pub long_press: u8,
}
fn effect(kind: u8, value: i32) -> Effect {
    Effect {
        kind,
        value,
        ..Effect::default()
    }
}
impl State {
    pub fn new(kind: u8, server_index: i32, picker: u8) -> Self {
        Self {
            list: ListState::new(0),
            kind,
            mode: BROWSING,
            server_index,
            new_server: u8::from(server_index < 0),
            picker,
            last_percent: -1,
            page: 1,
            current_list: -1,
            ..Self::default()
        }
    }
    pub fn interest(&self, now: u32) -> Interest {
        let mut i = self.list.interest(now);
        if self.kind == BROWSER {
            i.pressed |= model::PAGE_BACK | model::PAGE_FORWARD;
            i.released |= model::PAGE_BACK | model::PAGE_FORWARD;
            i.held |= model::PAGE_BACK | model::PAGE_FORWARD;
        }
        if self.kind == PLUGINS {
            if self.mode == PLUGIN_PICKER {
                i.pressed |= model::CONFIRM;
                i.held |= model::CONFIRM;
                i.touch_flags |= model::TOUCH_HELD;
                i.deadline_enabled = 0;
            }
            if self.mode == AUTH {
                i.held = 0;
                i.deadline_enabled = 1;
                i.deadline_ms = self.auth_next_poll_ms;
            }
        }
        i
    }
    // Returns None only when the ordinary list owns this snapshot.
    pub fn custom(&mut self, i: Input, search: bool, pages: u8) -> Option<Effect> {
        if self.kind < BROWSER {
            return None;
        }
        if self.kind == BROWSER && self.mode == BROWSING && pages != 0 {
            if i.released & model::PAGE_FORWARD != 0 {
                return Some(if pages & 4 != 0 {
                    effect(PAGE, 2)
                } else {
                    Effect::default()
                });
            }
            if i.released & model::PAGE_BACK != 0 {
                return Some(if pages & 2 != 0 {
                    effect(PAGE, 1)
                } else {
                    Effect::default()
                });
            }
            if i.held & (model::PAGE_BACK | model::PAGE_FORWARD) != 0 {
                return Some(Effect::default());
            }
        }
        if self.kind == PLUGINS && self.mode == AUTH {
            return Some(if i.released & model::BACK != 0 {
                self.mode = NO_TOKEN;
                Effect {
                    kind: REDRAW,
                    invalidate: 1,
                    ..Effect::default()
                }
            } else if i.now_ms.wrapping_sub(self.auth_next_poll_ms) < 0x8000_0000 {
                effect(POLL_AUTH, 0)
            } else {
                Effect::default()
            });
        }
        if self.kind >= PLUGINS && self.mode == DONE {
            return Some(
                if i.released & (model::BACK | model::CONFIRM) != 0 || tapped(i) {
                    effect(COMPLETE, 0)
                } else {
                    Effect::default()
                },
            );
        }
        match self.mode {
            WIFI_SELECTION | SEARCH_INPUT | DOWNLOADING => return Some(Effect::default()),
            ERROR | NO_TOKEN => {
                return Some(if i.released & model::BACK != 0 {
                    effect(1, 0)
                } else if i.released & model::CONFIRM != 0
                    || i.touch_flags & model::TOUCH_RELEASED != 0
                        && i.touch_x >= 0
                        && i.touch_y >= 0
                {
                    effect(RETRY, 0)
                } else {
                    Effect::default()
                })
            }
            CHECK_WIFI | LOADING => {
                return Some(if i.released & model::BACK != 0 {
                    effect(1, 0)
                } else {
                    Effect::default()
                })
            }
            _ => {}
        }
        if i.pressed & model::NAV_PREVIOUS != 0 {
            self.search_pending =
                u8::from(self.mode == BROWSING && search && self.list.selected == 0);
            if self.search_pending != 0 {
                return Some(Effect::default());
            }
            return None;
        }
        if self.search_pending != 0 && i.released & model::NAV_PREVIOUS != 0 {
            self.search_pending = 0;
            return Some(effect(SEARCH, 0));
        }
        None
    }
    pub fn update(&mut self, i: Input, count: u16, search: bool, pages: u8) -> Effect {
        if self.kind >= BROWSER
            && self.mode == DOWNLOADING
            && i.touch_flags & model::TOUCH_RELEASED != 0
            && i.action == u32::from(CANCEL_TRANSFER)
        {
            self.cancel = 1;
            return effect(CANCEL_TRANSFER, 0);
        }
        if self.kind == PLUGINS {
            if self.ignore_back != 0 {
                if i.released & model::BACK != 0 {
                    self.ignore_back = 0;
                    return Effect::default();
                }
                if i.held & model::BACK == 0 {
                    self.ignore_back = 0;
                }
            }
            if self.popup != 0 {
                return self.popup_input(i);
            }
            if self.mode == PLUGIN_PICKER {
                if i.pressed & model::CONFIRM != 0 {
                    self.long_press = 0;
                }
                if i.held & model::CONFIRM != 0 && i.held_ms >= 1000 && self.long_press == 0 {
                    self.long_press = 1;
                    return effect(UNINSTALL, self.list.selected);
                }
                if i.released & model::CONFIRM != 0 && self.long_press != 0 {
                    self.long_press = 0;
                    return Effect::default();
                }
                if i.touch_flags & model::TOUCH_RELEASED != 0 && i.action == u32::from(UNINSTALL) {
                    self.list.selected = i.value;
                    self.list.active = -1;
                    return effect(UNINSTALL, i.value);
                }
            }
        }
        if let Some(e) = self.custom(i, search, pages) {
            return e;
        }
        if i.touch_flags & model::TOUCH_RELEASED != 0 {
            if i.action == u32::from(SEARCH) && search {
                return effect(SEARCH, 0);
            }
            if i.action == u32::from(PAGE) {
                return effect(PAGE, i.value);
            }
        }
        let before = self.list.selected;
        let result = self.list.update(i, i32::from(count), true);
        if self.list.selected != before {
            self.search_pending = 0;
        }
        result
    }
}
impl State {
    pub fn select(&mut self, index: i32, reset: bool) {
        if reset {
            self.list = ListState::new(index);
        } else {
            self.list.selected = index;
            self.list.follow = 1;
            self.list.navigation_revision = self.list.navigation_revision.wrapping_add(1);
        }
    }
    pub fn set_mode(&mut self, mode: u8) {
        if mode == DOWNLOADING {
            self.progress = 0;
            self.total = 0;
            self.last_percent = -1;
            self.last_progress_ms = 0;
            self.cancel = 0;
        }
        self.mode = mode;
    }
    pub fn awake(&self) -> bool {
        matches!(
            self.mode,
            CHECK_WIFI | WIFI_SELECTION | SEARCH_INPUT | LOADING | DOWNLOADING | AUTH
        )
    }
    pub fn progress(&mut self, downloaded: u32, total: u32, now: u32) -> bool {
        self.progress = downloaded;
        self.total = total;
        let percent = if total > 0 {
            (u64::from(downloaded) * 100 / u64::from(total)) as i32
        } else {
            0
        };
        if percent >= 100
            || self.last_percent < 0
            || percent >= self.last_percent.saturating_add(5)
            || now.wrapping_sub(self.last_progress_ms) >= 5000
        {
            self.last_percent = percent;
            self.last_progress_ms = now;
            true
        } else {
            false
        }
    }
}

fn tapped(i: Input) -> bool {
    i.touch_flags & model::TOUCH_RELEASED != 0 && i.touch_x >= 0 && i.touch_y >= 0
}
impl State {
    fn popup_input(&mut self, i: Input) -> Effect {
        let mut e = Effect::default();
        if i.touch_flags != 0 {
            if i.action == u32::from(HUB_OPTION) {
                if i.touch_flags & model::TOUCH_RELEASED != 0 {
                    self.popup = 0;
                    e = effect(HUB_OPTION, i.value);
                    e.invalidate = 1;
                } else if i.touch_flags & model::TOUCH_PRESSED != 0
                    && self.popup != i.value as u8 + 1
                {
                    self.popup = i.value as u8 + 1;
                    e.invalidate = 1;
                }
            } else if i.action != POPUP_CHROME && tapped(i) {
                self.popup = 0;
                e.invalidate = 1;
            }
            return e;
        }
        if i.pressed & model::NAV_PREVIOUS != 0 {
            self.popup = (self.popup + 1) % 3 + 1;
        } else if i.pressed & model::NAV_NEXT != 0 {
            self.popup = self.popup % 3 + 1;
        } else if i.released & model::CONFIRM != 0 {
            e = effect(HUB_OPTION, i32::from(self.popup) - 1);
            self.popup = 0;
        } else if i.released & model::BACK != 0 {
            self.popup = 0;
        } else {
            return e;
        }
        e.invalidate = 1;
        e
    }
    pub fn item(&mut self, row: i32, count: i32) -> i32 {
        let previous = self.mode == BROWSING && self.plugin_flags & XML == 0 && self.page > 1;
        let next =
            self.mode == BROWSING && self.plugin_flags & XML == 0 && self.plugin_flags & MORE != 0;
        let index = row.wrapping_sub(i32::from(previous));
        if index == -1 {
            self.page = self.page.wrapping_sub(1);
        } else if next && index == count {
            self.page = self.page.wrapping_add(1);
        }
        index
    }
}
#[repr(C)]
#[derive(Default)]
pub struct Window {
    pub start: usize,
    pub limit: usize,
    pub seen: usize,
    pub more: u8,
}
impl Window {
    pub fn take(&mut self, eligible: bool) -> bool {
        if !eligible {
            return false;
        }
        let index = self.seen;
        self.seen = self.seen.wrapping_add(1);
        if index < self.start {
            return false;
        }
        if index - self.start < self.limit {
            return true;
        }
        self.more = 1;
        false
    }
}
pub fn page_template(bytes: &[u8]) -> bool {
    bytes.windows(6).any(|w| w == b"{page}")
}
