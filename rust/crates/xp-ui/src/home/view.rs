use super::{HomeState, HomeTouch};
use crate::scene::{Arena, Command, Error, Kind, Rect};

pub const HEADER: u32 = 0x8000_0001;
pub const FOOTER: u32 = 0x8000_0002;
pub const RESTORE: u32 = 0x8000_0003;
pub const SNAPSHOT: u32 = 0x8000_0004;
pub const MASK_CORNERS: u32 = 0x8000_0005;
pub const RIBBON: u32 = 0x8000_0006;
pub const PLACEHOLDER: u32 = 0x8000_0007;
pub const COVER_ICON: u32 = 0x8000_0008;
pub const NO_OPEN_BOOK: u32 = 0x8000_0010;
pub const START_READING: u32 = 0x8000_0011;
pub const CONTINUE: u32 = 0x8000_0012;
pub const COVER: u32 = 0x1000;
pub const TITLE: u32 = 0x2000;
pub const AUTHOR: u32 = 0x3000;
pub const MENU: u32 = 0x4000;
pub const MENU_ICON: u32 = 0x5000;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub content: Rect,
    pub safe: Rect,
    pub title_font: i32,
    pub small_font: i32,
    pub tiny_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub home_top: i16,
    pub tile_height: i16,
    pub cover_height: i16,
    pub side_padding: i16,
    pub spacing: i16,
    pub menu_top: i16,
    pub menu_height: i16,
    pub menu_gap: i16,
    pub hints_height: i16,
    pub scroll_width: i16,
    pub scroll_offset: i16,
    pub theme: u8,
    pub continue_in_menu: u8,
    pub columns: u8,
    pub reserved: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Cover {
    pub width: i32,
    pub height: i32,
    /// 0: empty/invalid; 1: nonempty path that could not open; 2: parsed.
    pub status: u8,
    pub reserved: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Prepared {
    pub covers: [Cover; 3],
    pub cover_width: i32,
    pub rendered: u8,
    pub restored: u8,
    pub can_snapshot: u8,
    pub reserved: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Widths {
    pub lyra: i32,
    pub rounded: i32,
}
impl Widths {
    pub fn prepare(&mut self, s: HomeState, l: Layout, p: &mut Prepared) {
        let width = match l.theme {
            1 => &mut self.lyra,
            3 => &mut self.rounded,
            _ => return,
        };
        if *width == 0 {
            *width = (f64::from(l.cover_height) * 0.6) as i32;
        }
        if s.books != 0 && p.rendered == 0 && p.covers[0].status == 2 {
            *width = p.covers[0].width;
        }
        p.cover_width = *width;
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Lines {
    pub first: u32,
    pub count: u8,
    pub reserved: u8,
    pub max_width: i16,
}

pub trait Texts {
    fn width(&mut self, handle: u32, font: i32, bold: bool) -> Result<i16, Error>;
    fn line(&mut self, font: i32) -> Result<i16, Error>;
    fn empty(&mut self, handle: u32) -> bool;
    fn wrap(
        &mut self,
        handle: u32,
        font: i32,
        width: i16,
        max: u8,
        bold: bool,
    ) -> Result<Lines, Error>;
    fn truncate(&mut self, handle: u32, font: i32, width: i16, bold: bool) -> Result<u32, Error>;
}

#[inline(never)]
fn rect(x: i32, y: i32, w: i32, h: i32) -> Result<Rect, Error> {
    Ok(Rect {
        x: cv(x)?,
        y: cv(y)?,
        w: cv(w)?,
        h: cv(h)?,
    })
}
fn cv(value: i32) -> Result<i16, Error> {
    i16::try_from(value).map_err(|_| Error::Invalid)
}
#[inline(never)]
fn fill(a: &mut Arena<'_>, r: Rect, paint: u8, radius: u8, corners: u8) -> Result<(), Error> {
    a.push(Command {
        rect: r,
        paint,
        radius,
        corners,
        ..Command::default()
    })
}
fn stroke(a: &mut Arena<'_>, r: Rect, paint: u8, radius: u8) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Stroke as u8,
        rect: r,
        paint,
        radius,
        width: 1,
        ..Command::default()
    })
}
fn resource(a: &mut Arena<'_>, r: Rect, id: u32, paint: u8) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Page as u8,
        rect: r,
        resource: id,
        generation: 1,
        paint,
        ..Command::default()
    })
}
#[inline(never)]
fn text(
    a: &mut Arena<'_>,
    r: Rect,
    id: u32,
    font: i32,
    bold: bool,
    paint: u8,
) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::TextHandle as u8,
        rect: r,
        resource: id,
        generation: 1,
        font,
        style: u8::from(bold),
        paint,
        ..Command::default()
    })
}
fn centered(
    a: &mut Arena<'_>,
    t: &mut dyn Texts,
    l: &Layout,
    id: u32,
    font: i32,
    y: i32,
    paint: u8,
) -> Result<(), Error> {
    let x = (i32::from(l.screen.w) - i32::from(t.width(id, font, false)?)) / 2;
    text(a, rect(x, y, 0, 0)?, id, font, false, paint)
}
#[allow(clippy::too_many_arguments)]
fn lines(
    a: &mut Arena<'_>,
    l: Lines,
    font: i32,
    bold: bool,
    paint: u8,
    x: i32,
    mut y: i32,
    line: i16,
    center: Option<(i16, &mut dyn Texts)>,
) -> Result<(), Error> {
    let mut center = center;
    for n in 0..u32::from(l.count) {
        let id = l.first + n;
        let x = if let Some((width, texts)) = center.as_mut() {
            (i32::from(*width) - i32::from(texts.width(id, font, bold)?)) / 2
        } else {
            x
        };
        text(a, rect(x, y, 0, 0)?, id, font, bold, paint)?;
        y += i32::from(line);
    }
    Ok(())
}

