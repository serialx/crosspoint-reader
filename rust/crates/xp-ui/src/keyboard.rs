//! Keyboard edits and input policy. Byte storage belongs to the activity.
use crate::model::{self, Effect as Navigation, EffectKind, Input, Interest};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Effect {
    pub navigation: Navigation,
    pub required_capacity: u32,
    pub haptics: u8,
    pub grow: u8,
    pub reserved: [u8; 2],
}
use core::ffi::{c_char, c_void, CStr};

const SHIFT: i16 = -1;
const MODE: i16 = -2;
const URL: i16 = -3;
const LANGUAGE: i16 = -4;
const DELETE: i16 = 8;
const ENTER: i16 = 13;
const PASSWORD: u8 = 1;
const URL_INPUT: u8 = 2;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub length: u32,
    pub cursor: u32,
    pub max_length: u32,
    pub saved_cursor: u32,
    pub right_start_cursor: u32,
    pub last_repeat: u32,
    pub hint_time: u32,
    pub touch_start: u32,
    pub row: i16,
    pub col: i16,
    pub active_key: i16,
    pub enabled_layouts: u16,
    pub layout: u8,
    pub input_type: u8,
    pub shifted: u8,
    pub symbols: u8,
    pub url_panel: u8,
    pub show_language: u8,
    pub cursor_mode: u8,
    pub toggle_position: u8,
    pub password_visible: u8,
    pub hint_visible: u8,
    pub delete_count: u8,
    pub confirm_held: u8,
    pub confirm_long: u8,
    pub up_held: u8,
    pub up_long: u8,
    pub down_held: u8,
    pub down_long: u8,
    pub right_held: u8,
    pub right_long: u8,
    pub touch_held: u8,
    pub touch_long: u8,
    pub active_index: i16,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct WireKey {
    pub label: *const c_char,
    pub output: *const c_char,
    pub alternate: *const c_char,
    pub value: i16,
    pub kind: u8,
    pub units: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Row {
    pub count: u8,
    pub inset: u8,
    pub independent: u8,
    pub reserved: u8,
}
#[repr(C)]
pub struct Layouts {
    pub context: *mut c_void,
    pub row: Option<unsafe extern "C" fn(*mut c_void, u8, u8, u8, u8, u8) -> Row>,
    pub key: Option<unsafe extern "C" fn(*mut c_void, u8, u8, u8, u8, u8, u8) -> WireKey>,
}
#[derive(Clone, Copy)]
pub struct Key<'a> {
    pub label: &'a [u8],
    pub output: &'a [u8],
    pub alternate: &'a [u8],
    pub value: i16,
    pub kind: u8,
    pub units: u8,
}
fn language_layout(language: u8) -> u8 {
    // Persisted layout order; language IDs come from the generated table.
    [
        b"EN".as_slice(),
        b"FR",
        b"DE",
        b"ES",
        b"RU",
        b"UK",
        b"BE",
        b"KK",
        b"HE",
        b"AR",
    ]
    .iter()
    .position(|code| xp_core::i18n::language_from_code(code) == language)
    .unwrap_or(0) as u8
}
pub fn enabled(configured: u16, language: u8) -> u16 {
    let configured = configured & 1023;
    if configured == 0 {
        1 | (1 << language_layout(language))
    } else if configured & 15 == 0 {
        configured | 1
    } else {
        configured
    }
}
pub fn next(mask: u16, current: u8) -> u8 {
    let from = if current < 10 { current } else { 0 };
    for step in 1..=10 {
        let i = (from + step) % 10;
        if mask & (1 << i) != 0 {
            return i;
        }
    }
    current
}
pub fn start(mask: u16, language: u8) -> u8 {
    let preferred = language_layout(language);
    if mask & (1 << preferred) != 0 {
        preferred
    } else {
        next(mask, preferred)
    }
}
pub fn init(length: u32, maximum: u32, input_type: u8, mask: u16, language: u8) -> State {
    State {
        length,
        cursor: length,
        max_length: maximum,
        input_type,
        enabled_layouts: mask,
        layout: if input_type == URL_INPUT {
            0
        } else {
            start(mask, language)
        },
        show_language: u8::from(mask.count_ones() > 1),
        active_key: i16::MIN,
        active_index: -1,
        ..State::default()
    }
}
unsafe fn bytes<'a>(p: *const c_char) -> &'a [u8] {
    if p.is_null() {
        &[]
    } else {
        unsafe { CStr::from_ptr(p).to_bytes() }
    }
}
impl Layouts {
    pub fn row(&self, state: &State, row: u8) -> Row {
        if row >= 5 || state.layout >= 10 {
            return Row::default();
        }
        if state.input_type == URL_INPUT && state.symbols == 0 {
            return url_row(state, row);
        }
        let value = self.row.map_or(Row::default(), |f| unsafe {
            f(
                self.context,
                state.layout,
                state.shifted,
                state.symbols,
                state.show_language,
                row,
            )
        });
        if value.count > 16 || value.inset > 8 {
            Row::default()
        } else {
            value
        }
    }
    pub fn key<'a>(&'a self, state: &State, row: u8, col: u8) -> Option<Key<'a>> {
        if col >= self.row(state, row).count {
            return None;
        }
        if state.input_type == URL_INPUT && state.symbols == 0 {
            return url_key(state, row, col);
        }
        let f = self.key?;
        let k = unsafe {
            f(
                self.context,
                state.layout,
                state.shifted,
                state.symbols,
                state.show_language,
                row,
                col,
            )
        };
        if k.kind > 7 || k.units == 0 {
            return None;
        }
        Some(Key {
            label: unsafe { bytes(k.label) },
            output: if k.kind == 3 && k.output.is_null() {
                b" "
            } else {
                unsafe { bytes(k.output) }
            },
            alternate: unsafe { bytes(k.alternate) },
            value: k.value,
            kind: k.kind,
            units: k.units,
        })
    }
    pub fn rows(&self, state: &State) -> i16 {
        (0..5).take_while(|r| self.row(state, *r).count > 0).count() as i16
    }
    pub fn find(&self, state: &State, value: i16) -> Option<Key<'_>> {
        for r in 0..self.rows(state) as u8 {
            for c in 0..self.row(state, r).count {
                if let Some(k) = self.key(state, r, c) {
                    if k.value == value {
                        return Some(k);
                    }
                }
            }
        }
        None
    }
}
fn url_row(s: &State, r: u8) -> Row {
    let (count, inset) = if s.url_panel != 0 {
        match r {
            0..=2 => (3, 0),
            3 => (4, 0),
            _ => (0, 0),
        }
    } else {
        match r {
            0 | 1 => (10, 0),
            2 => (9, 1),
            3 => (9, 0),
            4 => (6, 0),
            _ => (0, 0),
        }
    };
    Row {
        count,
        inset,
        independent: 0,
        reserved: 0,
    }
}
const fn key(
    label: &'static [u8],
    output: &'static [u8],
    value: i16,
    kind: u8,
    units: u8,
    alternate: &'static [u8],
) -> Key<'static> {
    Key {
        label,
        output,
        value,
        kind,
        units,
        alternate,
    }
}
const fn control(value: i16, kind: u8, units: u8, label: &'static [u8]) -> Key<'static> {
    key(label, b"", value, kind, units, b"")
}
fn url_key(s: &State, r: u8, c: u8) -> Option<Key<'static>> {
    const SNIPPETS: [&[u8]; 9] = [
        b"https://",
        b"www.",
        b".com",
        b"http://",
        b"192.168.",
        b".org",
        b"/opds",
        b":8080",
        b".net",
    ];
    const LOWER: [&[u8]; 26] = [
        b"q", b"w", b"e", b"r", b"t", b"y", b"u", b"i", b"o", b"p", b"a", b"s", b"d", b"f", b"g",
        b"h", b"j", b"k", b"l", b"z", b"x", b"c", b"v", b"b", b"n", b"m",
    ];
    const UPPER: [&[u8]; 26] = [
        b"Q", b"W", b"E", b"R", b"T", b"Y", b"U", b"I", b"O", b"P", b"A", b"S", b"D", b"F", b"G",
        b"H", b"J", b"K", b"L", b"Z", b"X", b"C", b"V", b"B", b"N", b"M",
    ];
    if c >= url_row(s, r).count {
        return None;
    }
    if s.url_panel != 0 {
        return Some(if r < 3 {
            let i = usize::from(r) * 3 + usize::from(c);
            let text = SNIPPETS[i];
            key(text, text, 2001 + i as i16, 0, 2, b"")
        } else {
            match c {
                0 => control(MODE, 2, 4, b"abc"),
                1 => control(URL, 0, 6, b"URL"),
                2 => control(DELETE, 4, 3, b"Del"),
                _ => control(ENTER, 5, 4, b"OK"),
            }
        });
    }
    if r == 0 {
        const DIGITS: [&[u8]; 10] = [b"1", b"2", b"3", b"4", b"5", b"6", b"7", b"8", b"9", b"0"];
        const ALT: [&[u8]; 10] = [b"!", b"@", b"#", b"$", b"%", b"^", b"&", b"*", b"(", b")"];
        let text = DIGITS[usize::from(c)];
        return Some(key(
            text,
            text,
            i16::from(text[0]),
            0,
            2,
            ALT[usize::from(c)],
        ));
    }
    if r == 4 {
        return Some(match c {
            0 => control(MODE, 2, 4, b"?123"),
            1 => key(b":", b":", 58, 0, 2, b""),
            2 => key(b"/", b"/", 47, 0, 2, b""),
            3 => key(b".", b".", 46, 0, 2, b""),
            4 => control(URL, 0, 6, b"URL"),
            _ => control(ENTER, 5, 4, b"OK"),
        });
    }
    if r == 3 && c == 0 {
        return Some(control(SHIFT, 1, 3, b""));
    }
    if r == 3 && c == 8 {
        return Some(control(DELETE, 4, 3, b"Del"));
    }
    let i = match r {
        1 => usize::from(c),
        2 => 10 + usize::from(c),
        _ => 18 + usize::from(c),
    };
    let text = if s.shifted != 0 { UPPER[i] } else { LOWER[i] };
    Some(key(text, text, i16::from(text[0]), 0, 2, b""))
}
pub fn previous(text: &[u8], position: usize) -> usize {
    let mut p = position.min(text.len());
    if p == 0 {
        return 0;
    }
    p -= 1;
    while p > 0 && text[p] & 0xc0 == 0x80 {
        p -= 1;
    }
    p
}
pub fn following(text: &[u8], position: usize) -> usize {
    if position >= text.len() {
        return text.len();
    }
    let mut p = position + 1;
    while p < text.len() && text[p] & 0xc0 == 0x80 {
        p += 1;
    }
    p
}
impl State {
    pub fn clamp(&mut self, layouts: &Layouts) {
        let rows = layouts.rows(self);
        self.row = self.row.max(0).min((rows - 1).max(0));
        self.col = self
            .col
            .max(0)
            .min((i16::from(layouts.row(self, self.row as u8).count) - 1).max(0));
    }
    pub fn move_row(&mut self, layouts: &Layouts, delta: i16) {
        let rows = layouts.rows(self);
        if rows == 0 {
            return;
        }
        let old = i16::from(layouts.row(self, self.row as u8).count);
        self.row = (self.row + delta + rows) % rows;
        let new = i16::from(layouts.row(self, self.row as u8).count);
        if old > 0 && new > 0 && old != new {
            self.col = self.col * new / old;
        }
        self.clamp(layouts);
    }
    pub fn move_col(&mut self, layouts: &Layouts, delta: i16) {
        let cols = i16::from(layouts.row(self, self.row as u8).count);
        if cols > 0 {
            self.col = (self.col + delta + cols) % cols;
        }
    }
    pub fn sync(&mut self, layouts: &Layouts, value: i16) -> bool {
        for r in 0..layouts.rows(self) as u8 {
            for c in 0..layouts.row(self, r).count {
                if layouts.key(self, r, c).is_some_and(|k| k.value == value) {
                    self.row = i16::from(r);
                    self.col = i16::from(c);
                    return true;
                }
            }
        }
        false
    }
    pub fn selected<'a>(&self, layouts: &'a Layouts) -> Option<Key<'a>> {
        layouts.key(self, self.row as u8, self.col as u8)
    }
    pub fn logical_index(&self, layouts: &Layouts) -> i16 {
        (0..self.row)
            .map(|r| i16::from(layouts.row(self, r as u8).count))
            .sum::<i16>()
            + self.col
    }
    pub fn insert(&mut self, text: &mut [u8], out: &[u8]) -> u32 {
        let len = self.length as usize;
        let Some(end) = len.checked_add(out.len()) else {
            return 0;
        };
        if out.is_empty() || (self.max_length != 0 && end > self.max_length as usize) {
            return 0;
        }
        if end >= text.len() {
            return u32::try_from(end + 1).unwrap_or(u32::MAX);
        }
        self.cursor = self.cursor.min(self.length);
        let cursor = self.cursor as usize;
        text.copy_within(cursor..len, cursor + out.len());
        text[cursor..cursor + out.len()].copy_from_slice(out);
        text[end] = 0;
        self.length = end as u32;
        self.cursor += out.len() as u32;
        0
    }
    pub fn backspace(&mut self, text: &mut [u8]) -> bool {
        if self.length == 0
            || self.cursor == 0
            || self.length as usize >= text.len()
            || self.cursor > self.length
        {
            return false;
        }
        let prev = previous(&text[..self.length as usize], self.cursor as usize);
        text.copy_within(self.cursor as usize..=self.length as usize, prev);
        self.length -= self.cursor - prev as u32;
        self.cursor = prev as u32;
        true
    }
    fn clear(&mut self, text: &mut [u8]) {
        self.length = 0;
        self.cursor = 0;
        if let Some(first) = text.first_mut() {
            *first = 0;
        }
    }
    fn insert_alt(&mut self, text: &mut [u8], key: Key<'_>) -> (bool, u32) {
        if key.kind != 0 {
            return (false, 0);
        }
        if !key.alternate.is_empty() {
            return (true, self.insert(text, key.alternate));
        }
        let cp = match key.value {
            97..=122 => key.value - 32,
            65..=90 => key.value + 32,
            0x410..=0x42f => key.value + 32,
            0x430..=0x44f => key.value - 32,
            0x401 => 0x451,
            0x451 => 0x401,
            _ => return (false, 0),
        };
        let bytes = if cp < 128 {
            [cp as u8, 0]
        } else {
            [0xc0 | (cp >> 6) as u8, 0x80 | (cp & 63) as u8]
        };
        (
            true,
            self.insert(text, &bytes[..if cp < 128 { 1 } else { 2 }]),
        )
    }
    pub fn alt_selected(&mut self, text: &mut [u8], layouts: &Layouts) -> (bool, u32) {
        let Some(key) = self.selected(layouts) else {
            return (false, 0);
        };
        if key.value == DELETE {
            self.clear(text);
            (true, 0)
        } else {
            self.insert_alt(text, key)
        }
    }
    pub fn activate(
        &mut self,
        text: &mut [u8],
        layouts: &Layouts,
        value: i16,
        long: bool,
        now: u32,
    ) -> Effect {
        let mut effect = Effect::default();
        match value {
            SHIFT | MODE | URL | LANGUAGE => {
                self.delete_count = 0;
                self.hint_visible = 0;
                match value {
                    SHIFT => self.shifted ^= 1,
                    MODE => {
                        if self.url_panel != 0 {
                            self.url_panel = 0;
                        } else {
                            self.symbols ^= 1;
                            self.shifted = 0;
                        }
                    }
                    URL => {
                        self.url_panel ^= 1;
                        self.symbols = 0;
                        self.shifted = 0;
                    }
                    _ => {
                        let next = next(self.enabled_layouts, self.layout);
                        if next == self.layout {
                            return effect;
                        }
                        self.layout = next;
                        self.shifted = 0;
                    }
                }
                self.clamp(layouts);
                effect.navigation.invalidate = 1;
            }
            ENTER => effect.navigation.kind = EffectKind::Activate as u8,
            DELETE => {
                if long {
                    self.clear(text);
                } else {
                    self.delete_count = self.delete_count.saturating_add(1);
                    if self.delete_count >= 2 {
                        self.hint_visible = 1;
                        self.hint_time = now;
                    }
                    self.backspace(text);
                }
                effect.navigation.invalidate = 1;
            }
            _ => {
                self.delete_count = 0;
                self.hint_visible = 0;
                let Some(key) = layouts.find(self, value) else {
                    return effect;
                };
                let (inserted, mut required) = if long {
                    self.insert_alt(text, key)
                } else {
                    (false, 0)
                };
                if !inserted {
                    if key.output.is_empty() {
                        return effect;
                    }
                    required = self.insert(text, key.output);
                }
                if required != 0 {
                    effect.required_capacity = required;
                    effect.grow = 1;
                }
                if self.shifted != 0 && self.symbols == 0 {
                    self.shifted = 0;
                    self.clamp(layouts);
                }
                effect.navigation.invalidate = 1;
            }
        }
        effect
    }
    pub fn mask(&self, text: &[u8], out: &mut [u8]) -> bool {
        let n = self.length as usize;
        if n > text.len() || n >= out.len() {
            return false;
        }
        out[..n].copy_from_slice(&text[..n]);
        out[n] = 0;
        if self.input_type == PASSWORD && self.password_visible == 0 {
            let end = if self.cursor_mode != 0 {
                0
            } else {
                self.cursor.min(self.length) as usize
            };
            let start = previous(&text[..n], end);
            out[..start].fill(b'*');
            out[end..n].fill(b'*');
        }
        true
    }
}
pub fn interest(s: &State) -> Interest {
    let mut interest = Interest {
        pressed: model::LEFT | model::RIGHT | model::UP | model::DOWN | model::CONFIRM,
        released: model::LEFT
            | model::RIGHT
            | model::UP
            | model::DOWN
            | model::CONFIRM
            | model::BACK,
        held: model::LEFT | model::RIGHT | model::UP | model::CONFIRM,
        held_after_ms: 501,
        touch_flags: 7,
        deadline_enabled: 0,
        ..Interest::default()
    };
    if s.touch_held != 0 {
        interest.held = 0;
        interest.deadline_enabled = 1;
        interest.deadline_ms = s.touch_start;
    } else if s.hint_visible != 0
        && s.cursor_mode == 0
        && s.confirm_held == 0
        && s.up_held == 0
        && s.right_held == 0
    {
        interest.held = 0;
        interest.deadline_enabled = 1;
        interest.deadline_ms = s.hint_time;
    }
    interest
}
pub fn update(
    s: &mut State,
    text: &mut [u8],
    layouts: &Layouts,
    input: &Input,
    tapped_cursor: u32,
) -> Effect {
    let mut effect = Effect::default();
    if s.length as usize >= text.len()
        || s.cursor > s.length
        || s.input_type > 2
        || s.layout >= 10
        || layouts.row.is_none()
        || layouts.key.is_none()
    {
        return effect;
    }
    let now = input.now_ms;
    if input.touch_flags & model::TOUCH_RELEASED != 0 && tapped_cursor != u32::MAX {
        s.cursor = tapped_cursor.min(s.length);
        while s.cursor > 0 && s.cursor < s.length && text[s.cursor as usize] & 0xc0 == 0x80 {
            s.cursor -= 1;
        }
        s.cursor_mode = 0;
        s.toggle_position = 0;
        s.hint_visible = 0;
        s.touch_held = 0;
        s.touch_long = 0;
        effect.navigation.invalidate = 1;
        effect.haptics = 1;
        return effect;
    }
    if s.cursor_mode == 0 {
        let press = input.touch_flags & model::TOUCH_PRESSED != 0;
        let release = input.touch_flags & model::TOUCH_RELEASED != 0
            && input.touch_x >= 0
            && input.touch_y >= 0;
        let contact = input.touch_flags & model::TOUCH_HELD != 0;
        let hit = if input.action == 1 {
            i16::try_from(input.value).ok()
        } else {
            None
        };
        let mut event = None;
        if release {
            let swallow = s.touch_long != 0;
            s.touch_held = 0;
            s.touch_long = 0;
            if !swallow {
                event = hit
                    .or(if s.active_key != i16::MIN {
                        Some(s.active_key)
                    } else {
                        None
                    })
                    .map(|k| (k, false));
            }
            s.active_key = i16::MIN;
            s.active_index = -1;
        } else if press {
            if s.touch_long == 0 {
                let active = hit.unwrap_or(i16::MIN);
                if s.touch_held == 0 || s.active_key != active {
                    s.touch_held = 1;
                    s.touch_start = now;
                    effect.navigation.invalidate = u8::from(s.active_key != active);
                }
                s.active_key = active;
                s.active_index = -1;
                let threshold = if active == DELETE { 900 } else { 350 };
                if active != i16::MIN && now.wrapping_sub(s.touch_start) >= threshold {
                    s.touch_long = 1;
                    event = Some((active, true));
                    s.active_key = i16::MIN;
                    s.active_index = -1;
                }
            }
        } else if s.touch_held != 0 && !contact {
            s.touch_held = 0;
            s.touch_long = 0;
            s.active_key = i16::MIN;
            s.active_index = -1;
            effect.navigation.invalidate = 1;
        }
        if let Some((value, long)) = event {
            s.sync(layouts, value);
            let mut action = s.activate(text, layouts, value, long, now);
            if action.navigation.invalidate != 0 {
                action.haptics = if long { 2 } else { 1 };
            }
            return action;
        }
        if press || release {
            return effect;
        }
    }
    let pressed = |b| input.pressed & b != 0;
    let released = |b| input.released & b != 0;
    let held = |b| input.held & b != 0;
    if s.cursor_mode == 0 && pressed(model::UP) {
        s.up_held = 1;
        s.up_long = 0;
    }
    if s.up_held != 0 && s.up_long == 0 && held(model::UP) && input.held_ms > 500 {
        s.cursor_mode = 1;
        s.up_long = 1;
        s.hint_visible = 1;
        s.hint_time = now;
        effect.navigation.invalidate = 1;
    }
    if released(model::UP) {
        if s.up_held != 0 && s.up_long == 0 && s.cursor_mode == 0 {
            s.move_row(layouts, -1);
            effect.navigation.invalidate = 1;
        }
        s.up_held = 0;
        s.up_long = 0;
    }
    if pressed(model::DOWN) {
        s.down_held = 1;
        if s.cursor_mode != 0 {
            s.toggle_position = 0;
            s.password_visible = 0;
            s.cursor_mode = 0;
            s.hint_visible = 0;
            s.down_long = 1;
            effect.navigation.invalidate = 1;
        } else {
            s.down_long = 0;
        }
    }
    if released(model::DOWN) {
        if s.down_held != 0 && s.down_long == 0 && s.cursor_mode == 0 {
            s.move_row(layouts, 1);
            effect.navigation.invalidate = 1;
        }
        s.down_held = 0;
        s.down_long = 0;
    }
    if navigate(s, input, model::LEFT) && s.cursor_mode == 0 {
        s.move_col(layouts, -1);
        effect.navigation.invalidate = 1;
    }
    if released(model::LEFT) && s.cursor_mode != 0 {
        if s.toggle_position != 0 {
            s.cursor = s.saved_cursor;
            s.toggle_position = 0;
            effect.navigation.invalidate = 1;
        } else if s.cursor > 0 {
            s.cursor = previous(&text[..s.length as usize], s.cursor as usize) as u32;
            effect.navigation.invalidate = 1;
        }
    }
    if pressed(model::RIGHT)
        && s.cursor_mode != 0
        && s.input_type == PASSWORD
        && s.toggle_position == 0
    {
        s.right_held = 1;
        s.right_long = 0;
        s.right_start_cursor = s.cursor;
    }
    if navigate(s, input, model::RIGHT) && s.cursor_mode == 0 {
        s.move_col(layouts, 1);
        effect.navigation.invalidate = 1;
    }
    if s.right_held != 0
        && s.right_long == 0
        && held(model::RIGHT)
        && input.held_ms > 500
        && s.cursor_mode != 0
        && s.input_type == PASSWORD
        && s.toggle_position == 0
    {
        s.saved_cursor = s.right_start_cursor;
        s.toggle_position = 1;
        s.right_long = 1;
        effect.navigation.invalidate = 1;
    }
    if released(model::RIGHT) {
        if s.cursor_mode != 0 && s.input_type == PASSWORD {
            s.right_held = 0;
            s.right_long = 0;
        }
        if s.cursor_mode != 0 && s.toggle_position == 0 && s.cursor < s.length {
            s.cursor = following(&text[..s.length as usize], s.cursor as usize) as u32;
            effect.navigation.invalidate = 1;
        }
        if s.cursor_mode != 0 {
            return effect;
        }
        s.right_held = 0;
        s.right_long = 0;
    }
    if pressed(model::CONFIRM) {
        s.confirm_held = 1;
        s.confirm_long = 0;
    }
    let selected = s.selected(layouts);
    let selected_delete = selected.is_some_and(|k| k.value == DELETE);
    if s.confirm_held != 0
        && s.confirm_long == 0
        && held(model::CONFIRM)
        && input.held_ms > 1500
        && selected_delete
    {
        s.alt_selected(text, layouts);
        s.confirm_long = 1;
        effect.navigation.invalidate = 1;
    }
    if s.confirm_held != 0
        && s.confirm_long == 0
        && held(model::CONFIRM)
        && input.held_ms > 500
        && !selected_delete
    {
        let (inserted, required) = s.alt_selected(text, layouts);
        if inserted {
            effect.navigation.invalidate = 1;
            s.confirm_long = 1;
        }
        if required != 0 {
            effect.required_capacity = required;
            effect.grow = 1;
            s.confirm_long = 1;
        }
    }
    if released(model::CONFIRM) {
        if s.confirm_held != 0 && s.confirm_long == 0 && s.cursor_mode == 0 {
            if let Some(k) = selected {
                let action = s.activate(text, layouts, k.value, false, now);
                effect.navigation.kind = action.navigation.kind;
                effect.navigation.invalidate |= action.navigation.invalidate;
                effect.required_capacity = action.required_capacity;
                effect.grow = action.grow;
            }
        } else if s.confirm_held != 0
            && s.confirm_long == 0
            && s.cursor_mode != 0
            && s.input_type == PASSWORD
            && s.toggle_position != 0
        {
            s.password_visible ^= 1;
            effect.navigation.invalidate = 1;
        }
        s.confirm_held = 0;
        s.confirm_long = 0;
    }
    if released(model::BACK) {
        effect.navigation.kind = EffectKind::Cancel as u8;
    }
    if s.hint_visible != 0 && s.cursor_mode == 0 && now.wrapping_sub(s.hint_time) > 4000 {
        s.hint_visible = 0;
        effect.navigation.invalidate = 1;
    }
    effect
}
fn navigate(s: &mut State, input: &Input, button: u32) -> bool {
    let press = input.pressed & button != 0;
    if (input.pressed | input.released) & button != 0 {
        s.last_repeat = 0;
        return press;
    }
    if input.held & button != 0
        && input.held_ms > 500
        && input.now_ms.wrapping_sub(s.last_repeat) > 500
    {
        s.last_repeat = input.now_ms;
        return true;
    }
    false
}

