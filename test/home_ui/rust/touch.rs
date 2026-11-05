use xp_ui::{
    home::{file, library},
    model::{self, Input, ListState},
};
fn main() {
    for family in 0..5 {
        for count in [0u16, 1, 5] {
            for open in [-1, 0, 3] {
                for event in 0..10 {
                    for row in [-1, 0, 4, 5] {
                        let mut i = Input {
                            touch_x: 10,
                            touch_y: 10,
                            action: if event == 3 { 2 } else { 1 },
                            value: row,
                            ..Input::default()
                        };
                        if event == 1 || event == 3 {
                            i.touch_flags = model::TOUCH_RELEASED;
                        }
                        i.swipe = match event {
                            4 => model::SWIPE_LEFT,
                            5 => model::SWIPE_RIGHT,
                            6 => model::SWIPE_UP,
                            7 => model::SWIPE_DOWN,
                            _ => 0,
                        };
                        // The native fixture's resource-only button leaf does nothing; exposed-reveal closes still run.
                        if open >= 0 {
                            if event == 8 {
                                i.released = model::BACK;
                            }
                            if event == 9 {
                                i.released = model::CONFIRM;
                            }
                        }
                        let mut nav = ListState::new(1);
                        nav.visible = 3;
                        nav.drawn = 2;
                        nav.drawn_count = i32::from(count);
                        let (selected, reveal, scroll, kind, value, invalid) = if family < 2 {
                            let mut s = file::State {
                                list: nav,
                                revealed: open,
                                picker: u8::from(family == 1),
                                reserved: [0; 3],
                            };
                            let f = file::Facts {
                                count,
                                long_touch: u8::from(event == 2),
                                touch: 1,
                                ..file::Facts::default()
                            };
                            let r = s.update_full(i, f);
                            let k = match r.effect.kind {
                                3 => 0,
                                9 | 10 => 2,
                                n => n,
                            };
                            (
                                s.list.selected,
                                s.revealed,
                                s.list.pending_scroll,
                                k,
                                r.effect.value,
                                r.effect.invalidate,
                            )
                        } else {
                            let mut s = library::State::default();
                            s.lists[0] = nav;
                            s.sort = if family == 3 { 2 } else { 1 };
                            s.collapsed = u8::from(family == 4);
                            s.revealed = open;
                            let f = library::Facts {
                                count,
                                books: count,
                                long_touch: u8::from(event == 2),
                                touch: 1,
                                ..library::Facts::default()
                            };
                            let r = s.update_full(i, f);
                            let k = if r.effect.kind == 7 {
                                11
                            } else if r.effect.kind == 3 {
                                0
                            } else {
                                r.effect.kind
                            };
                            (
                                s.nav().selected,
                                s.revealed,
                                s.nav().pending_scroll,
                                k,
                                r.effect.value,
                                r.effect.invalidate,
                            )
                        };
                        println!(
                            "{family} {count} {open} {event} {row} : {selected} {reveal} {scroll} {kind} {} {invalid}",
                            if kind == 0 { -1 } else { value }
                        );
                    }
                }
            }
        }
    }
}
