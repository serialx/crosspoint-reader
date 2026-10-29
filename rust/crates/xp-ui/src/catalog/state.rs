use crate::model::{self, Effect, Input, Interest, ListState};
pub const SERVERS: u8 = 0;
pub const EDITOR: u8 = 1;
pub const BROWSER: u8 = 2;
pub const CHECK_WIFI: u8 = 0;
pub const WIFI_SELECTION: u8 = 1;
pub const SEARCH_INPUT: u8 = 2;
pub const LOADING: u8 = 3;
pub const BROWSING: u8 = 4;
pub const DOWNLOADING: u8 = 5;
pub const ERROR: u8 = 6;
pub const AUTH: u8 = 11;
pub const SEARCH: u8 = 4;
pub const RETRY: u8 = 5;
pub const PAGE: u8 = 6;
pub const CANCEL_TRANSFER: u8 = 7;
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
        i
    }
    // Returns None only when the ordinary list owns this snapshot.
    pub fn custom(&mut self, i: Input, search: bool, pages: u8) -> Option<Effect> {
        if self.kind != BROWSER {
            return None;
        }
        if self.mode == BROWSING && pages != 0 {
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
        match self.mode {
            WIFI_SELECTION | SEARCH_INPUT | DOWNLOADING => return Some(Effect::default()),
            ERROR => {
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
        if self.kind == BROWSER
            && self.mode == DOWNLOADING
            && i.touch_flags & model::TOUCH_RELEASED != 0
            && i.action == u32::from(CANCEL_TRANSFER)
        {
            self.cancel = 1;
            return effect(CANCEL_TRANSFER, 0);
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
