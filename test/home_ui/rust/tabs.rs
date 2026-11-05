use xp_ui::{
    home::{
        rows::{Resources, Row},
        tabs,
    },
    scene::{Arena, Command, Error, Interaction, Kind, Rect, Refresh},
};
struct Data;
impl Data {
    fn text(&self, h: u32) -> &str {
        match h - tabs::LABEL {
            0 => "Recent",
            1 => "Title",
            2 => "Author",
            _ => panic!("handle"),
        }
    }
}
impl Resources for Data {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, _: u16) -> Result<Row, Error> {
        Err(Error::Invalid)
    }
    fn width(&mut self, h: u32, f: i32, _: u8) -> Result<i32, Error> {
        Ok(self.text(h).len() as i32 * if f == 0 { 8 } else { 10 })
    }
    fn height(&mut self, _: u32, f: i32, _: u8, _: u8, _: i16) -> Result<i32, Error> {
        self.line(f)
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
        3 => 3,
        _ => panic!("paint"),
    }
}
fn main() {
    for width in [480, 528, 792, 800] {
        for touch in [0, 1] {
            for theme in [0, 1, 2] {
                for selected in [0, 1] {
                    for active in [0, 1, 2] {
                        for descending in [0, 1] {
                            println!(
                                "tabs {width} {touch} {theme} {selected} {active} {descending}"
                            );
                            let screen = Rect {
                                x: 0,
                                y: 0,
                                w: width,
                                h: 792,
                            };
                            let layout = tabs::Layout {
                                screen,
                                safe: screen,
                                body_font: 1,
                                small_font: 0,
                                height: if theme == 1 { 48 } else { 50 },
                                gap: if theme == 1 { 8 } else { 10 },
                                side: 20,
                                divider: if theme == 1 { 3 } else { 0 },
                                spacing: if theme == 1 { 16 } else { 10 },
                                full: u8::from(theme == 2),
                                touch,
                                radius: if theme == 2 {
                                    20
                                } else if theme == 1 {
                                    6
                                } else {
                                    0
                                },
                                bold: u8::from(theme == 2),
                                reserved: [0; 2],
                            };
                            let mut commands = [Command::default(); 32];
                            let mut hits = [Interaction::default(); 3];
                            let mut a = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                            let mut data = Data;
                            let body = tabs::view(
                                &mut a,
                                Rect {
                                    x: 0,
                                    y: 94,
                                    w: width,
                                    h: 658,
                                },
                                layout,
                                active,
                                selected == 0,
                                descending != 0,
                                &mut data,
                            )
                            .unwrap();
                            let scene = a.finish(Refresh::Fast, 1).unwrap();
                            for c in scene.commands {
                                let r = c.rect;
                                match Kind::from_byte(c.kind).unwrap() {
                                    Kind::Fill => println!(
                                        "fill {} {} {} {} {} {} {} 15",
                                        r.x,
                                        r.y,
                                        r.w,
                                        r.h,
                                        if c.paint >= 2 { 2 } else { 1 },
                                        color(c.paint),
                                        c.radius
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
                                        let w =
                                            data.width(c.resource, c.font, c.style).unwrap() as i16;
                                        if w <= r.w {
                                            let x = r.x + (r.w - w) / 2;
                                            let y = r.y
                                                + ((r.h - data.line(c.font).unwrap() as i16) / 2)
                                                    .max(0);
                                            println!("origin {x} {y}");
                                        }
                                    }
                                    Kind::Page => {
                                        if c.resource == 0x80000203 {
                                            println!(
                                                "triangle {} {} {} {} {} {} {}",
                                                r.x + 3,
                                                r.y,
                                                r.x,
                                                r.y + 6,
                                                r.x + 6,
                                                r.y + 6,
                                                color(c.paint)
                                            );
                                        } else {
                                            println!(
                                                "triangle {} {} {} {} {} {} {}",
                                                r.x,
                                                r.y,
                                                r.x + 6,
                                                r.y,
                                                r.x + 3,
                                                r.y + 6,
                                                color(c.paint)
                                            );
                                        }
                                    }
                                    _ => panic!("command"),
                                }
                            }
                            println!("body {} {} {} {}", body.x, body.y, body.w, body.h);
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
    }
}
