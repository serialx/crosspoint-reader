use xp_ui::{
    home::rows::{self, Resources, Row},
    list::{self, Style},
    model::ListState,
    scene::{Arena, Command, Error, Interaction, Kind, Rect, Refresh},
};
struct Data {
    kind: i32,
}
impl Data {
    fn text(&self, h: u32) -> &str {
        match h {
            1..=19 => {
                if (h - 1) % 3 == 1 {
                    "A long book title with enough words to wrap across the filename row"
                } else {
                    "Book"
                }
            }
            20 => ".epub",
            21 => "An author name",
            22 => "Group heading",
            _ => panic!("resource"),
        }
    }
}
impl Resources for Data {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, i: u16) -> Result<Row, Error> {
        Ok(Row {
            label: u32::from(i) + 1,
            value: if self.kind == 0 { 20 } else { 0 },
            subtitle: if self.kind == 1 { 21 } else { 0 },
            heading: if self.kind > 0 && i % 3 == 0 { 22 } else { 0 },
            icon: 100,
            icon_size: 32,
            reserved: 0,
        })
    }
    fn width(&mut self, h: u32, f: i32, _: u8) -> Result<i32, Error> {
        Ok(self.text(h).len() as i32 * if f == 0 { 8 } else { 10 })
    }
    fn height(&mut self, h: u32, f: i32, _: u8, n: u8, w: i16) -> Result<i32, Error> {
        let unit = if f == 0 { 8 } else { 10 };
        let mut lines = 1;
        let mut width = 0;
        for word in self.text(h).split_whitespace() {
            let next = word.len() as i32 * unit;
            let space = if width > 0 { unit } else { 0 };
            if width + space + next > i32::from(w) && width > 0 {
                lines += 1;
                width = next;
            } else {
                width += space + next;
            }
        }
        Ok(lines.min(i32::from(n)) * self.line(f)?)
    }
    fn line(&mut self, f: i32) -> Result<i32, Error> {
        Ok(if f == 0 { 18 } else { 24 })
    }
}
fn color(p: u8) -> u8 {
    match p {
        0 => 4,
        1 => 1,
        2 => 2,
        _ => panic!("paint"),
    }
}
fn main() {
    for width in [480, 528, 792, 800] {
        for selected in [-1, 0, 3, 18] {
            for kind in [0, 1, 2] {
                for selection in [0, 1, 3] {
                    println!("list {width} {selected} {kind} {selection}");
                    let mut data = Data { kind };
                    let state = ListState::new(selected);
                    let mut commands = [Command::default(); 128];
                    let mut hits = [Interaction::default(); 24];
                    let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                    let style = Style {
                        area: Rect {
                            x: 0,
                            y: 0,
                            w: width,
                            h: 400,
                        },
                        screen: Rect {
                            x: 0,
                            y: 0,
                            w: width,
                            h: 400,
                        },
                        min_touch: 44,
                        font: if kind == 0 { 0 } else { 1 },
                        row_height: if kind == 0 { 26 } else { 32 },
                        row_gap: 6,
                        inset: 5,
                        padding: 8,
                        scroll_width: 3,
                        radius: 12,
                        selection,
                        scroll_left: 0,
                        bold: 0,
                    };
                    let mut content = rows::Content::new(&mut data, 19, 24, kind == 0, 0, 0, 4);
                    let next =
                        list::view_rows(&mut arena, state, style, 19, 1, &mut content).unwrap();
                    let scene = arena.finish(Refresh::Fast, 1).unwrap();
                    for c in scene.commands {
                        let r = c.rect;
                        match Kind::from_byte(c.kind).unwrap() {
                            Kind::Fill => println!(
                                "fill {} {} {} {} {} {} {} 15",
                                r.x,
                                r.y,
                                r.w,
                                r.h,
                                if c.paint == 2 { 2 } else { 1 },
                                color(c.paint),
                                c.radius
                            ),
                            Kind::Bitmap => println!(
                                "bitmap {} {} {} {} 32 32 2 {} 0",
                                r.x,
                                r.y,
                                r.w,
                                r.h,
                                color(c.paint)
                            ),
                            Kind::TextHandle => {
                                println!(
                                    "text {} {} {} {} {} {} {} {} {} 0 {}",
                                    r.x,
                                    r.y,
                                    r.w,
                                    r.h,
                                    c.font,
                                    c.align,
                                    color(c.paint),
                                    c.max_lines,
                                    c.style,
                                    data.text(c.resource)
                                );
                                let w = data.width(c.resource, c.font, c.style).unwrap() as i16;
                                if w <= r.w {
                                    let x = r.x
                                        + match c.align {
                                            1 => (r.w - w) / 2,
                                            2 => r.w - w,
                                            _ => 0,
                                        };
                                    let y = r.y
                                        + ((r.h - data.line(c.font).unwrap() as i16) / 2).max(0);
                                    println!("origin {x} {y}");
                                }
                            }
                            Kind::Clip => {
                                print!("clip {} {} {} {}", r.x, r.y, r.w.min(528 - r.x), r.h)
                            }
                            Kind::Pop => print!("clip 0 0 528 792"),
                            Kind::Triangle => println!(
                                "triangle {} {} {} {} {} {} {}",
                                r.x,
                                r.y,
                                r.x,
                                r.y + r.h,
                                r.x + r.w,
                                r.y + r.h / 2,
                                color(c.paint)
                            ),
                            _ => panic!("command {}", c.kind),
                        }
                    }
                    println!(
                        "nav {} {} {} {}",
                        next.selected,
                        next.top,
                        next.drawn,
                        next.page_rows()
                    );
                    for h in scene.interactions {
                        let r = h.rect;
                        println!(
                            "hit {} {} {} {} {} {}",
                            r.x, r.y, r.w, r.h, h.action, h.value
                        );
                    }
                }
            }
        }
    }
}
