use super::{
    file,
    rows::{Content, Resources},
};
use crate::{
    list,
    model::ListState,
    reader_chrome::list::{text, Screen, Theme},
    scene::{Arena, Command, Error, Kind, Rect},
};
pub const HEADER_FOLDER: u32 = 0x8000_0100;
pub const HEADER_ROOT: u32 = 0x8000_0101;
pub const HEADER_PICKER: u32 = 0x8000_0102;
pub const FOOTER: u32 = 0x8000_0103;
pub const PATH: u32 = 0x8000_0104;
pub const EMPTY: u32 = 0x8000_0105;
pub const NO_BIN: u32 = 0x8000_0106;
pub const ELLIPSIS: u32 = 0x8000_0107;
pub const PATH_SUFFIX: u32 = 0x1000_0000;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub theme: Theme,
    pub hints_height: i16,
    pub side_padding: i16,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Feedback {
    pub list: ListState,
    pub path_start: u32,
    pub path_truncated: u8,
    pub back_home: u8,
    pub select_file: u8,
    pub has_rows: u8,
}
fn cv(n: i32) -> Result<i16, Error> {
    i16::try_from(n).map_err(|_| Error::Invalid)
}
#[inline(never)]
fn page(a: &mut Arena<'_>, rect: Rect, resource: u32) -> Result<(), Error> {
    a.push(Command {
        kind: Kind::Page as u8,
        rect,
        resource,
        generation: 1,
        ..Command::default()
    })
}
pub(super) struct Adapter<'a>(pub &'a mut dyn Resources);
impl crate::reader_chrome::list::Resources for Adapter<'_> {
    fn window(&mut self, f: u16, n: u16) -> Result<(), Error> {
        self.0.window(f, n)
    }
    fn row(&mut self, _: u16) -> Result<crate::reader_chrome::list::Row, Error> {
        Err(Error::Invalid)
    }
    fn width(&mut self, h: u32, f: i32, b: u8) -> Result<i32, Error> {
        self.0.width(h, f, b)
    }
    fn height(&mut self, h: u32, f: i32, b: u8, n: u8, w: i16) -> Result<i32, Error> {
        self.0.height(h, f, b, n, w)
    }
    fn line(&mut self, f: i32) -> Result<i32, Error> {
        self.0.line(f)
    }
}
pub fn view(
    a: &mut Arena<'_>,
    state: &file::State,
    f: file::Facts,
    l: &Layout,
    path: &[u8],
    resources: &mut dyn Resources,
) -> Result<Feedback, Error> {
    let t = &l.theme;
    t.validate()?;
    if l.hints_height < 0
        || l.side_padding < 0
        || l.side_padding > t.screen.w / 2
        || path.len() > 0x0fff_ffff
    {
        return Err(Error::Invalid);
    }
    let mut out = Feedback {
        list: state.list,
        back_home: u8::from(f.root != 0 && state.picker == 0),
        select_file: u8::from(
            state.picker != 0
                && f.count > 0
                && (0..i32::from(f.count)).contains(&state.list.selected)
                && f.directory == 0,
        ),
        has_rows: u8::from(f.count > 0),
        ..Feedback::default()
    };
    a.push(Command {
        kind: Kind::Clear as u8,
        ..Command::default()
    })?;
    page(
        a,
        Rect {
            x: 0,
            y: t.top_padding,
            w: t.screen.w,
            h: t.header_height,
        },
        if state.picker != 0 {
            HEADER_PICKER
        } else if f.root != 0 {
            HEADER_ROOT
        } else {
            HEADER_FOLDER
        },
    )?;
    let y = (i32::from(t.top_padding) + i32::from(t.header_height)).max(i32::from(t.safe.y))
        + i32::from(t.spacing);
    let bottom = (i32::from(t.screen.h) - i32::from(l.hints_height))
        .min(i32::from(t.safe.y) + i32::from(t.safe.h));
    let path_line = resources.line(t.help_font)?;
    let band_h = (path_line + i32::from(t.spacing)).min((bottom - y).max(0));
    let band_y = bottom - band_h;
    a.push(Command {
        rect: Rect {
            x: t.safe.x,
            y: cv(band_y)?,
            w: t.safe.w,
            h: 3,
        },
        ..Command::default()
    })?;
    let max_width = i32::from(t.safe.w) - 2 * i32::from(l.side_padding);
    if resources.width(PATH_SUFFIX, t.help_font, 0)? > max_width {
        out.path_truncated = 1;
        let available = max_width - resources.width(ELLIPSIS, t.help_font, 0)?;
        let mut offset = 0;
        while offset < path.len() {
            if resources.width(PATH_SUFFIX + offset as u32, t.help_font, 0)? <= available {
                break;
            }
            offset += 1;
            while offset < path.len() && path[offset] & 0xc0 == 0x80 {
                offset += 1;
            }
        }
        out.path_start = offset as u32;
    }
    let py =
        band_y + i32::from(t.spacing) / 2 + (band_h - i32::from(t.spacing) / 2 - path_line) / 2;
    text(
        a,
        Rect {
            x: t.safe.x + l.side_padding,
            y: cv(py)?,
            w: 0,
            h: 0,
        },
        PATH,
        1,
        t.help_font,
        0,
        0,
        0,
        0,
    )?;
    let area = Rect {
        x: t.safe.x,
        y: cv(y)?,
        w: t.safe.w,
        h: cv((band_y - y).max(0))?,
    };
    if f.count == 0 {
        let lh = cv(resources.line(t.body_font)?)?;
        text(
            a,
            Rect {
                y: area.y + (area.h - lh) / 2,
                h: lh,
                ..area
            },
            if state.picker != 0 { NO_BIN } else { EMPTY },
            1,
            t.body_font,
            0,
            t.title_bold,
            1,
            1,
        )?;
    } else {
        let style = t.style(Screen::Menu, area, &mut Adapter(resources))?;
        let reveal_width = cv(resources.line(t.body_font)? * 2 + 8 + 2 * i32::from(t.space_sm))?;
        let mut rows = Content::new(
            resources,
            f.count,
            24,
            true,
            t.small_font,
            t.small_font,
            if t.touch != 0 { 8 } else { 4 },
        );
        rows.revealed = state.revealed;
        rows.reveal_width = reveal_width;
        // The filename list has no per-file string cache: the binding resolves handles on demand.
        out.list = list::view_rows(a, state.list, style, f.count, 1, &mut rows)?;
    }
    page(a, Rect::default(), FOOTER)?;
    Ok(out)
}
