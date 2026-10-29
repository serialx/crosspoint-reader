#![allow(dead_code)]
use home::cache::*;
use scene::Rect;
use xp_ui::home;
pub use xp_ui::{ffi, model, scene};
fn main() {
    for flags in 0..8 {
        for status in 0..5 {
            let mut state = State::default();
            state.capacity = if flags & 1 != 0 {
                0
            } else {
                State::capacity(480 * 800 / 8, 480, 800)
            };
            let mut orientation = 0;
            for step in 0..12 {
                if step == 4 {
                    orientation = 1;
                }
                if step == 6 {
                    state.invalidate_slot(0);
                }
                if step == 9 {
                    state.invalidate();
                }
                state.prepare(orientation);
                let r = Rect {
                    x: if step == 3 { 21 } else { 20 },
                    y: 10,
                    w: if step == 5 { 301 } else { 100 },
                    h: if step == 5 { 400 } else { 150 },
                };
                let index = if step == 11 {
                    7
                } else if step == 8 {
                    1
                } else {
                    0
                };
                let mut trace = 0;
                if index < 7 {
                    let cached = state.lookup(index, r);
                    if cached.bytes != 0 {
                        trace |= 32;
                    }
                    if cached.bytes == 0 || flags & 2 != 0 {
                        state.invalidate_slot(index);
                        if status == 0 || status == 3 {
                            trace |= 1;
                        }
                        trace |= if status == 0 { 2 | 4 | 8 } else { 16 | 2 };
                        let needed = (((i32::from(r.x) + i32::from(r.w) + 7) / 8
                            - i32::from(r.x) / 8)
                            * i32::from(r.h)) as u32;
                        let slot = state.reserve(index, r, needed);
                        if slot.bytes != 0 {
                            trace |= 64;
                            state.copied(index, flags & 4 == 0);
                        }
                    }
                }
                print!(
                    "cache {flags} {status} {step}: {} {trace} {} {} {}",
                    u8::from(index < 7),
                    state.capacity,
                    state.used,
                    state.orientation
                );
                for s in state.slots {
                    print!(
                        " {},{},{},{},{},{},{}",
                        s.rect.x, s.rect.y, s.rect.w, s.rect.h, s.offset, s.bytes, s.valid
                    );
                }
                println!();
            }
        }
    }
}
