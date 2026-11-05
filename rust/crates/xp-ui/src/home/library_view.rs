use super::{
    library,
    rows::{Content, Resources},
    tabs,
};
use crate::{
    list,
    model::ListState,
    reader_chrome::list::{text, Screen, Theme},
    scene::{Arena, Command, Error, Kind, Rect},
};
pub const HEADER: u32 = 0x8000_0210;
pub const FOOTER: u32 = 0x8000_0211;
pub const POSITION: u32 = 0x8000_0212;
pub const EMPTY: u32 = 0x8000_0213;
pub const NO_RESULTS: u32 = 0x8000_0214;
pub const UNAVAILABLE: u32 = 0x8000_0215;
pub const HELP: u32 = 0x8000_0220;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub theme: Theme,
    pub tabs: tabs::Layout,
    pub content: Rect,
    pub hints_height: i16,
    pub corner: i16,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Feedback {
    pub list: ListState,
    pub body: Rect,
    pub back_home: u8,
    pub confirm: u8,
    pub search: u8,
    pub window_capacity: u8,
}
fn cv(n: i32) -> Result<i16, Error> {
    i16::try_from(n).map_err(|_| Error::Invalid)
}
#[inline(never)]
fn page(a: &mut Arena<'_>, r: Rect, h: u32, style: u8) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Page as u8,
        rect: r,
        resource: h,
        generation: 1,
        style,
        ..Command::default()
    })
}
pub fn view(
    a: &mut Arena<'_>,
    s: &library::State,
    f: library::Facts,
    l: &Layout,
    t: &mut dyn Resources,
) -> Result<Feedback, Error> {
    l.theme.validate()?;
    let theme = &l.theme;
    let nav = *s.nav();
    let mut out = Feedback {
        list: nav,
        back_home: u8::from(s.tabs_focused() && s.collapsed == 0 && s.queried == 0),
        confirm: if s.tabs_focused() {
            0
        } else if s.collapsed != 0 {
            1
        } else {
            2
        },
        search: u8::from(s.tabs_focused() && s.degraded == 0),
        ..Feedback::default()
    };
    a.push(Command {
        kind: Kind::Clear as u8,
        ..Command::default()
    })?;
    let mask = if theme.touch != 0 { 1 } else { 0 }
        | if s.degraded == 0 {
            2 | if theme.touch != 0 { 4 } else { 0 }
        } else {
            0
        };
    page(
        a,
        Rect {
            x: 0,
            y: theme.top_padding,
            w: theme.screen.w,
            h: theme.header_height,
        },
        HEADER,
        mask,
    )?;
    let top =
        (i32::from(theme.top_padding) + i32::from(theme.header_height)).max(i32::from(l.content.y));
    let line = t.line(theme.help_font)?;
    let bottom =
        i32::from(l.content.y) + i32::from(l.content.h) - i32::from(l.hints_height) - line - 4;
    let mut area = Rect {
        x: l.content.x,
        y: cv(top)?,
        w: l.content.w,
        h: cv((bottom - top).max(0))?,
    };
    if s.degraded == 0 {
        area = tabs::view(
            a,
            area,
            l.tabs,
            s.active_tab,
            s.tabs_focused(),
            s.sort & 1 != 0,
            t,
        )?;
    }
    out.body = area;
    if f.books == 0 {
        let line = cv(t.line(theme.small_font)?)?;
        text(
            a,
            Rect {
                y: area.y + (area.h - line) / 2,
                h: line,
                ..area
            },
            if f.filter_failed != 0 {
                UNAVAILABLE
            } else if s.queried == 0 {
                EMPTY
            } else {
                NO_RESULTS
            },
            1,
            theme.small_font,
            0,
            0,
            1,
            1,
        )?;
    } else {
        let mut style = theme.style(Screen::Chapters, area, &mut super::file_view::Adapter(t))?;
        style.row_gap = style.row_gap.max(6);
        let capacity = (((i32::from(area.h) + i32::from(style.row_gap))
            / (i32::from(style.row_height) + i32::from(style.row_gap)))
        .max(1)
            + 1)
        .min(24) as u16;
        out.window_capacity = capacity as u8;
        let reveal_width = cv(t.line(theme.body_font)? * 2 + 8 + 2 * i32::from(theme.space_sm))?;
        let mut rows = Content::new(
            t,
            f.count,
            capacity,
            false,
            theme.small_font,
            theme.small_font,
            if theme.touch != 0 { 8 } else { 4 },
        );
        rows.revealed = s.revealed;
        rows.reveal_width = reveal_width;
        let mut row_nav = nav;
        row_nav.selected -= 1;
        out.list = list::view_rows(a, row_nav, style, f.count, 1, &mut rows)?;
        out.list.selected += 1;
    }
    let footer_y =
        i32::from(l.content.y) + i32::from(l.content.h) - i32::from(l.hints_height) - line;
    if f.count > 0 {
        t.position(s.entry() + 1, f.count, s.collapsed != 0)?;
        let width = t.width(POSITION, theme.help_font, 0)?;
        let right = (i32::from(theme.screen.w) - i32::from(l.content.x) - i32::from(l.content.w))
            .max(i32::from(l.corner));
        text(
            a,
            Rect {
                x: cv(i32::from(theme.screen.w) - right - width - 12)?,
                y: cv(footer_y)?,
                w: 0,
                h: 0,
            },
            POSITION,
            1,
            theme.help_font,
            0,
            0,
            0,
            0,
        )?;
    }
    if theme.touch == 0 && s.collapsed == 0 {
        let help = if s.tabs_focused() && s.degraded == 0 {
            1
        } else if !s.tabs_focused() && s.sort < 2 && f.count > 0 {
            2
        } else if !s.tabs_focused() && s.delete_eligible(f.books) && f.count > 0 {
            3
        } else if !s.tabs_focused() && s.groupable(f.books) {
            4
        } else {
            0
        };
        if help != 0 {
            let left = i32::from(l.content.x.max(l.corner));
            page(
                a,
                Rect {
                    x: cv(left + 12)?,
                    y: cv(footer_y)?,
                    w: cv(i32::from(theme.screen.w) / 2 - left - 12)?,
                    h: cv(line)?,
                },
                HELP + help,
                0,
            )?;
        }
    }
    page(a, Rect::default(), FOOTER, 0)?;
    Ok(out)
}
