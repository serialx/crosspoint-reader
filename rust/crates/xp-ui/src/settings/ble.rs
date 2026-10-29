use crate::model::Effect;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub shown_signature: u32,
    pub last_poll: u32,
    pub code: u32,
    pub press_ms: u32,
    pub since_ms: u32,
    pub view: u8,
    pub action: u8,
    pub note: u8,
    pub reserved: u8,
}
const ACTIONS: [u8; 8] = [1, 2, 3, 4, 7, 8, 5, 6];
pub fn action(index: i32) -> u8 {
    usize::try_from(index)
        .ok()
        .and_then(|i| ACTIONS.get(i))
        .copied()
        .unwrap_or(0)
}
impl State {
    pub fn show(&mut self, view: u8) {
        if view < 4 {
            self.view = view;
            self.action = 0;
            self.note = 0;
        }
    }
    pub fn poll(&mut self, now: u32) -> bool {
        if now.wrapping_sub(self.last_poll) < 250 {
            return false;
        }
        self.last_poll = now;
        true
    }
    pub fn back(&mut self) -> u8 {
        if self.action != 0 {
            self.action = 0;
            self.note = 0;
            1
        } else if self.view == 0 {
            2
        } else {
            let stop = self.view == 1;
            self.show(0);
            if stop {
                4
            } else {
                3
            }
        }
    }
    pub fn begin(&mut self, index: i32, connected: bool, now: u32) -> bool {
        let a = action(index);
        if a == 0 || !connected {
            return false;
        }
        self.action = a;
        self.code = 0;
        self.since_ms = now;
        self.note = 0;
        true
    }
    // 1 saves a tap, 2 saves a hold, 3 repaints a terminal learning result.
    pub fn step(&mut self, kind: u8, code: u32, at: u32, now: u32) -> u8 {
        if self.action == 0 {
            return 0;
        }
        match kind {
            0 => {
                self.action = 0;
                self.note = 1;
                3
            }
            1 => {
                self.code = code;
                self.press_ms = at;
                self.since_ms = now;
                0
            }
            2 | 3 if self.code != 0 && self.code == code => {
                if kind == 3 {
                    self.code = 0;
                    0
                } else {
                    1 + u8::from(at.wrapping_sub(self.press_ms) >= 700)
                }
            }
            4 if now.wrapping_sub(self.since_ms) >= 15000 => {
                if self.code != 0 {
                    1
                } else {
                    self.action = 0;
                    self.note = 2;
                    3
                }
            }
            5 | 6 => {
                self.action = 0;
                self.note = if kind == 5 { 3 } else { 4 };
                if kind == 6 {
                    self.code = 0;
                    self.since_ms = now;
                }
                3
            }
            _ => 0,
        }
    }
    pub fn clear(&mut self, index: i32) -> u8 {
        if self.action != 0 {
            return 0;
        }
        let a = action(index);
        if a != 0 {
            self.note = 5;
        }
        a
    }
    pub fn activate(&self, code: i16, enabled: bool, scanning: bool) -> Effect {
        // kind: toggle, scan, show, pair, forget, learn; value carries the argument.
        let (kind, value) = match (self.view, code) {
            (0, 0) => (10, i32::from(!enabled)),
            (0, 1) => (11, 15000),
            (0, 2 | 3) => (12, i32::from(code)),
            (1, c) if c < 0 => (11, if scanning { 0 } else { 15000 }),
            (1, c) => (13, i32::from(c)),
            (2, c) => (14, i32::from(c)),
            (3, c) => (15, i32::from(c)),
            _ => (0, 0),
        };
        Effect {
            kind,
            value,
            ..Effect::default()
        }
    }
}
pub fn status(flags: u8, failure: bool) -> u8 {
    if flags & 32 == 0 || flags & 16 != 0 {
        0
    } else if failure {
        1
    } else if flags & 1 == 0 {
        2
    } else if flags & 4 != 0 {
        3
    } else if flags & 8 != 0 {
        4
    } else if flags & 2 != 0 {
        5
    } else {
        6
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Row {
    pub code: i16,
    pub label: u8,
    pub subtitle: u8,
    pub value: u8,
    pub enabled: u8,
    pub toggle: u8,
    pub checked: u8,
}
// labels 0..3 menu,4 stop scan,5 scanning,6 no devices,7 no remotes,8+ maps,16+ peers.
// subtitle 1 status,2 connect,3 press,4 note; value 1 forget,2 mapping.
pub fn row(s: &State, flags: u8, found: u8, bonds: u8, index: u16) -> Option<Row> {
    let running = flags & 1 != 0;
    let scanning = flags & 2 != 0;
    let connected = flags & 4 != 0;
    let (found, bonds) = if running { (found, bonds) } else { (0, 0) };
    let r = match s.view {
        0 if index < 4 => Row {
            code: index as i16,
            label: index as u8,
            subtitle: if index == 0 {
                1
            } else if index == 3 && !connected {
                2
            } else {
                0
            },
            enabled: u8::from(index == 0 || if index == 3 { connected } else { running }),
            toggle: u8::from(index == 0),
            checked: u8::from(flags & 32 != 0),
            ..Row::default()
        },
        1 if index == 0 => Row {
            code: -1,
            label: if scanning { 4 } else { 1 },
            enabled: u8::from(running),
            ..Row::default()
        },
        1 if found == 0 && index == 1 => Row {
            code: -1,
            label: if scanning { 5 } else { 6 },
            ..Row::default()
        },
        1 if index <= u16::from(found) => Row {
            code: (index - 1) as i16,
            label: 16 + (index - 1) as u8,
            enabled: 1,
            ..Row::default()
        },
        2 if bonds == 0 && index == 0 => Row {
            code: -1,
            label: 7,
            ..Row::default()
        },
        2 if index < u16::from(bonds) => Row {
            code: index as i16,
            label: 16 + found + index as u8,
            value: 1,
            enabled: 1,
            ..Row::default()
        },
        3 if index < 8 => Row {
            code: index as i16,
            label: 8 + index as u8,
            value: 2,
            enabled: u8::from(connected && s.action == 0),
            subtitle: if !connected {
                2
            } else if action(i32::from(index)) == s.action {
                3
            } else if index == 0 {
                4
            } else {
                0
            },
            ..Row::default()
        },
        _ => return None,
    };
    Some(r)
}

#[cfg(test)]
mod tests {
    extern crate std;
    use self::std::vec::Vec;
    use super::*;
    #[test]
    fn original_learning_traces() {
        let mut last = u32::MAX;
        let mut s = State::default();
        let (mut saved, mut held, mut updates) = (0, -1, 0);
        let golden = include_str!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../../test/ui_settings/ble.golden"
        ));
        for line in golden.lines() {
            let v: Vec<u32> = line
                .split_whitespace()
                .map(|x| x.parse::<i64>().unwrap() as u32)
                .collect();
            if v[0] != last {
                last = v[0];
                s = State {
                    action: 1,
                    ..State::default()
                };
                saved = 0;
                held = -1;
                updates = 0;
            }
            let action = s.action;
            if v[5] == 0 || v[6] == 0 {
                s.step(0, 0, 0, v[1]);
                updates += 1;
            } else {
                let mut result = if v[4] != 0 {
                    s.step(v[4] as u8, v[2], v[3], v[1])
                } else {
                    0
                };
                if result == 0 && s.action == action {
                    result = s.step(4, 0, 0, v[1]);
                }
                if result == 1 || result == 2 {
                    let ok = v[7] != 0 && v[8] != 0;
                    if v[7] != 0 {
                        held = if result == 2 { 1 } else { 0 };
                    }
                    if ok {
                        saved += 1;
                    }
                    s.step(if ok { 6 } else { 5 }, 0, 0, v[1]);
                }
                if s.action != action {
                    updates += 1;
                }
            }
            assert_eq!(
                [
                    u32::from(s.action),
                    u32::from(s.note),
                    s.code,
                    s.press_ms,
                    s.since_ms,
                    saved,
                    held as u32,
                    updates
                ],
                v[9..17],
                "{line}"
            );
        }
    }
}
