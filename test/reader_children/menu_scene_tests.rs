use xp_ui::{
    model::ListState,
    reader_chrome::{hits, list},
    scene::{Arena, Command, Error, Rect, Refresh},
};

// 19 backgrounds/labels, three checked toggles, two values, clear/header/progress/footer,
// one selection decoration, one partial-row clip/pop, and scrollbar track/thumb.
const MENU_COMMAND_BOUND: usize = 19 * 2 + 3 * 2 + 2 + 4 + 1 + 2 + 2;
struct MenuRows;
impl list::Resources for MenuRows {
    fn window(&mut self, _: u16, count: u16) -> Result<(), Error> {
        assert!(count <= 19);
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        // Put every optional decoration in the leading rows: this dominates any
        // subset/order of the real menu when the viewport clips the trailing rows.
        Ok(list::Row {
            label: u32::from(index) * 4 + 1,
            toggle: u8::from(index < 3),
            checked: 1,
            value: if (3..5).contains(&index) {
                u32::from(index) * 4 + 3
            } else {
                0
            },
            ..Default::default()
        })
    }
    fn width(&mut self, _: u32, _: i32, _: u8) -> Result<i32, Error> {
        Ok(120)
    }
    fn height(&mut self, _: u32, _: i32, _: u8, _: u8, _: i16) -> Result<i32, Error> {
        Ok(24)
    }
    fn line(&mut self, _: i32) -> Result<i32, Error> {
        Ok(24)
    }
}

#[test]
fn exhaustive_menu_command_branches_fit_fifty_five_commands() {
    let mut commands = [Command::default(); MENU_COMMAND_BOUND];
    let mut peak = 0;
    let mut cases = 0;
    // Row-height/partial-row boundaries partition command-producing geometry.
    // Wrapping/font/translation/polarity changes dimensions or paint data only;
    // fewer rows, unchecked toggles and absent values cannot exceed this bound.
    for orientation in 0..4 {
        let (w, h) = if orientation % 2 == 0 {
            (480, 800)
        } else {
            (800, 480)
        };
        for touch in 0..2 {
            let row = if touch == 0 { 32 } else { 56 };
            let gap = if touch == 0 { 0 } else { 6 };
            for full in 0..=19 {
                for remainder in [0, 17, 18, row - 1] {
                    let height = full * (row + gap) + remainder;
                    if height > h {
                        continue;
                    }
                    for top in 0..19 {
                        for selected in -1..19 {
                            for active in [-1, selected] {
                                for selection in 0..4 {
                                    for scroll_left in 0..2 {
                                        let theme = list::Theme {
                                            screen: Rect {
                                                w,
                                                h,
                                                ..Default::default()
                                            },
                                            safe: Rect {
                                                w,
                                                h: height,
                                                ..Default::default()
                                            },
                                            touch,
                                            orientation,
                                            selection,
                                            scroll_width: 3,
                                            scroll_left,
                                            ..Default::default()
                                        };
                                        let mut hits = hits::Layout::new(1, 1);
                                        let mut arena =
                                            Arena::with_hit_sink(&mut commands, &mut hits).unwrap();
                                        list::view(
                                            &mut arena,
                                            ListState {
                                                selected,
                                                top,
                                                active,
                                                follow: 0,
                                                ..Default::default()
                                            },
                                            list::Screen::Menu,
                                            theme,
                                            &mut MenuRows,
                                            19,
                                            19,
                                            1,
                                            -1,
                                        )
                                        .unwrap();
                                        peak = peak.max(
                                            arena.finish(Refresh::Fast, 1).unwrap().commands.len(),
                                        );
                                        cases += 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    assert_eq!(peak, MENU_COMMAND_BOUND);
    println!("Menu host bound: {peak} commands; {cases} geometry/selection compositions with maximal decorations");
}