pub fn thumbnail_height(l: Layout) -> i32 {
    if l.theme == 2 {
        let width = (i32::from(l.screen.w) - 2 * i32::from(l.side_padding)) / 3;
        i32::from(l.cover_height).max((width - 16) * 5 / 3 + 2)
    } else {
        i32::from(l.cover_height)
    }
}

pub fn standard(
    a: &mut Arena<'_>,
    state: HomeState,
    l: &Layout,
    p: &Prepared,
    t: &mut dyn Texts,
) -> Result<HomeTouch, Error> {
    if !(0..=3).contains(&l.theme)
        || !(1..=8192).contains(&l.screen.w)
        || !(1..=8192).contains(&l.screen.h)
        || l.columns == 0
    {
        return Err(Error::Invalid);
    }
    a.push(Command {
        kind: Kind::Clear as u8,
        rect: l.screen,
        paint: 1,
        ..Command::default()
    })?;
    let cover = rect(
        i32::from(l.content.x),
        i32::from(l.home_top),
        i32::from(l.content.w),
        i32::from(l.tile_height),
    )?;
    if p.restored != 0 {
        resource(a, cover, RESTORE, 0)?;
    }
    resource(
        a,
        rect(
            0,
            i32::from(l.top_padding),
            i32::from(l.screen.w),
            i32::from(l.home_top - l.top_padding),
        )?,
        HEADER,
        0,
    )?;
    match l.theme {
        0 => classic(a, state, l, p, t, cover)?,
        1 => lyra(a, state, l, p, t, cover)?,
        2 => three(a, state, l, p, t, cover)?,
        3 => rounded(a, state, l, p, t, cover)?,
        _ => return Err(Error::Invalid),
    }
    menus(a, state, l, t)?;
    resource(a, l.screen, FOOTER, 0)?;
    Ok(HomeTouch {
        cover: rect(
            i32::from(l.content.x + l.side_padding),
            i32::from(l.home_top),
            i32::from(l.content.w - 2 * l.side_padding) / i32::from(l.columns),
            i32::from(l.tile_height),
        )?,
        menu: rect(
            0,
            i32::from(l.home_top + l.tile_height + l.menu_top),
            i32::from(i16::MAX),
            if l.theme == 3 {
                i32::from(t.line(l.title_font)?) + 20
            } else {
                i32::from(l.menu_height)
            },
        )?,
        menu_step: cv(if l.theme == 3 {
            i32::from(t.line(l.title_font)?) + 20 + i32::from(l.menu_gap)
        } else {
            i32::from(l.menu_height + l.menu_gap)
        })?,
        columns: l.columns,
        continue_in_menu: l.continue_in_menu,
    })
}

