use xp_ui::{
    model::ListState,
    reader_chrome::{hits, list},
    scene::{Arena, Command, Error, Rect, Refresh},
};

// Chapter rows have one label and no icons, subtitles, values, toggles or overlays.
// 24 * (background + label) + clear/header/footer + selection + clip/pop + track/thumb.
const CHAPTER_COMMAND_BOUND: usize = 56;
struct Labels;
impl list::Resources for Labels {
    fn window(&mut self, _: u16, count: u16) -> Result<(), Error> {
        assert!(count <= 24);
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        Ok(list::Row {
            label: u32::from(index) * 4 + 1,
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
fn exhaustive_chapter_command_branches_fit_fifty_six_commands() {
    let mut peak = 0;
    let mut cases = 0;
    let mut commands = [Command::default(); CHAPTER_COMMAND_BOUND];
    // Geometry is partitioned at each command-producing boundary: an exact row,
    // less than 18 px left, the first partial row, and its final partial pixel.
    // Text width/indent and polarity change command data, never command count.
    for orientation in 0..4 {
        let (w, h) = if orientation % 2 == 0 {
            (480, 800)
        } else {
            (800, 480)
        };
        for touch in 0..2 {
            let row = if touch == 0 { 32 } else { 56 };
            let gap = if touch == 0 { 0 } else { 6 };
            for full in 0..=24 {
                for remainder in [0, 17, 18, row - 1] {
                    let height = full * (row + gap) + remainder;
                    if height > h {
                        continue;
                    }
                    for total in [0, full, full + 1, 24, 25, 1000, 65535] {
                        for top in [0, (total - 24).max(0)] {
                            for selected in -1..=25 {
                                let selected = if selected < 0 {
                                    -1
                                } else {
                                    (top + selected).min(65535)
                                };
                                for active in [-1, top] {
                                    for selection in 0..4 {
                                        for (scroll_width, scroll_left) in
                                            [(0, 0), (0, 1), (3, 0), (3, 1)]
                                        {
                                            let theme = list::Theme {
                                                screen: Rect {
                                                    x: 0,
                                                    y: 0,
                                                    w: w as i16,
                                                    h: h as i16,
                                                },
                                                safe: Rect {
                                                    x: 0,
                                                    y: 0,
                                                    w: w as i16,
                                                    h: height as i16,
                                                },
                                                touch,
                                                selection,
                                                scroll_width,
                                                scroll_left,
                                                orientation,
                                                ..Default::default()
                                            };
                                            let mut hits = hits::Layout::new(1, 1);
                                            let mut arena =
                                                Arena::with_hit_sink(&mut commands, &mut hits)
                                                    .unwrap();
                                            let state = ListState {
                                                selected,
                                                top,
                                                active,
                                                follow: 0,
                                                ..Default::default()
                                            };
                                            list::view(
                                                &mut arena,
                                                state,
                                                list::Screen::Chapters,
                                                theme,
                                                &mut Labels,
                                                total as u16,
                                                24,
                                                1,
                                                -1,
                                            )
                                            .unwrap_or_else(|error| panic!("{error:?}: orientation={orientation} touch={touch} full={full} remainder={remainder} total={total} top={top} selected={selected} active={active} selection={selection} scroll={scroll_width}/{scroll_left}"));
                                            let scene = arena.finish(Refresh::Fast, 1).unwrap();
                                            peak = peak.max(scene.commands.len());
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
    }
    assert_eq!(peak, CHAPTER_COMMAND_BOUND);
    println!("Chapter exhaustive host command bound: {peak}, {cases} compositions");
}
