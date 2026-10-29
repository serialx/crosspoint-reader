//! Cover-grid geometry and arithmetic hit routing; bitmap storage stays in the binding.
use super::{view, HomeState};
use crate::scene::{Arena, Command, Error, Kind, Rect};

pub const PROGRESS: u32 = 0x8000_0013;
pub const GRID_COVER: u32 = 0x6000;
pub const GRID_ICON: u32 = 0x7000;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub safe: Rect,
    pub body_font: i32,
    pub small_font: i32,
    pub title_font: i32,
    pub progress: i32,
    pub space_sm: i16,
    pub space_lg: i16,
    pub header_side: i16,
    pub header_top: i16,
    pub header_height: i16,
    pub corner: i16,
    pub tab_height: i16,
    pub tab_height_pct: i16,
    pub tab_width_pct: i16,
    pub tab_offset: i16,
    pub touch: u8,
    pub body_bold: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Geometry {
    pub screen: Rect,
    pub hero: Rect,
    pub grid: Rect,
    pub tabs: Rect,
    pub cell_width: i16,
    pub row_height: i16,
    pub gap: i16,
    pub tab_width: i16,
    pub thumb_height: i16,
    pub min_touch: i16,
    pub books: u8,
    pub tab_count: u8,
    pub grid_distributed: u8,
    pub tab_distributed: u8,
}
pub trait Texts {
    fn width(&mut self, handle: u32, font: i32) -> Result<i16, Error>;
    fn line(&mut self, font: i32) -> Result<i16, Error>;
    fn height(&mut self, handle: u32, font: i32, width: i16, lines: u8) -> Result<i16, Error>;
    fn empty(&mut self, handle: u32) -> bool;
}
#[inline(never)]
fn r(x: i32, y: i32, w: i32, h: i32) -> Result<Rect, Error> {
    let cv = |v| i16::try_from(v).map_err(|_| Error::Invalid);
    Ok(Rect {
        x: cv(x)?,
        y: cv(y)?,
        w: cv(w)?,
        h: cv(h)?,
    })
}
#[inline(never)]
fn inset(a: Rect, t: i32, right: i32, b: i32, left: i32) -> Result<Rect, Error> {
    r(
        i32::from(a.x) + left,
        i32::from(a.y) + t,
        (i32::from(a.w) - left - right).max(0),
        (i32::from(a.h) - t - b).max(0),
    )
}
#[inline(never)]
fn fill(a: &mut Arena<'_>, rect: Rect, paint: u8) -> Result<(), Error> {
    a.push(Command {
        rect,
        paint,
        corners: 15,
        ..Command::default()
    })
}
#[inline(never)]
fn page(a: &mut Arena<'_>, rect: Rect, resource: u32) -> Result<(), Error> {
    a.push(Command {
        rect,
        resource,
        generation: 1,
        kind: Kind::Page as u8,
        ..Command::default()
    })
}
#[inline(never)]
fn text(
    a: &mut Arena<'_>,
    rect: Rect,
    resource: u32,
    font: i32,
    max_lines: u8,
    style: u8,
    align: u8,
) -> Result<(), Error> {
    a.push(Command {
        rect,
        resource,
        font,
        max_lines,
        style,
        align,
        generation: 1,
        kind: Kind::TextHandle as u8,
        ..Command::default()
    })
}
fn cover(a: &mut Arena<'_>, rect: Rect, index: u32) -> Result<(), Error> {
    fill(
        a,
        r(
            i32::from(rect.x) + i32::from(rect.w),
            i32::from(rect.y) + 2,
            2,
            i32::from(rect.h),
        )?,
        0,
    )?;
    fill(
        a,
        r(
            i32::from(rect.x) + 2,
            i32::from(rect.y) + i32::from(rect.h),
            i32::from(rect.w),
            2,
        )?,
        0,
    )?;
    page(a, rect, GRID_COVER + index)
}
impl Geometry {
    pub fn count(&self) -> u8 {
        self.books.saturating_add(self.tab_count)
    }
    pub fn valid(&self) -> bool {
        self.books <= 7
            && self.tab_count <= 6
            && self.screen.w > 0
            && self.screen.h > 0
            && self.cell_width >= 0
            && self.row_height >= 0
            && self.tab_width >= 0
            && self.gap >= 0
            && self.min_touch >= 0
            && self.grid_distributed <= 1
            && self.tab_distributed <= 1
            && (self.tab_distributed == 0 || self.tab_count > 1)
    }
    pub fn visual(&self, index: u8) -> Option<Rect> {
        if !self.valid() || index >= self.count() {
            return None;
        }
        if index == 0 && self.books != 0 {
            return Some(self.hero);
        }
        if index < self.books {
            let i = i32::from(index - 1);
            let col = i % 3;
            let row = i / 3;
            let width = i32::from(self.cell_width);
            let x = if self.grid_distributed != 0 {
                col * (i32::from(self.grid.w) - width) / 2
            } else {
                col * (width + i32::from(self.gap))
            };
            return r(
                i32::from(self.grid.x) + x,
                i32::from(self.grid.y) + row * (i32::from(self.row_height) + i32::from(self.gap)),
                width,
                i32::from(self.row_height),
            )
            .ok();
        }
        let i = i32::from(index - self.books);
        let n = i32::from(self.tab_count);
        let width = i32::from(self.tab_width);
        let x = if self.tab_distributed != 0 {
            i * (i32::from(self.tabs.w) - width) / (n - 1)
        } else {
            i * width
        };
        let width = if self.tab_distributed == 0 && i == n - 1 {
            i32::from(self.tabs.w) - x
        } else {
            width
        };
        r(
            i32::from(self.tabs.x) + x,
            i32::from(self.tabs.y),
            width,
            i32::from(self.tabs.h),
        )
        .ok()
    }
    pub fn hit_rect(&self, index: u8) -> Option<Rect> {
        let rect = self.visual(index)?;
        let min = if index >= self.books {
            44
        } else {
            i32::from(self.min_touch)
        };
        let mut w = i32::from(rect.w).max(min);
        let mut h = i32::from(rect.h).max(min);
        let mut x = i32::from(rect.x) - (w - i32::from(rect.w)) / 2;
        let mut y = i32::from(rect.y) - (h - i32::from(rect.h)) / 2;
        let left = i32::from(self.screen.x);
        let top = i32::from(self.screen.y);
        let right = left + i32::from(self.screen.w);
        let bottom = top + i32::from(self.screen.h);
        x = x.max(left);
        y = y.max(top);
        if x + w > right {
            x = right - w;
        }
        if y + h > bottom {
            y = bottom - h;
        }
        if x - left < 12 {
            w += x - left;
            x = left;
        }
        if y - top < 12 {
            h += y - top;
            y = top;
        }
        if right - x - w < 12 {
            w = right - x;
        }
        if bottom - y - h < 12 {
            h = bottom - y;
        }
        r(x, y, w, h).ok()
    }

