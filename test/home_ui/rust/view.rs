#![allow(dead_code)]
use home::{HomeState, view::*};
use scene::{Arena, Command, Error, Rect, Refresh};
use xp_ui::home;
pub use xp_ui::{ffi, model, scene};

struct Texts {
    strings: Vec<String>,
}
impl Texts {
    fn get(&self, handle: u32) -> String {
        match handle {
            NO_OPEN_BOOK => "No open book".into(),
            START_READING => "Start reading".into(),
            CONTINUE => "Continue reading".into(),
            TITLE..=0x2002 => if handle == TITLE + 1 {
                "Short"
            } else {
                "A book title with enough words to occupy multiple lines"
            }
            .into(),
            AUTHOR..=0x3002 => if handle == AUTHOR + 1 {
                ""
            } else {
                "An author"
            }
            .into(),
            MENU..=0x4005 => format!("Menu {}", handle - MENU),
            _ => self.strings[(handle - 0x10000) as usize].clone(),
        }
    }
    fn store(&mut self, text: String) -> u32 {
        let id = 0x10000 + self.strings.len() as u32;
        self.strings.push(text);
        id
    }
}
impl home::view::Texts for Texts {
    fn width(&mut self, h: u32, f: i32, b: bool) -> Result<i16, Error> {
        Ok((self.get(h).len() as i32 * (f / 2 + i32::from(b))) as i16)
    }
    fn line(&mut self, f: i32) -> Result<i16, Error> {
        Ok((f * 2) as i16)
    }
    fn empty(&mut self, h: u32) -> bool {
        self.get(h).is_empty()
    }
    fn truncate(&mut self, h: u32, f: i32, w: i16, b: bool) -> Result<u32, Error> {
        let mut s = self.get(h);
        s.truncate(
            s.len()
                .min((i32::from(w).max(0) / (f / 2 + i32::from(b))) as usize),
        );
        Ok(self.store(s))
    }
    fn wrap(&mut self, h: u32, f: i32, w: i16, max: u8, b: bool) -> Result<Lines, Error> {
        let text = self.get(h);
        let chars = (i32::from(w) / (f / 2 + i32::from(b))).max(1) as usize;
        let mut lines = Lines {
            first: 0x10000 + self.strings.len() as u32,
            ..Lines::default()
        };
        for chunk in text.as_bytes().chunks(chars).take(usize::from(max)) {
            let id = self.store(String::from_utf8(chunk.to_vec()).unwrap());
            lines.count += 1;
            lines.max_width = lines.max_width.max(self.width(id, f, b)?);
        }
        Ok(lines)
    }
}
fn main() {
    for width in [480, 528, 792, 800] {
        for theme in 0..4 {
            for books in [0, 1, 3] {
                for selected in [0, 1, 3, 5] {
                    for status in 0..3 {
                        for snapshot in 0..3 {
                            println!("view {width} {theme} {books} {selected} {status} {snapshot}");
                            let mut state = HomeState::new(books, false, true, false, books != 0);
                            state.selected = selected;
                            let layout = Layout {
                                screen: Rect {
                                    x: 0,
                                    y: 0,
                                    w: width,
                                    h: 800,
                                },
                                content: Rect {
                                    x: 6,
                                    y: 0,
                                    w: width - 12,
                                    h: 800,
                                },
                                title_font: 12,
                                small_font: 10,
                                tiny_font: 8,
                                top_padding: 10,
                                header_height: 50,
                                home_top: 60,
                                tile_height: 250,
                                cover_height: 180,
                                side_padding: 20,
                                spacing: 10,
                                menu_top: 12,
                                menu_height: 42,
                                menu_gap: 6,
                                hints_height: 40,
                                scroll_width: 4,
                                scroll_offset: 6,
                                theme,
                                continue_in_menu: u8::from(theme == 3),
                                columns: 3,
                                ..Layout::default()
                            };
                            let prepared = Prepared {
                                covers: [Cover {
                                    width: 113,
                                    height: 270,
                                    status,
                                    ..Cover::default()
                                }; 3],
                                cover_width: 113,
                                rendered: u8::from(snapshot == 2),
                                restored: u8::from(snapshot == 2),
                                can_snapshot: u8::from(snapshot != 0),
                                reserved: 0,
                            };
                            let mut texts = Texts {
                                strings: Vec::new(),
                            };
                            let mut commands = [Command::default(); 96];
                            let mut arena = Arena::new(&mut commands, &mut [], &mut []).unwrap();
                            standard(&mut arena, state, &layout, &prepared, &mut texts).unwrap();
                            let scene = arena.finish(Refresh::Fast, 1).unwrap();
                            for c in scene.commands {
                                if c.kind == 10 {
                                    println!(
                                        "t {} {} {} {} {} {}",
                                        c.font,
                                        c.rect.x,
                                        c.rect.y,
                                        c.paint,
                                        c.style,
                                        texts.get(c.resource)
                                    );
                                } else {
                                    println!(
                                        "p {} {} {} {} {} {} {} {} {}",
                                        c.kind,
                                        c.rect.x,
                                        c.rect.y,
                                        c.rect.w,
                                        c.rect.h,
                                        c.paint,
                                        c.radius,
                                        c.corners,
                                        c.resource
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
