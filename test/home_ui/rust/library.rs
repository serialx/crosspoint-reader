#![allow(dead_code)]
use home::library::*;
use model::*;
use xp_ui::home;
pub use xp_ui::{ffi, model, scene};
fn main() {
    for tab in 0..3 {
        for count in [0, 1, 12] {
            for initial in [0, 1, 12] {
                for flags in 0..8 {
                    for event in 0..15 {
                        let mut state = State {
                            active_tab: tab,
                            sort: 2 * tab + u8::from(tab == 0),
                            degraded: flags & 1,
                            collapsed: u8::from(flags & 2 != 0),
                            queried: u8::from(flags & 4 != 0),
                            started_on_tabs: u8::from(initial == 0),
                            ..State::default()
                        };
                        state.nav_mut().selected = initial;
                        state.nav_mut().visible = 3;
                        state.nav_mut().drawn = 2;
                        let mut input = Input {
                            now_ms: 5000,
                            ..Input::default()
                        };
                        let mut long = false;
                        match event {
                            0 => input.released = CONFIRM,
                            1 => long = true,
                            2 => input.released = BACK,
                            3 => input.pressed = NAV_NEXT,
                            4 => input.pressed = NAV_PREVIOUS,
                            5 => input.released = NAV_PREVIOUS,
                            6 => {
                                input.held = NAV_NEXT;
                                input.held_ms = 501;
                            }
                            7 => {
                                input.held = NAV_PREVIOUS;
                                input.held_ms = 501;
                            }
                            8 => input.released = CONFIRM | BACK,
                            9 => {
                                input.released = BACK;
                                long = true;
                            }
                            10 => {
                                input.released = CONFIRM;
                                state.suppress_confirm = 1;
                            }
                            11 => {
                                input.released = CONFIRM | BACK;
                                state.suppress_confirm = 1;
                                state.suppress_back = 1;
                            }
                            12 => {
                                input.released = BACK;
                                state.suppress_back = 1;
                            }
                            13 => input.pressed = NAV_NEXT | NAV_PREVIOUS,
                            _ => {}
                        }
                        let update = state.update(
                            input,
                            Facts {
                                count,
                                books: count,
                                long_confirm: u8::from(long),
                                long_touch: 0,
                                touch: 0,
                                filter_failed: 0,
                            },
                        );
                        let e = update.effect;
                        let text = match e.kind {
                            HOME => "home".into(),
                            SEARCH => "search".into(),
                            OPTIONS => format!("options:{}", e.value),
                            DELETE => format!("delete:{}", e.value),
                            COLLAPSE => format!("collapse:{}", e.value),
                            EXPAND => "expand".into(),
                            2 => format!("activate:{}", e.value),
                            _ => String::new(),
                        };
                        println!(
                            "library {tab} {count} {initial} {flags} {event}: {} {} {} {} {} {} {} {} {} {} {} {} {text}",
                            state.active_tab,
                            state.descending_tabs,
                            state.sort,
                            state.nav().selected,
                            state.nav().top,
                            state.nav().follow,
                            state.collapsed,
                            state.queried,
                            state.suppress_confirm,
                            state.suppress_back,
                            e.invalidate,
                            update.consumed
                        );
                    }
                }
            }
        }
    }
}