    pub fn hit(&self, x: i16, y: i16) -> i32 {
        (0..self.count())
            .rev()
            .find(|i| self.hit_rect(*i).is_some_and(|r| r.contains(x, y)))
            .map_or(-1, i32::from)
    }
}
pub fn view(
    a: &mut Arena<'_>,
    s: HomeState,
    l: &Layout,
    t: &mut dyn Texts,
) -> Result<Geometry, Error> {
    if s.books > 7
        || l.screen.w <= 0
        || l.screen.h <= 0
        || l.space_sm < 0
        || l.space_lg < 0
        || l.tab_height_pct < 0
        || l.tab_width_pct < 0
    {
        return Err(Error::Invalid);
    }
    let sw = i32::from(l.screen.w);
    let sh = i32::from(l.screen.h);
    let sm = i32::from(l.space_sm);
    let lg = i32::from(l.space_lg);
    let left = i32::from(l.safe.x.max(l.corner));
    let right = (sw - i32::from(l.safe.x) - i32::from(l.safe.w)).max(i32::from(l.corner));
    let hi = (i32::from(l.header_side) - 6 - left).max(0);
    let mut body = inset(
        l.screen,
        i32::from(l.safe.y),
        right,
        sh - i32::from(l.safe.y) - i32::from(l.safe.h),
        left,
    )?;
    body = inset(body, if l.touch != 0 { lg } else { sm + 4 }, hi, sm, hi)?;
    body = inset(body, i32::from(l.header_height) + sm, 0, 0, 0)?;
    let th = i32::from(l.tab_height).min(i32::from(body.h)).max(0);
    let tabs = r(
        i32::from(body.x),
        i32::from(body.y) + i32::from(body.h) - th,
        i32::from(body.w),
        th,
    )?;
    body.h = (i32::from(body.h) - th - if l.touch != 0 { sm } else { 4 }).max(0) as i16;
    let adjusted = th * i32::from(l.tab_height_pct) / 100;
    let mut tabs = r(
        i32::from(tabs.x),
        i32::from(tabs.y) + th - adjusted + i32::from(l.tab_offset),
        i32::from(tabs.w),
        adjusted,
    )?;
    let mut g = Geometry {
        screen: l.screen,
        books: s.books,
        tab_count: if s.opds != 0 || s.plugins != 0 { 5 } else { 4 },
        min_touch: 44,
        ..Geometry::default()
    };
    if s.books != 0 {
        let row_gap = (i32::from(body.w) / 100).max(4);
        let row_h = ((i32::from(body.h) - sm - 2 * row_gap) / 3 + 8).max(1);
        let title_lines = if sw > sh { 1 } else { 2 };
        let feature_h = i32::from(body.h).min(row_h.max(
            i32::from(t.line(l.body_font)?) * title_lines
                + i32::from(t.line(l.small_font)?) * 2
                + 32,
        ));
        let hero = r(
            i32::from(body.x),
            i32::from(body.y),
            i32::from(body.w),
            feature_h,
        )?;
        body = inset(body, feature_h + sm, 0, 0, 0)?;
        let cover_h = (feature_h.min(row_h) - 12)
            .min((i32::from(hero.w) / 3 - 12) * 3 / 2)
            .max(1);
        let cover_w = (cover_h * 2 / 3).max(1);
        g.hero = hero;
        g.grid = r(
            i32::from(body.x),
            i32::from(body.y),
            i32::from(body.w),
            2 * (cover_h + 12) + row_gap,
        )?;
        g.row_height = (cover_h + 12) as i16;
        g.gap = row_gap as i16;
        g.thumb_height = (cover_h + 8) as i16;
        let natural = cover_w + 12;
        g.grid_distributed = u8::from(3 * natural + 2 * row_gap <= i32::from(g.grid.w));
        g.cell_width = if g.grid_distributed != 0 {
            natural
        } else {
            (i32::from(g.grid.w) - 2 * row_gap) / 3
        } as i16;
        fill(a, hero, 1)?;
        let content = inset(hero, 6, 6, 6, 6)?;
        let cw = cover_w.min(i32::from(content.w));
        let ch = cover_h.min(i32::from(content.h));
        let c = r(
            i32::from(content.x),
            i32::from(content.y) + (i32::from(content.h) - ch) / 2,
            cw,
            ch,
        )?;
        cover(a, c, 0)?;
        let tx = i32::from(c.x) + cw + lg + sm;
        let tw = i32::from(hero.x) + i32::from(hero.w) - 6 - tx;
        let label_h = if l.progress >= 0 {
            i32::from(t.line(l.small_font)?)
        } else {
            0
        };
        let ph = label_h.max(6);
        let py = i32::from(c.y) + ch - ph;
        let bottom = py - 8;
        let title_h =
            i32::from(t.height(view::TITLE, l.body_font, tw as i16, title_lines as u8)?);
        let author_h = if t.empty(view::AUTHOR) {
            0
        } else {
            i32::from(t.line(l.small_font)?)
        };
        let block = title_h
            + if author_h > 0 {
                author_h + if title_h > 0 { 4 } else { 0 }
            } else {
                0
            };
        let y = (i32::from(c.y) + (ch - block) / 2)
            .min(bottom - block)
            .max(i32::from(content.y));
        text(
            a,
            r(tx, y, tw, title_h)?,
            view::TITLE,
            l.body_font,
            title_lines as u8,
            l.body_bold,
            0,
        )?;
        let ay = y + title_h + 4;
        if author_h > 0 && ay < bottom {
            text(
                a,
                r(tx, ay, tw, author_h)?,
                view::AUTHOR,
                l.small_font,
                1,
                0,
                0,
            )?;
        }
        if l.progress >= 0 {
            let lw = i32::from(t.width(PROGRESS, l.small_font)?).min(tw / 2);
            let (bx, bw) = if tw > 0 {
                text(
                    a,
                    r(tx, py + (ph - label_h) / 2, lw, label_h)?,
                    PROGRESS,
                    l.small_font,
                    1,
                    0,
                    0,
                )?;
                (tx + lw + 8, tw - lw - 8)
            } else {
                (tx, tw)
            };
            if bw > 0 {
                let bar = r(bx, py + (ph - 6) / 2, bw, 6)?;
                fill(a, bar, 2)?;
                let w = bw * l.progress.clamp(0, 100) / 100;
                if w > 0 {
                    fill(a, r(bx, i32::from(bar.y), w, 6)?, 0)?;
                }
            }
        }
        if s.selected == 0 && l.touch == 0 {
            fill(
                a,
                r(
                    i32::from(hero.x) - 5,
                    i32::from(hero.y) + (feature_h - cover_h) / 2,
                    3,
                    cover_h,
                )?,
                2,
            )?;
        }
        for i in 1..s.books {
            let cell = g.visual(i).ok_or(Error::Invalid)?;
            fill(a, cell, 1)?;
            let content = inset(cell, 6, 6, 6, 6)?;
            let content = if content.w <= 0 || content.h <= 0 {
                cell
            } else {
                content
            };
            let c = r(
                i32::from(content.x) + (i32::from(content.w) - cover_w) / 2,
                i32::from(content.y),
                cover_w,
                cover_h,
            )?;
            cover(a, c, u32::from(i))?;
            if s.selected == i32::from(i) {
                a.push(Command {
                    kind: Kind::Stroke as u8,
                    rect: r(
                        i32::from(c.x) - 6,
                        i32::from(c.y) - 6,
                        cover_w + 12,
                        cover_h + 12,
                    )?,
                    paint: 2,
                    width: 8,
                    corners: 15,
                    ..Command::default()
                })?;
            }
        }
        tabs.x = g.grid.x + 6;
        tabs.w = g.grid.w - 12;
    } else {
        tabs = inset(tabs, 0, 6, 0, 6)?;
    }
    let width = i32::from(tabs.w) * i32::from(l.tab_width_pct) / 100;
    tabs.x += (i32::from(tabs.w) - width) as i16 / 2;
    tabs.w = width as i16;
    g.tabs = tabs;
    let natural = (i32::from(tabs.h) - 12).max(40) + 8;
    g.tab_distributed = u8::from(natural * i32::from(g.tab_count) <= i32::from(tabs.w));
    g.tab_width = if g.tab_distributed != 0 {
        natural
    } else {
        i32::from(tabs.w) / i32::from(g.tab_count)
    } as i16;
    for i in 0..g.tab_count {
        let slot = g.visual(s.books + i).ok_or(Error::Invalid)?;
        let mut pill = inset(slot, 4, 4, 8, 4)?;
        let wanted = i32::from(pill.h).max(40);
        if wanted < i32::from(pill.w) {
            pill.x += ((i32::from(pill.w) - wanted) / 2) as i16;
            pill.w = wanted as i16;
        }
        if pill.w <= 0 || pill.h <= 0 {
            pill = slot;
        }
        if s.selected == i32::from(s.books + i) {
            fill(
                a,
                r(
                    i32::from(pill.x),
                    i32::from(pill.y) + i32::from(pill.h) - 2,
                    i32::from(pill.w),
                    2,
                )?,
                0,
            )?;
        }
        let content = inset(pill, 4, 4, 4, 4)?;
        let content = if content.w <= 0 || content.h <= 0 {
            pill
        } else {
            content
        };
        let icon = if g.tab_count == 4 && i >= 2 { i + 1 } else { i };
        page(
            a,
            r(
                i32::from(content.x) + (i32::from(content.w) - 32) / 2,
                i32::from(content.y) + (i32::from(content.h) - 32) / 2,
                32,
                32,
            )?,
            GRID_ICON + u32::from(icon),
        )?;
    }
    if s.books == 0 {
        let title_h = i32::from(t.line(l.title_font)?);
        let message_h = i32::from(t.line(l.body_font)?);
        let height = 32 + lg + title_h + sm + message_h;
        let y = i32::from(body.y) + ((i32::from(body.h) - height) / 2).max(0);
        page(
            a,
            r(i32::from(body.x) + (i32::from(body.w) - 32) / 2, y, 32, 32)?,
            GRID_ICON + 5,
        )?;
        text(
            a,
            r(i32::from(body.x), y + 32 + lg, i32::from(body.w), title_h)?,
            view::NO_OPEN_BOOK,
            l.title_font,
            1,
            1,
            1,
        )?;
        text(
            a,
            r(
                i32::from(body.x),
                y + 32 + lg + title_h + sm,
                i32::from(body.w),
                message_h,
            )?,
            view::START_READING,
            l.body_font,
            1,
            l.body_bold,
            1,
        )?;
    }
    page(
        a,
        r(0, i32::from(l.header_top), sw, i32::from(l.header_height))?,
        view::HEADER,
    )?;
    Ok(g)
}
