extern crate std;
use self::std::vec::Vec;
use super::view::{Content, Resources, Row};
use crate::{
    list::{Rows, Style},
    scene::{Arena, Command, Error, Kind, Rect, Refresh},
};

struct Metrics {
    label_width: i16,
    label_height: i32,
    subtitle_height: i32,
}
impl Resources for Metrics {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, _: u16) -> Result<Row, Error> {
        Ok(Row {
            label: 1,
            subtitle: 2,
            value: 3,
            enabled: 1,
            ..Row::default()
        })
    }
    fn width(&mut self, handle: u32, _: i32, _: u8) -> Result<i32, Error> {
        Ok(if handle == 1 {
            "A long network or plugin name".len() as i32 * 10
        } else {
            32
        })
    }
    fn height(&mut self, handle: u32, _: i32, _: u8, _: u8, width: i16) -> Result<i32, Error> {
        if handle == 1 {
            assert_eq!(width, self.label_width);
            Ok(self.label_height)
        } else {
            Ok(self.subtitle_height)
        }
    }
    fn line(&mut self, font: i32) -> Result<i32, Error> {
        Ok(if font == 0 { 18 } else { 24 })
    }
}

#[test]
fn wrapped_subtitles_match_sdk_row_measurements() {
    let golden = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../test/ui_settings/subtitles.golden"
    ));
    for record in golden.lines() {
        let n: Vec<i32> = record
            .split_whitespace()
            .map(|n| n.parse().unwrap())
            .collect();
        let mut metrics = Metrics {
            label_width: n[6] as i16,
            label_height: n[7],
            subtitle_height: n[8],
        };
        let mut rows = Content::new(&mut metrics, 1, 0, n[1] as i16, 2, 8);
        rows.subtitle_lines = n[3] as u8;
        rows.balance_value = n[4] != 0;
        let style = Style {
            font: 1,
            padding: 8,
            row_height: n[2] as i16,
            ..Style::default()
        };
        let height = rows.height(0, n[0] as i16, style).unwrap();
        assert_eq!(i32::from(height), n[10], "{record}");
        let mut commands = [Command::default(); 3];
        let mut arena = Arena::new(&mut commands, &mut [], &mut []).unwrap();
        rows.paint(
            &mut arena,
            0,
            Rect {
                w: n[0] as i16,
                h: height,
                ..Rect::default()
            },
            style,
            0,
            1,
        )
        .unwrap();
        let scene = arena.finish(Refresh::None, 1).unwrap();
        let subtitle = scene.commands.iter().find(|c| c.resource == 2).unwrap();
        assert_eq!(subtitle.kind, Kind::TextHandle as u8);
        assert_eq!(subtitle.max_lines, n[3] as u8);
        assert_eq!(i32::from(subtitle.rect.h), n[8]);
        assert_eq!(subtitle.rect.w, n[0] as i16 - 16);
        assert_eq!(
            i32::from(subtitle.rect.y),
            ((n[10] - n[7] - n[8]) / 2).max(0) + n[7]
        );
    }
}