#[no_mangle]
pub extern "C" fn xp_ui_keyboard_enabled(configured: u16, language: u8) -> u16 {
    enabled(configured, language)
}
#[no_mangle]
pub extern "C" fn xp_ui_keyboard_start(mask: u16, language: u8) -> u8 {
    start(mask, language)
}
#[no_mangle]
pub extern "C" fn xp_ui_keyboard_next(mask: u16, current: u8) -> u8 {
    next(mask, current)
}
/// # Safety
/// State is writable and exclusively borrowed for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_init(
    state: *mut State,
    length: u32,
    maximum: u32,
    input_type: u8,
    mask: u16,
    language: u8,
) {
    if let Some(state) = unsafe { state.as_mut() } {
        *state = init(length, maximum, input_type, mask, language);
    }
}
/// # Safety
/// State is readable for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_interest(state: *const State) -> Interest {
    unsafe { state.as_ref() }.map_or(Interest::default(), interest)
}
// SDK active contacts retain their flat interaction index across publication,
// even when that index is temporarily outside the new page's key count.
fn publish(s: &mut State, previous: &State, next: &State, layouts: &Layouts) {
    if s.active_index < 0 {
        if s.active_key == i16::MIN {
            return;
        }
        let mut index = 0;
        'find: for row in 0..layouts.rows(previous) as u8 {
            for col in 0..layouts.row(previous, row).count {
                if layouts
                    .key(previous, row, col)
                    .is_some_and(|key| key.value == s.active_key)
                {
                    s.active_index = index;
                    break 'find;
                }
                index += 1;
            }
        }
    }
    s.active_key = i16::MIN;
    let mut index = s.active_index;
    if index < 0 {
        return;
    }
    for row in 0..layouts.rows(next) as u8 {
        let count = i16::from(layouts.row(next, row).count);
        if index < count {
            s.active_key = layouts
                .key(next, row, index as u8)
                .map_or(i16::MIN, |key| key.value);
            return;
        }
        index -= count;
    }
}
/// # Safety
/// State is exclusive; previous/next/layouts are readable and disjoint from it.
/// SDK callback strings are immutable and valid for this synchronous call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_publish(
    state: *mut State,
    previous: *const State,
    next: *const State,
    layouts: *const Layouts,
) {
    let (Some(state), Some(previous), Some(next), Some(layouts)) = (
        unsafe { state.as_mut() },
        unsafe { previous.as_ref() },
        unsafe { next.as_ref() },
        unsafe { layouts.as_ref() },
    ) else {
        return;
    };
    if previous.layout >= 10 || next.layout >= 10 || layouts.row.is_none() || layouts.key.is_none()
    {
        return;
    }
    publish(state, previous, next, layouts);
}
/// # Safety
/// State and text are exclusive valid storage; input/layouts are readable and
/// disjoint. Callbacks return bounded immutable SDK data for this call only.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_keyboard_update(
    state: *mut State,
    text: *mut u8,
    capacity: u32,
    layouts: *const Layouts,
    input: *const Input,
    tapped_cursor: u32,
) -> Effect {
    let (Some(state), Some(layouts), Some(input)) = (
        unsafe { state.as_mut() },
        unsafe { layouts.as_ref() },
        unsafe { input.as_ref() },
    ) else {
        return Effect::default();
    };
    if text.is_null() || capacity == 0 || capacity as usize > isize::MAX as usize {
        return Effect::default();
    }
    update(
        state,
        unsafe { core::slice::from_raw_parts_mut(text, capacity as usize) },
        layouts,
        input,
        tapped_cursor,
    )
}
#[cfg(test)]
mod tests {
    use super::*;
    use core::mem::{align_of, offset_of, size_of};
    unsafe extern "C" fn row(_: *mut c_void, id: u8, _: u8, _: u8, _: u8, r: u8) -> Row {
        if id < 10 && r == 0 {
            Row {
                count: 1,
                ..Row::default()
            }
        } else {
            Row::default()
        }
    }
    unsafe extern "C" fn key(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, _: u8, _: u8) -> WireKey {
        WireKey {
            label: c"é".as_ptr(),
            output: c"é".as_ptr(),
            alternate: core::ptr::null(),
            value: 1000,
            kind: 0,
            units: 2,
        }
    }
    fn layouts() -> Layouts {
        Layouts {
            context: core::ptr::null_mut(),
            row: Some(row),
            key: Some(key),
        }
    }
    #[test]
    fn wire_layout() {
        assert_eq!(
            (
                size_of::<State>(),
                align_of::<State>(),
                offset_of!(State, row),
                offset_of!(State, layout),
                offset_of!(State, active_index)
            ),
            (64, 4, 32, 40, 62)
        );
        assert_eq!(
            (
                size_of::<Row>(),
                align_of::<Row>(),
                offset_of!(Row, independent)
            ),
            (4, 1, 2)
        );
        let p = size_of::<usize>();
        assert_eq!(size_of::<WireKey>(), if p == 4 { 16 } else { 32 });
        assert_eq!(offset_of!(WireKey, value), 3 * p);
        assert_eq!(align_of::<WireKey>(), p);
        assert_eq!(size_of::<Layouts>(), 3 * p);
        assert_eq!(offset_of!(Layouts, key), 2 * p);
        assert_eq!(align_of::<Layouts>(), p);
        assert_eq!(
            (
                size_of::<Effect>(),
                align_of::<Effect>(),
                offset_of!(Effect, required_capacity),
                offset_of!(Effect, haptics)
            ),
            (16, 4, 8, 12)
        );
    }
    #[test]
    fn published_active_index_survives_a_shorter_page() {
        unsafe extern "C" fn rows(_: *mut c_void, id: u8, _: u8, _: u8, _: u8, row: u8) -> Row {
            Row {
                count: if row == 0 { 1 + u8::from(id == 1) } else { 0 },
                ..Row::default()
            }
        }
        unsafe extern "C" fn keys(
            _: *mut c_void,
            _: u8,
            shift: u8,
            _: u8,
            _: u8,
            _: u8,
            col: u8,
        ) -> WireKey {
            WireKey {
                value: 1000 + i16::from(shift) * 1000 + i16::from(col),
                ..unsafe { key(core::ptr::null_mut(), 0, 0, 0, 0, 0, 0) }
            }
        }
        let layouts = Layouts {
            context: core::ptr::null_mut(),
            row: Some(rows),
            key: Some(keys),
        };
        let mut previous = init(0, 0, 0, 3, 0);
        previous.layout = 1;
        let mut live = previous;
        live.active_key = 1001;
        let mut next = previous;
        next.layout = 0;
        next.shifted = 1;
        publish(&mut live, &previous, &next, &layouts);
        assert_eq!((live.active_index, live.active_key), (1, i16::MIN));
        previous = next;
        next.layout = 1;
        publish(&mut live, &previous, &next, &layouts);
        assert_eq!((live.active_index, live.active_key), (1, 2001));

        let mut text = [0; 8];
        let release = Input {
            touch_flags: model::TOUCH_RELEASED,
            ..Input::default()
        };
        update(&mut live, &mut text, &layouts, &release, u32::MAX);
        assert_eq!((live.active_index, live.active_key), (-1, i16::MIN));
        let press = Input {
            action: 1,
            value: 2000,
            touch_flags: model::TOUCH_PRESSED,
            ..Input::default()
        };
        update(&mut live, &mut text, &layouts, &press, u32::MAX);
        publish(&mut live, &next, &next, &layouts);
        assert_eq!((live.active_index, live.active_key), (0, 2000));
    }
    #[test]
    fn growth_failure_and_maximum() {
        let l = layouts();
        let mut s = init(1, 0, 0, 1, 0);
        let mut b = [b'a', 0];
        let e = s.activate(&mut b, &l, 1000, false, 0);
        assert_eq!(e.required_capacity, 4);
        assert_eq!(e.grow, 1);
        assert_eq!(b, [b'a', 0]);
        assert_eq!((s.cursor, s.length), (1, 1));
        s.max_length = 2;
        let e = s.activate(&mut b, &l, 1000, false, 0);
        assert_eq!(e.grow, 0);
        assert_eq!((s.cursor, s.length), (1, 1));
        s.max_length = 0;
        let mut b = [b'a', 0, 0, 0];
        let e = s.activate(&mut b, &l, 1000, false, 0);
        assert_eq!(e.grow, 0);
        assert_eq!(&b, b"a\xc3\xa9\0");
        assert_eq!((s.cursor, s.length), (3, 3));
        assert!(s.backspace(&mut b));
        assert_eq!((s.cursor, s.length), (1, 1));
        assert_eq!(&b[..2], b"a\0");
    }
    #[test]
    fn invalid_callbacks_and_indices() {
        let mut s = init(0, 0, 0, 1, 0);
        let mut b = [0; 8];
        let l = Layouts {
            context: core::ptr::null_mut(),
            row: None,
            key: None,
        };
        let e = update(
            &mut s,
            &mut b,
            &l,
            &Input {
                released: model::CONFIRM,
                ..Input::default()
            },
            u32::MAX,
        );
        assert_eq!(e.navigation.invalidate, 0);
        assert_eq!(s.length, 0);
        let l = layouts();
        assert!(l.key(&s, 5, 0).is_none());
        assert!(l.key(&s, 0, 1).is_none());
        s.layout = 10;
        assert!(l.key(&s, 0, 0).is_none());
    }
    #[test]
    fn cancelled_touch_does_not_type() {
        let l = layouts();
        let mut s = init(0, 0, 0, 1, 0);
        let mut b = [0; 8];
        update(
            &mut s,
            &mut b,
            &l,
            &Input {
                touch_flags: model::TOUCH_PRESSED | model::TOUCH_HELD,
                action: 1,
                value: 1000,
                now_ms: 10,
                ..Input::default()
            },
            u32::MAX,
        );
        let effect = update(
            &mut s,
            &mut b,
            &l,
            &Input {
                touch_flags: model::TOUCH_RELEASED,
                touch_x: -1,
                touch_y: -1,
                now_ms: 20,
                ..Input::default()
            },
            u32::MAX,
        );
        assert_eq!(s.length, 0);
        assert_eq!(effect.navigation.kind, EffectKind::None as u8);
    }
}
#[cfg(test)]
mod touch_tests {
    use super::*;
    unsafe extern "C" fn row(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, r: u8) -> Row {
        Row {
            count: if r == 0 { 2 } else { 0 },
            ..Row::default()
        }
    }
    unsafe extern "C" fn key(_: *mut c_void, _: u8, _: u8, _: u8, _: u8, _: u8, c: u8) -> WireKey {
        WireKey {
            label: c"a".as_ptr(),
            output: c"a".as_ptr(),
            alternate: core::ptr::null(),
            value: if c == 0 { 97 } else { 8 },
            kind: if c == 0 { 0 } else { 4 },
            units: 2,
        }
    }
    #[test]
    fn threshold_release_slide_and_wrap() {
        let tables = Layouts {
            context: core::ptr::null_mut(),
            row: Some(row),
            key: Some(key),
        };
        let mut state = init(0, 0, 0, 1, 0);
        let mut bytes = [0; 16];
        let mut frame = Input {
            touch_flags: 5,
            action: 1,
            value: 97,
            now_ms: u32::MAX - 100,
            ..Input::default()
        };
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        frame.now_ms = 248;
        assert_eq!(
            update(&mut state, &mut bytes, &tables, &frame, u32::MAX)
                .navigation
                .invalidate,
            0
        );
        assert_eq!(state.length, 0);
        frame.now_ms = 249;
        assert_eq!(
            update(&mut state, &mut bytes, &tables, &frame, u32::MAX).haptics,
            2
        );
        assert_eq!(&bytes[..2], b"A\0");
        frame.touch_flags = 2;
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        assert_eq!(state.length, 1);
        frame.touch_flags = 5;
        frame.now_ms = 500;
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        frame.value = 8;
        frame.now_ms = 800;
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        frame.now_ms = 1699;
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        assert_eq!(state.length, 1);
        frame.now_ms = 1700;
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        assert_eq!(state.length, 0);
        frame.touch_flags = 2;
        update(&mut state, &mut bytes, &tables, &frame, u32::MAX);
        assert_eq!(state.delete_count, 0);
    }
}