fn empty_lyra(a: &mut Arena<'_>, l: &Layout, t: &mut dyn Texts, r: Rect) -> Result<(), Error> {
    text(
        a,
        rect(
            i32::from(r.x) + 48,
            i32::from(r.y) + i32::from(r.h) / 2 - i32::from(t.line(l.title_font)?) - 2,
            0,
            0,
        )?,
        NO_OPEN_BOOK,
        l.title_font,
        true,
        0,
    )?;
    text(
        a,
        rect(
            i32::from(r.x) + 48,
            i32::from(r.y) + i32::from(r.h) / 2 + 2,
            0,
            0,
        )?,
        START_READING,
        l.small_font,
        false,
        0,
    )
}
fn lyra(
    a: &mut Arena<'_>,
    s: HomeState,
    l: &Layout,
    p: &Prepared,
    t: &mut dyn Texts,
    r: Rect,
) -> Result<(), Error> {
    if s.books == 0 {
        return empty_lyra(a, l, t, r);
    }
    let x = i32::from(r.x + l.side_padding);
    let y = i32::from(r.y);
    let w = i32::from(r.w - 2 * l.side_padding);
    let h = i32::from(l.cover_height);
    let cw = p.cover_width;
    let image = rect(x + 8, y + 8, cw, h)?;
    if p.rendered == 0 {
        if p.covers[0].status == 2 {
            resource(a, image, COVER, 0)?;
        }
        stroke(a, image, 0, 0)?;
        if p.covers[0].status == 0 {
            resource(a, image, PLACEHOLDER, 0)?;
        }
        if p.can_snapshot != 0 {
            resource(a, r, SNAPSHOT, 0)?;
        }
    }
    if s.selected == 0 {
        fill(a, rect(x, y, w, 8)?, 2, 6, 3)?;
        fill(a, rect(x, y + 8, 8, h)?, 2, 0, 0)?;
        fill(a, rect(x + 8 + cw, y + 8, w - 8 - cw, h)?, 2, 0, 0)?;
        fill(a, rect(x, y + h + 8, w, 8)?, 2, 6, 12)?;
    }
    let tw = cv(w - 16 - i32::from(l.spacing) - cw)?;
    let title = t.wrap(TITLE, l.title_font, tw, 3, true)?;
    let author = t.truncate(AUTHOR, l.small_font, tw, false)?;
    let title_line = t.line(l.title_font)?;
    let small_line = t.line(l.small_font)?;
    let author_height = if t.empty(AUTHOR) {
        0
    } else {
        i32::from(small_line) * 3 / 2
    };
    let block = i32::from(title_line) * i32::from(title.count) + author_height;
    let ty = y + i32::from(r.h) / 2 - block / 2;
    let tx = x + 8 + cw + i32::from(l.spacing);
    lines(a, title, l.title_font, true, 0, tx, ty, title_line, None)?;
    if author_height != 0 {
        text(
            a,
            rect(
                tx,
                ty + i32::from(title_line) * i32::from(title.count) + i32::from(small_line) / 2,
                0,
                0,
            )?,
            author,
            l.small_font,
            false,
            0,
        )?;
    }
    Ok(())
}
fn three(
    a: &mut Arena<'_>,
    s: HomeState,
    l: &Layout,
    p: &Prepared,
    t: &mut dyn Texts,
    r: Rect,
) -> Result<(), Error> {
    if s.books == 0 {
        return empty_lyra(a, l, t, r);
    }
    let width = i32::from(r.w - 2 * l.side_padding) / 3;
    let count = s.books.min(l.columns).min(3);
    let y = i32::from(r.y);
    let h = i32::from(l.cover_height);
    if p.rendered == 0 {
        for i in 0..count {
            let x = i32::from(r.x + l.side_padding) + width * i32::from(i);
            let image = rect(x + 8, y + 8, width - 16, h)?;
            let status = p.covers[usize::from(i)].status;
            if status == 2 {
                resource(a, image, COVER + u32::from(i), 0)?;
            }
            stroke(a, image, 0, 0)?;
            if status == 0 {
                fill(
                    a,
                    rect(x + 8, y + 8 + h / 3, width - 16, 2 * h / 3)?,
                    0,
                    0,
                    0,
                )?;
                resource(a, rect(x + 32, y + 32, 32, 32)?, COVER_ICON, 0)?;
            }
        }
        if p.can_snapshot != 0 {
            resource(a, r, SNAPSHOT, 0)?;
        }
    }
    let line = t.line(l.tiny_font)?;
    for i in 0..count {
        let x = i32::from(r.x + l.side_padding) + width * i32::from(i);
        let title = t.wrap(TITLE + u32::from(i), l.tiny_font, cv(width - 16)?, 3, false)?;
        if s.selected == i32::from(i) {
            fill(a, rect(x, y, width, 8)?, 2, 6, 3)?;
            fill(a, rect(x, y + 8, 8, h)?, 2, 0, 0)?;
            fill(a, rect(x + width - 8, y + 8, 8, h)?, 2, 0, 0)?;
            fill(
                a,
                rect(
                    x,
                    y + h + 8,
                    width,
                    i32::from(title.count) * i32::from(line) + 13,
                )?,
                2,
                6,
                12,
            )?;
        }
        lines(
            a,
            title,
            l.tiny_font,
            false,
            0,
            x + 8,
            y + h + 13,
            line,
            None,
        )?;
    }
    Ok(())
}
fn rounded(
    a: &mut Arena<'_>,
    s: HomeState,
    l: &Layout,
    p: &Prepared,
    t: &mut dyn Texts,
    r: Rect,
) -> Result<(), Error> {
    let x = i32::from(r.x + l.side_padding);
    let y = i32::from(r.y);
    let w = i32::from(r.w - 2 * l.side_padding);
    let h = i32::from(r.h);
    if s.books == 0 {
        fill(a, rect(x, y, w, h)?, 2, 20, 0)?;
        let line = i32::from(t.line(l.title_font)?);
        return centered(a, t, l, NO_OPEN_BOOK, l.title_font, y + h / 2 - line / 2, 0);
    }
    let ch = i32::from(l.cover_height);
    let cw = p.cover_width;
    let image = rect(x + (w - cw) / 2, y + (h - ch) / 2, cw, ch)?;
    if p.rendered == 0 {
        if p.covers[0].status == 2 {
            resource(a, image, COVER, 0)?;
            resource(a, image, MASK_CORNERS, 2)?;
        }
        stroke(a, image, 0, 18)?;
        if p.covers[0].status == 0 {
            fill(
                a,
                rect(
                    i32::from(image.x),
                    i32::from(image.y) + ch / 3,
                    cw,
                    2 * ch / 3,
                )?,
                0,
                0,
                0,
            )?;
            resource(
                a,
                rect(i32::from(image.x) + 24, i32::from(image.y) + 24, 32, 32)?,
                COVER_ICON,
                0,
            )?;
            resource(a, image, MASK_CORNERS, 2)?;
        }
        if p.can_snapshot != 0 {
            resource(a, r, SNAPSHOT, 0)?;
        }
    }
    fill(a, rect(x, y, w, i32::from(image.y) - y)?, 2, 20, 3)?;
    fill(a, rect(x, i32::from(image.y), (w - cw) / 2, ch)?, 2, 0, 0)?;
    fill(
        a,
        rect(x + (w + cw) / 2, i32::from(image.y), (w - cw) / 2, ch)?,
        2,
        0,
        0,
    )?;
    fill(
        a,
        rect(
            x,
            i32::from(image.y) + ch,
            w,
            h - (i32::from(image.y) - y + ch),
        )?,
        2,
        20,
        12,
    )
}

