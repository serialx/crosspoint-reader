#![allow(dead_code)]
use home::file::*;
use model::*;
use xp_ui::home;
pub use xp_ui::{ffi, model, scene};
fn files(path: &str) -> Vec<&'static str> {
    match path {
        "/" => vec!["Books/", "Alpha.epub", "Beta.bin"],
        "/Books" => vec!["Child/", "Gamma.epub"],
        _ => vec![],
    }
}
fn main() {
    for picker in [0, 1] {
        for path in ["/", "/Books", "/Books/Child"] {
            for selection in [-1, 0, 1, 2, 3] {
                for event in 0..13 {
                    let mut base = path.to_string();
                    let rows = files(path);
                    let mut state = State::new(selection, picker != 0);
                    state.list.visible = 2;
                    let mut input = Input {
                        now_ms: 5000,
                        ..Input::default()
                    };
                    let mut long = false;
                    match event {
                        0 => input.released = CONFIRM,
                        1 => long = true,
                        2 => input.released = BACK,
                        3..=5 => {
                            input.released = BACK;
                            input.held_ms = 996 + event;
                        }
                        6 => input.pressed = NAV_NEXT,
                        7 => input.pressed = NAV_PREVIOUS,
                        8 => {
                            input.held = NAV_NEXT;
                            input.held_ms = 501;
                        }
                        9 => {
                            input.held = NAV_PREVIOUS;
                            input.held_ms = 501;
                        }
                        10 => input.released = CONFIRM | BACK,
                        11 => {
                            input.released = BACK;
                            long = true;
                        }
                        _ => {}
                    }
                    let facts = Facts {
                        count: rows.len() as u16,
                        root: u8::from(path == "/"),
                        directory: u8::from(
                            rows.get(selection as usize)
                                .is_some_and(|s| s.ends_with('/')),
                        ),
                        long_confirm: u8::from(long),
                        long_touch: 0,
                        touch: 0,
                        touch_directory: 0,
                    };
                    let update = state.update(input, facts);
                    let mut effect = String::new();
                    match update.effect.kind {
                        ROOT => base = "/".into(),
                        PARENT => {
                            let slash = base.rfind('/').unwrap();
                            let name = base[slash + 1..].to_string() + "/";
                            base.truncate(slash);
                            if base.is_empty() {
                                base = "/".into();
                            }
                            let rows = files(&base);
                            let selected = rows.iter().position(|s| *s == name).unwrap_or(0);
                            state.parent_loaded(selected as i32, rows.len() as u16);
                        }
                        HOME => effect = "home".into(),
                        1 => effect = "cancel".into(),
                        OPTIONS => effect = "options".into(),
                        DIRECTORY | OPEN | PICK => {
                            let name = rows[update.effect.value as usize];
                            let mut clean = base.clone();
                            if !clean.ends_with('/') {
                                clean.push('/');
                            }
                            if update.effect.kind == DIRECTORY {
                                base = clean + name.trim_end_matches('/');
                            } else if update.effect.kind == PICK {
                                effect = "result:".to_string() + &clean + name;
                            } else {
                                base = clean;
                                effect = "open:".to_string() + &base + name;
                            }
                        }
                        _ => {}
                    }
                    println!(
                        "file {picker} {path} {selection} {event}: {} {} {} {} {} {base} {effect}",
                        state.list.selected,
                        state.list.top,
                        state.list.follow,
                        update.effect.invalidate,
                        update.consumed
                    );
                }
            }
        }
    }
}
