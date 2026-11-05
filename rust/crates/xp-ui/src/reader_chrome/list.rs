use crate::{
    list::{self, Style},
    model::ListState,
    scene::{Arena, Command, Error, Kind, Rect},
};

pub const HEADER: u32 = 0x8000_0001;
pub const FOOTER: u32 = 0x8000_0002;
pub const EMPTY: u32 = 0x8000_0003;
pub const HELP: u32 = 0x8000_0004;
pub const PROGRESS: u32 = 0x8000_0005;
pub const TITLE: u32 = 0x8000_0006;
pub const SUBTITLE: u32 = 0x8000_0007;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Screen {
    Chapters,
    Menu,
    Bookmarks,
    Clippings,
    End,
    Panel,
}
impl Screen {
    pub fn from_byte(v: u8) -> Option<Self> {
        Some(match v {
            0 => Self::Chapters,
            1 => Self::Menu,
            2 => Self::Bookmarks,
            3 => Self::Clippings,
            4 => Self::End,
            5 => Self::Panel,
            _ => return None,
        })
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Theme {
    pub screen: Rect,
    pub safe: Rect,
    pub body_font: i32,
    pub small_font: i32,
    pub title_font: i32,
    pub fixed_title_font: i32,
    pub help_font: i32,
    pub row_min: i16,
    pub row_gap: i16,
    pub row_inset: i16,
    pub row_padding: i16,
    pub scroll_width: i16,
    pub top_padding: i16,
    pub header_height: i16,
    pub spacing: i16,
    pub tab_height: i16,
    pub header_padding: i16,
    pub space_sm: i16,
    pub space_md: i16,
    pub space_lg: i16,
    pub row_radius: u8,
    pub selection: u8,
    pub scroll_left: u8,
    pub title_bold: u8,
    pub control_radius: u8,
    pub sheet_radius: u8,
    pub touch: u8,
    pub orientation: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Row {
    pub label: u32,
    pub subtitle: u32,
    pub value: u32,
    pub icon: u32,
    pub toggle: u8,
    pub checked: u8,
    pub reserved: [u8; 2],
}

pub trait Resources {
    fn window(&mut self, first: u16, requested: u16) -> Result<(), Error>;
    fn row(&mut self, index: u16) -> Result<Row, Error>;
    fn width(&mut self, handle: u32, font: i32, bold: u8) -> Result<i32, Error>;
    fn height(
        &mut self,
        handle: u32,
        font: i32,
        bold: u8,
        lines: u8,
        width: i16,
    ) -> Result<i32, Error>;
    fn line(&mut self, font: i32) -> Result<i32, Error>;
}

#[derive(Clone, Copy, Default)]
struct Measured {
    row: Row,
    label_width: i16,
    label_height: i16,
    subtitle_height: i16,
    value_width: i16,
    height: i16,
}

pub struct Content<'a> {
    pub resources: &'a mut dyn Resources,
    pub count: u16,
    pub capacity: u16,
    pub kind: Screen,
    pub small_font: i32,
    pub value_font: i32,
    pub padding_y: i16,
    pub revealed: i32,
    pub reveal_width: i16,
    first: Option<u16>,
    measured: Measured,
}
impl<'a> Content<'a> {
    pub fn new(
        resources: &'a mut dyn Resources,
        count: u16,
        capacity: u16,
        kind: Screen,
        small_font: i32,
        value_font: i32,
        padding_y: i16,
    ) -> Self {
        Self {
            resources,
            count,
            capacity,
            kind,
            small_font,
            value_font,
            padding_y,
            first: None,
            measured: Measured::default(),
            revealed: -1,
            reveal_width: 0,
        }
    }
    fn label_lines(&self) -> u8 {
        if self.kind == Screen::Menu {
            2
        } else {
            1
        }
    }
    fn value_inset(&self) -> i16 {
        if self.kind == Screen::Menu {
            8
        } else {
            0
        }
    }
}

impl Theme {
    pub fn validate(&self) -> Result<(), Error> {
        if self.screen.x != 0
            || self.screen.y != 0
            || !(1..=8192).contains(&self.screen.w)
            || !(1..=8192).contains(&self.screen.h)
            || self.safe.x < 0
            || self.safe.y < 0
            || self.safe.w <= 0
            || self.safe.h < 0
            || i32::from(self.safe.x) + i32::from(self.safe.w) > i32::from(self.screen.w)
            || i32::from(self.safe.y) + i32::from(self.safe.h) > i32::from(self.screen.h)
            || self.selection > 3
            || self.orientation > 3
            || self.title_bold > 1
            || [
                self.row_min,
                self.row_gap,
                self.row_inset,
                self.row_padding,
                self.scroll_width,
                self.top_padding,
                self.header_height,
                self.spacing,
                self.tab_height,
                self.header_padding,
                self.space_sm,
                self.space_md,
                self.space_lg,
            ]
            .into_iter()
            .any(|n| !(0..=8192).contains(&n))
        {
            return Err(Error::Invalid);
        }
        Ok(())
    }
    pub fn style(
        &self,
        kind: Screen,
        area: Rect,
        resources: &mut dyn Resources,
    ) -> Result<Style, Error> {
        let font = if kind == Screen::Menu {
            self.small_font
        } else {
            self.body_font
        };
        let line = resources.line(font)?;
        let body_line = resources.line(self.body_font)?;
        let padding = if self.touch != 0 { 8 } else { 4 };
        let min_touch = (body_line + 14).max(44);
        let mut row_height = (line + 2 * padding).max(i32::from(self.row_min));
        let mut gap = self.row_gap;
        if self.touch != 0 {
            row_height = row_height.max(min_touch).max(56);
            gap = gap.max(6);
        }
        Ok(Style {
            area,
            screen: self.screen,
            min_touch: i16_value(min_touch)?,
            font,
            row_height: i16_value(row_height)?,
            row_gap: gap,
            inset: self.row_inset,
            padding: self.row_padding,
            scroll_width: self.scroll_width,
            radius: self.row_radius,
            selection: self.selection,
            scroll_left: self.scroll_left,
            bold: if kind == Screen::Menu {
                0
            } else {
                self.title_bold
            },
        })
    }
}

fn take_top(area: &mut Rect, height: i16) -> Result<Rect, Error> {
    if height < 0 || height > area.h {
        return Err(Error::Invalid);
    }
    let band = Rect { h: height, ..*area };
    area.y += height;
    area.h -= height;
    Ok(band)
}

fn resource(arena: &mut Arena<'_>, rect: Rect, handle: u32, generation: u32) -> Result<(), Error> {
    arena.push(Command {
        kind: Kind::Page as u8,
        rect,
        resource: handle,
        generation,
        ..Command::default()
    })
}

#[allow(clippy::too_many_arguments)]
pub fn view(
    arena: &mut Arena<'_>,
    state: ListState,
    kind: Screen,
    theme: Theme,
    resources: &mut dyn Resources,
    count: u16,
    capacity: u16,
    generation: u32,
    revealed: i32,
) -> Result<ListState, Error> {
    theme.validate()?;
    let screen = theme.screen;
    if kind != Screen::End {
        arena.push(Command {
            kind: Kind::Clear as u8,
            rect: screen,
            paint: 1,
            ..Command::default()
        })?;
    }
    let mut area = theme.safe;
    if kind == Screen::End {
        if count == 0 {
            text(
                arena,
                Rect {
                    x: i16_value(
                        (i32::from(screen.w)
                            - resources.width(TITLE, theme.fixed_title_font, 1)?)
                            / 2,
                    )?,
                    y: screen.h * 3 / 8,
                    h: 0,
                    ..screen
                },
                TITLE,
                generation,
                theme.fixed_title_font,
                0,
                1,
                1,
                0,
            )?;
            return Ok(state);
        }
        let title_y = area.y + area.h / 8;
        let title_h = i16_value(resources.line(theme.fixed_title_font)?)?;
        let subtitle_h = i16_value(resources.line(theme.help_font)?)?;
        let subtitle_y =
            i16_value(i32::from(title_y) + i32::from(title_h) + i32::from(theme.spacing))?;
        text(
            arena,
            Rect {
                y: title_y,
                h: title_h,
                ..area
            },
            TITLE,
            generation,
            theme.fixed_title_font,
            0,
            1,
            1,
            1,
        )?;
        text(
            arena,
            Rect {
                y: subtitle_y,
                h: subtitle_h,
                ..area
            },
            SUBTITLE,
            generation,
            theme.help_font,
            0,
            0,
            1,
            1,
        )?;
        let top = i16_value(
            i32::from(subtitle_y) + i32::from(subtitle_h) + 2 * i32::from(theme.spacing)
                - i32::from(area.y),
        )?;
        take_top(&mut area, top)?;
        area.h = area.h.checked_sub(theme.spacing).ok_or(Error::Invalid)?;
    } else {
        let band = i16_value(i32::from(theme.top_padding) + i32::from(theme.header_height))?;
        take_top(&mut area, band)?;
        if kind == Screen::Bookmarks {
            let gutter = if theme.orientation == 1 || theme.orientation == 3 {
                40
            } else {
                0
            };
            let width = resources.width(TITLE, theme.fixed_title_font, 1)?;
            let x = (if theme.orientation == 1 { gutter } else { 0 })
                + (i32::from(screen.w) - gutter - width) / 2;
            text(
                arena,
                Rect {
                    x: i16_value(x)?,
                    y: 15 + if theme.orientation == 2 { 50 } else { 0 },
                    w: 0,
                    h: 0,
                },
                TITLE,
                generation,
                theme.fixed_title_font,
                0,
                1,
                0,
                0,
            )?;
        } else {
            let header = if kind == Screen::Clippings {
                Rect {
                    x: 0,
                    y: theme.top_padding,
                    w: screen.w,
                    h: theme.header_height,
                }
            } else {
                Rect {
                    x: theme.safe.x,
                    y: theme.safe.y + theme.top_padding,
                    w: theme.safe.w,
                    h: theme.header_height,
                }
            };
            resource(arena, header, HEADER, generation)?;
        }
        if kind == Screen::Menu {
            let band = take_top(&mut area, theme.tab_height)?;
            let pad = theme.header_padding;
            text(
                arena,
                Rect {
                    x: band.x + pad,
                    w: band.w - 2 * pad,
                    ..band
                },
                PROGRESS,
                generation,
                theme.small_font,
                0,
                0,
                0,
                1,
            )?;
        }
        take_top(&mut area, theme.spacing)?;
    }
    if count == 0 {
        text(
            arena,
            area,
            EMPTY,
            generation,
            theme.body_font,
            0,
            theme.title_bold,
            1,
            1,
        )?;
    } else {
        if matches!(kind, Screen::Bookmarks | Screen::Clippings) && theme.touch == 0 {
            let height = i16_value(resources.line(theme.help_font)?)?;
            area.h = area
                .h
                .checked_sub(height)
                .and_then(|h| h.checked_sub(theme.spacing))
                .ok_or(Error::Invalid)?;
            resource(
                arena,
                Rect {
                    y: area.y + area.h + theme.spacing,
                    h: height,
                    ..area
                },
                HELP,
                generation,
            )?;
        }
        let style = theme.style(kind, area, resources)?;
        let mut content = Content::new(
            resources,
            count,
            capacity,
            kind,
            theme.small_font,
            theme.small_font,
            if theme.touch != 0 { 8 } else { 4 },
        );
        content.revealed = revealed;
        content.reveal_width = i16_value(
            content.resources.line(theme.body_font)? * 2 + 8 + 2 * i32::from(theme.space_sm),
        )?;
        let next = list::view_rows(arena, state, style, count, generation, &mut content)?;
        resource(arena, screen, FOOTER, generation)?;
        return Ok(next);
    }
    resource(arena, screen, FOOTER, generation)?;
    Ok(state)
}

fn i16_value(value: i32) -> Result<i16, Error> {
    i16::try_from(value).map_err(|_| Error::Invalid)
}

#[allow(clippy::too_many_arguments)]
pub fn text(
    arena: &mut Arena<'_>,
    rect: Rect,
    resource: u32,
    generation: u32,
    font: i32,
    paint: u8,
    bold: u8,
    align: u8,
    lines: u8,
) -> Result<(), Error> {
    if resource == 0 {
        return Ok(());
    }
    arena.push(Command {
        kind: Kind::TextHandle as u8,
        rect,
        resource,
        generation,
        font,
        paint,
        style: bold,
        align,
        max_lines: lines,
        ..Command::default()
    })
}

impl list::Rows for Content<'_> {
    fn action(&self) -> u32 {
        if self.kind == Screen::Panel {
            6
        } else {
            1
        }
    }
    fn partial(&self) -> bool {
        self.kind != Screen::End && self.kind != Screen::Panel
    }
    fn nav_managed(&self) -> bool {
        self.kind != Screen::End
    }
    fn available(&self, index: u16) -> bool {
        self.first
            .is_none_or(|first| index < first.saturating_add(self.capacity))
    }
    fn height(&mut self, index: u16, width: i16, style: Style) -> Result<i16, Error> {
        if self.capacity == 0 {
            return Err(Error::Invalid);
        }
        if self.first.is_none() {
            self.resources
                .window(index, self.capacity.min(self.count - index))?;
            self.first = Some(index);
        }
        let row = self.resources.row(index)?;
        let line = self.resources.line(style.font)?;
        let icon = if row.icon != 0 { 32 } else { 0 };
        let content_width =
            i32::from(width) - 2 * i32::from(style.padding) - if icon != 0 { icon + 10 } else { 0 };
        let value_width = if row.toggle != 0 {
            28
        } else if row.value != 0 {
            self.resources.width(
                row.value,
                self.value_font,
                u8::from(self.kind == Screen::Panel),
            )?
        } else {
            0
        };
        let has_value = row.toggle != 0 || row.value != 0;
        let mut label_width = content_width
            - if has_value {
                value_width + i32::from(self.value_inset()) + 10
            } else {
                0
            };
        let lines = self.label_lines();
        if has_value
            && lines > 1
            && self.resources.width(row.label, style.font, style.bold)? > label_width
        {
            label_width = label_width.min(content_width * 3 / 5);
        }
        label_width = label_width.max(0);
        let label_height = if lines > 1 && label_width > 0 {
            self.resources
                .height(
                    row.label,
                    style.font,
                    style.bold,
                    lines,
                    i16_value(label_width)?,
                )?
                .max(line)
        } else {
            line
        };
        let subtitle_height = if row.subtitle != 0 {
            self.resources.line(self.small_font)?
        } else {
            0
        };
        let value_height = if row.toggle != 0 {
            28
        } else if row.value != 0 {
            self.resources.line(self.value_font)?
        } else {
            0
        };
        let band_height = label_height.max(value_height);
        let text_height = band_height + subtitle_height;
        let row_min = i32::from(style.row_height);
        let needed = if self.padding_y >= 0 {
            text_height.max(icon) + 2 * i32::from(self.padding_y)
        } else if row.subtitle != 0 {
            text_height + (row_min - line - subtitle_height).max(0)
        } else if label_height > row_min {
            row_min + label_height - line
        } else {
            row_min
        };
        let height = needed.max(row_min).max(text_height).max(icon);
        self.measured = Measured {
            row,
            label_width: i16_value(label_width)?,
            label_height: i16_value(band_height)?,
            subtitle_height: i16_value(subtitle_height)?,
            value_width: i16_value(value_width)?,
            height: i16_value(height)?,
        };
        Ok(self.measured.height)
    }
    fn overlay(
        &mut self,
        arena: &mut Arena<'_>,
        index: u16,
        rect: Rect,
        style: Style,
        generation: u32,
    ) -> Result<(), Error> {
        if self.revealed != i32::from(index) {
            return Ok(());
        }
        let width = self.reveal_width.min(rect.w / 2);
        let hit = Rect {
            x: rect.x + rect.w - width,
            w: width,
            ..rect
        };
        let inset = style.padding / 2;
        let button = Rect {
            x: hit.x + inset,
            y: hit.y + inset,
            w: hit.w - 2 * inset,
            h: hit.h - 2 * inset,
        };
        arena.hit(crate::scene::Interaction {
            rect: hit,
            action: 2,
            value: i32::from(index),
            generation,
            inputs: 1,
            flags: 0,
        })?;
        arena.push(Command {
            rect: button,
            paint: 1,
            radius: style.radius,
            ..Command::default()
        })?;
        arena.push(Command {
            rect: button,
            kind: Kind::Stroke as u8,
            width: 1,
            radius: style.radius,
            ..Command::default()
        })?;
        arena.push(Command {
            rect: Rect {
                x: button.x + (button.w - 24) / 2,
                y: button.y + (button.h - 24) / 2,
                w: 24,
                h: 24,
            },
            kind: Kind::Bitmap as u8,
            resource: 0x8000_0008,
            generation,
            ..Command::default()
        })
    }
    fn paint(
        &mut self,
        arena: &mut Arena<'_>,
        _: u16,
        rect: Rect,
        style: Style,
        fg: u8,
        generation: u32,
    ) -> Result<(), Error> {
        let m = self.measured;
        let row = m.row;
        let mut content = Rect {
            x: rect.x + style.padding,
            w: rect.w - 2 * style.padding,
            ..rect
        };
        let mut band = content;
        if row.subtitle != 0 {
            band.y += ((content.h - m.label_height - m.subtitle_height) / 2).max(0);
            band.h = m.label_height;
        }
        if row.icon != 0 {
            arena.push(Command {
                kind: Kind::Bitmap as u8,
                rect: Rect {
                    x: content.x,
                    y: content.y + (content.h - 32) / 2,
                    w: 32,
                    h: 32,
                },
                resource: row.icon,
                generation,
                paint: fg,
                ..Command::default()
            })?;
            content.x += 42;
            content.w -= 42;
            band.x = content.x;
            band.w = content.w;
        }
        if row.toggle != 0 {
            let box_rect = Rect {
                x: band.x + band.w - self.value_inset() - 25,
                y: band.y + (band.h - 22) / 2,
                w: 22,
                h: 22,
            };
            arena.push(Command {
                kind: Kind::Stroke as u8,
                rect: box_rect,
                paint: fg,
                width: 2,
                radius: 2,
                ..Command::default()
            })?;
            if row.checked != 0 {
                arena.push(Command {
                    rect: Rect {
                        x: box_rect.x + 5,
                        y: box_rect.y + 5,
                        w: 12,
                        h: 12,
                    },
                    paint: fg,
                    radius: 2,
                    ..Command::default()
                })?;
            }
        } else if row.value != 0 {
            text(
                arena,
                Rect {
                    x: i16_value(
                        i32::from(band.x) + i32::from(band.w)
                            - i32::from(m.value_width)
                            - i32::from(self.value_inset()),
                    )?,
                    w: m.value_width,
                    ..band
                },
                row.value,
                generation,
                self.value_font,
                fg,
                u8::from(self.kind == Screen::Panel),
                2,
                1,
            )?;
        }
        text(
            arena,
            Rect {
                w: m.label_width,
                ..band
            },
            row.label,
            generation,
            style.font,
            fg,
            style.bold,
            0,
            self.label_lines(),
        )?;
        if row.subtitle != 0 {
            text(
                arena,
                Rect {
                    x: content.x,
                    y: band.y + band.h,
                    w: content.w,
                    h: m.subtitle_height,
                },
                row.subtitle,
                generation,
                self.small_font,
                fg,
                0,
                0,
                1,
            )?;
        }
        Ok(())
    }
}