fn classic(
    a: &mut Arena<'_>,
    s: HomeState,
    l: &Layout,
    p: &Prepared,
    t: &mut dyn Texts,
    r: Rect,
) -> Result<(), Error> {
    let selected = s.books != 0 && s.selected == 0;
    let h = i32::from(r.h);
    let c = p.covers[0];
    let w = if s.books != 0 && c.status == 2 && c.width > 0 && c.height > 0 {
        ((h as f32 * (c.width as f32 / c.height as f32)) as i32).min((f32::from(r.w) * 0.9) as i32)
    } else {
        i32::from(r.w) / 2
    };
    let x = i32::from(r.x) + (i32::from(r.w) - w) / 2;
    let y = i32::from(r.y);
    let card = rect(x, y, w, h)?;
    let new_image = s.books != 0 && c.status == 2 && p.rendered == 0;
    let rendered = p.rendered != 0 || new_image && p.can_snapshot != 0;
    if new_image {
        resource(a, card, COVER, 0)?;
        stroke(a, card, 0, 0)?;
        if p.can_snapshot != 0 {
            resource(a, r, SNAPSHOT, 0)?;
        }
        if selected {
            stroke(a, rect(x + 1, y + 1, w - 2, h - 2)?, 0, 0)?;
            stroke(a, rect(x + 2, y + 2, w - 4, h - 4)?, 0, 0)?;
        }
    }
    if p.restored == 0 && !rendered {
        if selected {
            fill(a, card, 0, 0, 0)?;
        } else {
            stroke(a, card, 0, 0)?;
        }
        if s.books != 0 {
            resource(
                a,
                rect(x + w - w / 8 - 10, y + 5, w / 8, h / 5)?,
                RIBBON,
                u8::from(selected),
            )?;
        }
    }
    if p.restored != 0 && selected && rendered {
        stroke(a, rect(x + 1, y + 1, w - 2, h - 2)?, 0, 0)?;
        stroke(a, rect(x + 2, y + 2, w - 4, h - 4)?, 0, 0)?;
    }
    let title_line = t.line(l.title_font)?;
    let small_line = t.line(l.small_font)?;
    if s.books == 0 {
        let ty = y + (h - i32::from(title_line) - i32::from(small_line)) / 2;
        centered(a, t, l, NO_OPEN_BOOK, l.title_font, ty, 0)?;
        return centered(
            a,
            t,
            l,
            START_READING,
            l.small_font,
            ty + i32::from(title_line),
            0,
        );
    }
    let title = t.wrap(TITLE, l.title_font, cv(w - 40)?, 3, false)?;
    let author = if t.empty(AUTHOR) {
        0
    } else {
        t.truncate(AUTHOR, l.small_font, cv(w - 40)?, false)?
    };
    let total = i32::from(title_line) * i32::from(title.count)
        + if author != 0 {
            i32::from(small_line) * 3 / 2
        } else {
            0
        };
    let ty = y + (h - total) / 2;
    if rendered {
        let max_width = i32::from(title.max_width).max(if author != 0 {
            i32::from(t.width(author, l.small_font, false)?)
        } else {
            0
        });
        let box_width = max_width + 16;
        let box_rect = rect(
            i32::from(r.x) + (i32::from(r.w) - box_width) / 2,
            ty - 8,
            box_width,
            total + 16,
        )?;
        fill(a, box_rect, u8::from(!selected), 0, 0)?;
        stroke(a, box_rect, u8::from(selected), 0)?;
    }
    lines(
        a,
        title,
        l.title_font,
        false,
        u8::from(selected),
        0,
        ty,
        title_line,
        Some((l.screen.w, t)),
    )?;
    if author != 0 {
        centered(
            a,
            t,
            l,
            author,
            l.small_font,
            ty + i32::from(title_line) * i32::from(title.count) + i32::from(small_line) / 2,
            u8::from(selected),
        )?;
    }
    let cy = y + h - i32::from(small_line) * 3 / 2;
    if rendered {
        let bw = i32::from(t.width(CONTINUE, l.small_font, false)?) + 12;
        let box_rect = rect(
            i32::from(r.x) + (i32::from(r.w) - bw) / 2,
            cy - 3,
            bw,
            i32::from(small_line) + 6,
        )?;
        fill(a, box_rect, u8::from(!selected), 0, 0)?;
        stroke(a, box_rect, u8::from(selected), 0)?;
    }
    centered(a, t, l, CONTINUE, l.small_font, cy, u8::from(selected))
}

