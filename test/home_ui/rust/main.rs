#![allow(dead_code)]
use home::{HomeState, HomeTouch};
use model::*;
use xp_ui::home;
pub use xp_ui::{ffi, model, scene};
fn main() {
    for grid in [0, 1] {
        for count in [0, 1, 3, 7] {
            for slot in [0, 1, 2] {
                for initial in 0..count + 4 + i32::from(slot != 0) {
                    for event in 0..25 {
                        let mut h = HomeState::new(
                            count as u8,
                            grid != 0,
                            slot == 1,
                            slot == 2,
                            count != 0,
                        );
                        h.selected = initial;
                        let mut input = Input {
                            now_ms: 5000,
                            touch_x: -1,
                            touch_y: -1,
                            ..Input::default()
                        };
                        let buttons = [NAV_NEXT, NAV_PREVIOUS, UP, DOWN, LEFT, RIGHT];
                        match event {
                            0..6 => input.pressed = buttons[event],
                            6..12 => {
                                input.held = buttons[event - 6];
                                input.held_ms = 501;
                            }
                            12 => input.released = BACK,
                            13 => input.released = CONFIRM,
                            14 => input.swipe = SWIPE_UP,
                            15 => input.swipe = SWIPE_DOWN,
                            16 => {
                                input.touch_flags = TOUCH_PRESSED;
                                input.touch_x = 30;
                                input.touch_y = 90;
                            }
                            17 => {
                                input.touch_flags = TOUCH_RELEASED;
                                input.touch_x = 30;
                                input.touch_y = 90;
                            }
                            18 => {
                                input.touch_flags = TOUCH_RELEASED;
                                input.touch_x = 30;
                                input.touch_y = 332;
                            }
                            19 => {
                                input.touch_flags = TOUCH_RELEASED;
                                input.touch_x = 30;
                                input.touch_y = 368;
                            }
                            20 => {
                                input.held = NAV_NEXT;
                                input.held_ms = 500;
                            }
                            21 => input.pressed = NAV_NEXT | NAV_PREVIOUS,
                            22 => {
                                input.pressed = NAV_NEXT;
                                input.swipe = SWIPE_UP;
                            }
                            23 => input.released = CONFIRM | BACK,
                            _ => {}
                        }
                        let touch = HomeTouch {
                            cover: scene::Rect {
                                x: 22,
                                y: 80,
                                w: 161,
                                h: 240,
                            },
                            menu: scene::Rect {
                                x: 0,
                                y: 332,
                                w: i16::MAX,
                                h: 36,
                            },
                            menu_step: 42,
                            columns: 3,
                            continue_in_menu: 0,
                        };
                        let update = h.update(input, &touch);
                        let effect = if update.effect.kind != 0 {
                            update.effect.value
                        } else {
                            -1
                        };
                        let mut updates = update.effect.invalidate as i32;
                        println!(
                            "home {grid} {count} {slot} {initial} {event}: {} {effect} {updates} {}",
                            h.selected, update.haptic
                        );
                        if (6..12).contains(&event) {
                            for ms in [500, 501, 1001, 1002] {
                                input.now_ms = 5000 + ms;
                                updates += h.update(input, &touch).effect.invalidate as i32;
                                println!("repeat {ms}: {} {updates}", h.selected);
                            }
                        }
                    }
                }
            }
        }
    }
}
