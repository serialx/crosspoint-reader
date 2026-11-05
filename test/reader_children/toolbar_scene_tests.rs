use xp_ui::{
    model::ListState,
    reader_chrome::{hits, list, toolbar},
    scene::{Arena, Command, Error, Rect, Refresh},
};

// Sixteen rows: background, label, checkbox outline and checked fill each.
// Chrome: sheet(3), title(1), tools(4), selection(1), partial clip/pop(2),
// scrollbar(2), and page indicator(1). Optional text/value cannot exceed this.
const PANEL_BOUND: usize = 78;
struct Rows {
    kind: u8,
    line: i32,
}
impl list::Resources for Rows {
    fn window(&mut self, _: u16, count: u16) -> Result<(), Error> {
        assert!(count <= 16);
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<list::Row, Error> {
        Ok(list::Row {
            label: u32::from(index) * 2 + 1,
            value: u32::from(self.kind == 1) * (u32::from(index) * 2 + 2),
            toggle: u8::from(self.kind >= 2),
            checked: u8::from(self.kind == 3),
            ..Default::default()
        })
    }
    fn width(&mut self, _: u32, _: i32, _: u8) -> Result<i32, Error> {
        Ok(32)
    }
    fn height(&mut self, _: u32, _: i32, _: u8, _: u8, _: i16) -> Result<i32, Error> {
        Ok(self.line)
    }
    fn line(&mut self, _: i32) -> Result<i32, Error> {
        Ok(self.line)
    }
}

#[test]
fn toolbar_command_branches_stay_within_panel_bound() {
    let mut commands = [Command::default(); PANEL_BOUND];
    let mut peaks = [0; 2];
    let mut cases = 0;
    // Includes synthetic small metrics that reach the full sixteen-row window.
    // Each optional per-row command branch is homogeneous: mixed rows cannot
    // exceed the all-checked case at the same row count.
    for (w, h) in [(480, 800), (800, 480)] {
        for (line, row, gap) in [(1, 28, 0), (24, 32, 0), (24, 48, 4), (36, 56, 6)] {
            for dense_rows in 0..2 {
                for kind in 0..4 {
                    for selection in 0..4 {
                        for (scroll_width, scroll_left) in [(0, 0), (3, 0), (3, 1)] {
                            for count in [0u16, 1, 15, 16, 17, 1000, 65535] {
                                for top in [0, count.saturating_sub(16)] {
                                    for selected in -1..=16 {
                                        for panel in 0..2 {
                                            let theme = list::Theme {
                                                screen: Rect { x: 0, y: 0, w, h },
                                                safe: Rect { x: 0, y: 0, w, h },
                                                row_min: row,
                                                row_gap: gap,
                                                selection,
                                                scroll_width,
                                                scroll_left,
                                                ..Default::default()
                                            };
                                            let model = toolbar::Model {
                                                panel,
                                                dense_rows,
                                                active_tool: 1,
                                                chapter_title: list::TITLE,
                                                page_info: list::PROGRESS,
                                                panel_title: list::HEADER,
                                                ..Default::default()
                                            };
                                            let mut hits = hits::Layout::new(6, 1);
                                            let mut arena =
                                                Arena::with_hit_sink(&mut commands, &mut hits)
                                                    .unwrap();
                                            toolbar::view(
                                                &mut arena,
                                                ListState {
                                                    top: i32::from(top),
                                                    selected,
                                                    ..Default::default()
                                                },
                                                model,
                                                theme,
                                                &mut Rows { kind, line },
                                                count,
                                                16,
                                                1,
                                            )
                                            .unwrap();
                                            let scene = arena.finish(Refresh::None, 1).unwrap();
                                            peaks[usize::from(panel)] =
                                                peaks[usize::from(panel)].max(scene.commands.len());
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
    assert_eq!(peaks[0], 17);
    assert_eq!(peaks[1], 72);
    assert!(peaks[1] <= PANEL_BOUND);
    println!("Toolbar host command bound {PANEL_BOUND}; reached {peaks:?}, {cases} compositions");
}