fn menus(a: &mut Arena<'_>, s: HomeState, l: &Layout, t: &mut dyn Texts) -> Result<(), Error> {
    let extra = l.continue_in_menu != 0 && s.books != 0;
    let count = 4 + i32::from(s.opds != 0 || s.plugins != 0) + i32::from(extra);
    let selected = if l.continue_in_menu != 0 {
        s.selected
    } else {
        s.selected - i32::from(s.books)
    };
    let y = i32::from(l.home_top + l.tile_height + l.menu_top);
    let h = i32::from(l.screen.h)
        - i32::from(l.header_height + l.home_top + l.spacing + l.menu_top + l.hints_height);
    let x = i32::from(l.content.x + l.side_padding);
    let w = i32::from(l.content.w - 2 * l.side_padding);
    let line = t.line(if l.theme == 0 {
        l.small_font
    } else {
        l.title_font
    })?;
    let rh = if l.theme == 3 {
        i32::from(line) + 20
    } else {
        i32::from(l.menu_height)
    };
    let step = rh
        + if l.theme == 3 {
            6
        } else {
            i32::from(l.menu_gap)
        };
    if step <= 0 {
        return Err(Error::Invalid);
    }
    let page = if l.theme == 3 {
        (h / step).max(1)
    } else {
        count
    };
    let start = if l.theme == 3 {
        selected.max(0) / page * page
    } else {
        0
    };
    for i in start..count.min(start + page) {
        let id = MENU + i as u32;
        let sy = y
            + (i - start) * step
            + if l.theme == 0 {
                i32::from(l.spacing)
            } else {
                0
            };
        let picked = selected == i;
        if l.theme == 0 {
            let r = rect(x, sy, w, rh)?;
            if picked {
                fill(a, r, 0, 0, 0)?;
            } else {
                stroke(a, r, 0, 0)?;
            }
            let tx = i32::from(l.content.x)
                + (i32::from(l.content.w) - i32::from(t.width(id, l.small_font, false)?)) / 2;
            text(
                a,
                rect(tx, sy + (rh - i32::from(line)) / 2, 0, 0)?,
                id,
                l.small_font,
                false,
                u8::from(picked),
            )?;
        } else if l.theme == 3 {
            let label = t.truncate(id, l.title_font, cv((w - 40).max(0))?, true)?;
            let width = w.min(i32::from(t.width(label, l.title_font, true)?) + 40);
            fill(a, rect(x, sy, width, rh)?, u8::from(!picked), 30, 0)?;
            text(
                a,
                rect(x + 20, sy + (rh - i32::from(line)) / 2, 0, 0)?,
                label,
                l.title_font,
                true,
                u8::from(picked),
            )?;
        } else {
            if picked {
                fill(a, rect(x, sy, w, rh)?, 2, 6, 0)?;
            }
            let ty = sy + (rh - i32::from(line)) / 2;
            resource(a, rect(x + 16, ty, 32, 32)?, MENU_ICON + i as u32, 0)?;
            text(a, rect(x + 58, ty, 0, 0)?, id, l.title_font, false, 0)?;
        }
    }
    if l.theme == 3 && count > page {
        let th = (h * page / count).max(10);
        let max_start = (count - page).max(1);
        let travel = (h - th).max(1);
        fill(
            a,
            rect(
                i32::from(l.content.x + l.content.w - l.scroll_offset - l.scroll_width),
                y + start.clamp(0, max_start) * travel / max_start,
                i32::from(l.scroll_width),
                th,
            )?,
            0,
            0,
            0,
        )?;
    }
    Ok(())
}
