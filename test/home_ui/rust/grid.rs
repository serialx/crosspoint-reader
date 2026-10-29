use xp_ui::{
    home::{HomeState, grid::*, view},
    scene::{Arena, Command, Error, Kind, Rect, Refresh},
};
struct Texts {
    progress: i32,
}
impl Texts {
    fn get(&self, h: u32) -> String {
        match h {
            view::TITLE => "A long title with several words".into(),
            view::AUTHOR => "An author".into(),
            view::NO_OPEN_BOOK => "No open book".into(),
            view::START_READING => "Start reading".into(),
            PROGRESS => format!("{}%", self.progress),
            _ => panic!("handle"),
        }
    }
}
impl xp_ui::home::grid::Texts for Texts {
    fn width(&mut self, h: u32, f: i32) -> Result<i16, Error> {
        Ok((self.get(h).len() * if f == 0 { 8 } else { 10 }) as i16)
    }
    fn line(&mut self, f: i32) -> Result<i16, Error> {
        Ok(if f == 0 { 18 } else { 24 })
    }
    fn height(&mut self, h: u32, f: i32, w: i16, n: u8) -> Result<i16, Error> {
        let text = self.get(h);
        let unit = if f == 0 { 8 } else { 10 };
        let mut lines = 1;
        let mut width = 0;
        for word in text.split_whitespace() {
            let next = word.len() as i32 * unit;
            let space = if width > 0 { unit } else { 0 };
            if width + space + next > i32::from(w) && width > 0 {
                lines += 1;
                width = next;
            } else {
                width += space + next;
            }
        }
        Ok(lines.min(i32::from(n)) as i16 * self.line(f)?)
    }
    fn empty(&mut self, h: u32) -> bool {
        self.get(h).is_empty()
    }
}
fn main() {
    for width in [480, 528, 792, 800] {
        for touch in [0, 1] {
            for books in [0, 1, 4, 7] {
                for selected in [0, 2, 7, 10] {
                    for progress in [-1, 42, 100] {
                        for opds in [0, 1] {
                            println!("grid {width} {touch} {books} {selected} {progress} {opds}");
                            let height = if width > 600 { 480 } else { 792 };
                            let screen = Rect {
                                x: 0,
                                y: 0,
                                w: width,
                                h: height,
                            };
                            let l = Layout {
                                screen,
                                safe: Rect {
                                    x: 4,
                                    y: 8,
                                    w: width - 8,
                                    h: height - 44,
                                },
                                body_font: 1,
                                small_font: 0,
                                title_font: 1,
                                progress,
                                space_sm: 4,
                                space_lg: 16,
                                header_side: 20,
                                header_top: 10,
                                header_height: 36,
                                corner: if touch != 0 { 12 } else { 0 },
                                tab_height: 50,
                                tab_height_pct: if touch != 0 { 90 } else { 100 },
                                tab_width_pct: if touch != 0 { 80 } else { 100 },
                                tab_offset: if touch != 0 { 3 } else { 0 },
                                touch,
                                body_bold: 0,
                                reserved: [0; 2],
                            };
                            let mut s = HomeState::new(books, true, opds != 0, false, books != 0);
                            s.selected = selected;
                            let mut t = Texts { progress };
                            let mut commands = [Command::default(); 96];
                            let mut a = Arena::new(&mut commands, &mut [], &mut []).unwrap();
                            let g = view(&mut a, s, &l, &mut t).unwrap();
                            let scene = a.finish(Refresh::Fast, 1).unwrap();
                            for c in scene.commands {
                                let r = c.rect;
                                match Kind::from_byte(c.kind).unwrap() {
                                    Kind::Fill => println!(
                                        "fill {} {} {} {} {} {} {} {}",
                                        r.x,
                                        r.y,
                                        r.w,
                                        r.h,
                                        if c.paint == 2 { 2 } else { 1 },
                                        if c.paint == 2 {
                                            2
                                        } else if c.paint == 1 {
                                            1
                                        } else {
                                            4
                                        },
                                        c.radius,
                                        c.corners
                                    ),
                                    Kind::Stroke => println!(
                                        "stroke {} {} {} {} {} {} {} {} {}",
                                        r.x,
                                        r.y,
                                        r.w,
                                        r.h,
                                        if c.paint == 2 { 2 } else { 1 },
                                        if c.paint == 2 {
                                            2
                                        } else if c.paint == 1 {
                                            1
                                        } else {
                                            4
                                        },
                                        c.width,
                                        c.radius,
                                        c.corners
                                    ),
                                    Kind::Page => {
                                        if c.resource == view::HEADER {
                                            println!("header {} {} {} {}", r.x, r.y, r.w, r.h)
                                        } else if c.resource >= GRID_ICON {
                                            println!(
                                                "icon {} {} {} {}",
                                                c.resource - GRID_ICON,
                                                r.x,
                                                r.y,
                                                r.w
                                            )
                                        } else {
                                            println!(
                                                "cover {} {} {} {} {}",
                                                r.x,
                                                r.y,
                                                r.w,
                                                r.h,
                                                c.resource - GRID_COVER
                                            )
                                        }
                                    }
                                    Kind::TextHandle => {
                                        println!(
                                            "text {} {} {} {} {} {} 4 {} {} 0 {}",
                                            r.x,
                                            r.y,
                                            r.w,
                                            r.h,
                                            c.font,
                                            c.align,
                                            c.max_lines,
                                            c.style,
                                            t.get(c.resource)
                                        );
                                        let w = xp_ui::home::grid::Texts::width(
                                            &mut t, c.resource, c.font,
                                        )
                                        .unwrap();
                                        if w <= r.w {
                                            let x =
                                                r.x + if c.align == 1 { (r.w - w) / 2 } else { 0 };
                                            let y = r.y
                                                + ((r.h
                                                    - xp_ui::home::grid::Texts::line(
                                                        &mut t, c.font,
                                                    )
                                                    .unwrap())
                                                    / 2)
                                                .max(0);
                                            println!("origin {x} {y}");
                                        }
                                    }
                                    _ => panic!("command"),
                                }
                            }
                            println!("height {}", g.thumb_height);
                            for i in 0..g.count() {
                                let r = g.hit_rect(i).unwrap();
                                println!("hit {} {} {} {} 1 {i}", r.x, r.y, r.w, r.h);
                            }
                        }
                    }
                }
            }
        }
    }
}
