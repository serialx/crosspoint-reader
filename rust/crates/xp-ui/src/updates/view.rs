use super::{
    state::{cache, font, ota, sd, Kind as ScreenKind, State, CACHE, FONTS, OTA, SD},
    Feedback, Layout, Services,
};
use crate::{
    list,
    scene::{Arena, Command, Error, Kind, Rect, Refresh},
    settings::{
        settings_ffi::Services as Rows,
        view::{Content, Resources},
    },
};

pub const HEADER: u32 = 0x8000_0001;
pub const FOOTER: u32 = 0x8000_0002;
pub const TEXT_BASE: u32 = 0x4000_0000;
pub const CHECKING: u32 = TEXT_BASE + 1;
pub const UPDATING: u32 = TEXT_BASE + 2;
pub const NO_UPDATE: u32 = TEXT_BASE + 3;
pub const UPDATE_FAILED: u32 = TEXT_BASE + 4;
pub const UPDATE_COMPLETE: u32 = TEXT_BASE + 5;
pub const POWER_HINT: u32 = TEXT_BASE + 6;
pub const VALIDATING: u32 = TEXT_BASE + 7;
pub const POWER_WARNING: u32 = TEXT_BASE + 8;
pub const RESTART_HINT: u32 = TEXT_BASE + 9;
pub const RECOVERY_HINT: u32 = TEXT_BASE + 10;
pub const LOADING_FONTS: u32 = TEXT_BASE + 11;
pub const NO_FONTS: u32 = TEXT_BASE + 12;
pub const FONT_INSTALLED: u32 = TEXT_BASE + 13;
pub const FONT_FAILED: u32 = TEXT_BASE + 14;
pub const CLEARING: u32 = TEXT_BASE + 15;
pub const CLEARED: u32 = TEXT_BASE + 16;
pub const CLEAR_FAILED: u32 = TEXT_BASE + 17;
pub const CHECK_SERIAL: u32 = TEXT_BASE + 18;
pub const DETAIL: u32 = TEXT_BASE + 19;
pub const CURRENT_VERSION: u32 = TEXT_BASE + 20;
pub const NEW_VERSION: u32 = TEXT_BASE + 21;
pub const BYTES: u32 = TEXT_BASE + 22;
pub const FONT_STATUS: u32 = TEXT_BASE + 23;
pub const CACHE_COUNTS: u32 = TEXT_BASE + 24;
pub const PERCENT: u32 = TEXT_BASE + 25;
pub const POPUP_TITLE: u32 = TEXT_BASE + 26;
pub const POPUP_BODY: u32 = TEXT_BASE + 27;
pub const CANCEL: u32 = TEXT_BASE + 28;
pub const ACCEPT: u32 = TEXT_BASE + 29;

fn rect(x: i16, y: i16, w: i16, h: i16) -> Rect {
    Rect { x, y, w, h }
}
fn raw(arena: &mut Arena<'_>, kind: Kind, r: Rect, resource: u32) -> Result<(), Error> {
    arena.push(Command {
        kind: kind as u8,
        rect: r,
        resource,
        generation: 1,
        ..Command::default()
    })
}
struct View<'a, 'b> {
    arena: &'a mut Arena<'b>,
    layout: &'a Layout,
    services: &'a Services,
    rows: Rows,
}
impl View<'_, '_> {
    fn line(&mut self) -> Result<i16, Error> {
        let h = self.rows.line(self.layout.list.help_font)?;
        if !(1..=512).contains(&h) {
            return Err(Error::Invalid);
        }
        Ok(h as i16)
    }
    fn text(&mut self, handle: u32, x: Option<i16>, y: i16, bold: u8) -> Result<(), Error> {
        let font = self.layout.list.help_font;
        let text = self.services.text(handle)?;
        let width = self.services.width(text, font, bold)?;
        let x = x.unwrap_or(((i32::from(self.layout.list.list.screen.w) - width) / 2) as i16);
        self.arena.push(Command {
            kind: Kind::TextHandle as u8,
            rect: rect(x, y, 0, 0),
            resource: handle,
            generation: 1,
            font,
            style: bold,
            ..Command::default()
        })
    }
    fn wrapped(
        &mut self,
        handle: u32,
        r: Rect,
        font: i32,
        bold: u8,
        lines: u8,
        align: u8,
    ) -> Result<(), Error> {
        self.arena.push(Command {
            kind: Kind::TextHandle as u8,
            rect: r,
            resource: handle,
            generation: 1,
            font,
            style: bold,
            max_lines: lines,
            align,
            ..Command::default()
        })
    }
    fn footer(&mut self, kind: u8) -> Result<(), Error> {
        self.arena.push(Command {
            kind: Kind::Page as u8,
            rect: self.layout.list.list.screen,
            resource: FOOTER,
            generation: 1,
            font: i32::from(kind),
            ..Command::default()
        })
    }
    fn progress(&mut self, y: i16, percent: u32) -> Result<(), Error> {
        let l = &self.layout.list;
        let r = rect(
            l.side_padding,
            y,
            l.list.screen.w - 2 * l.side_padding,
            self.layout.progress_height,
        );
        self.arena.push(Command {
            kind: Kind::Page as u8,
            rect: r,
            resource: 0x8000_0003,
            generation: 1,
            font: percent as i32,
            ..Command::default()
        })?;
        self.arena.push(Command {
            kind: Kind::Stroke as u8,
            rect: r,
            width: 1,
            ..Command::default()
        })?;
        let fill = (i32::from(r.w) - 4).wrapping_mul(percent as i32) / 100;
        if fill > 0 {
            raw(
                self.arena,
                Kind::Fill,
                rect(
                    r.x + 2,
                    r.y + 2,
                    fill.min(i32::from(l.list.screen.w - r.x - 2)) as i16,
                    r.h - 4,
                ),
                0,
            )?;
        }
        self.text(PERCENT, None, r.y + r.h + 15, 0)
    }
    fn popup(&mut self, state: &State, feedback: &mut Feedback) -> Result<(), Error> {
        let l = *self.layout;
        let screen = l.list.list.screen;
        let font = l.list.list.font;
        let line = self.rows.line(font)?;
        if !(1..=512).contains(&line) {
            return Err(Error::Invalid);
        }
        let line = line as i16;
        let width = (screen.w * 3 / 4).min(screen.w - 2 * l.popup_side_margin);
        let inner = width - 2 * l.popup_padding;
        if inner <= 0 {
            return Err(Error::Invalid);
        }
        let title_h = self.services.height(POPUP_TITLE, font, inner, 1, 2)?;
        let body_h = if state.kind == ScreenKind::Cache as u8 {
            self.services.height(POPUP_BODY, font, inner, 0, 6)?
        } else {
            0
        };
        let button_h = line + 2 * l.selection_padding;
        let height = (2 * l.popup_padding
            + title_h
            + l.popup_gap
            + body_h
            + l.popup_gap
            + 2 * button_h
            + l.popup_gap)
            .min(screen.h);
        let card = rect(
            (screen.w - width) / 2,
            (screen.h - height) / 2,
            width,
            height,
        );
        feedback.popup = card;
        self.arena.push(Command {
            rect: card,
            paint: 1,
            radius: l.popup_radius,
            ..Command::default()
        })?;
        if l.popup_border > 0 {
            self.arena.push(Command {
                kind: Kind::Stroke as u8,
                rect: card,
                width: l.popup_border,
                radius: l.popup_radius,
                ..Command::default()
            })?;
        }
        let x = card.x + l.popup_padding;
        let y = card.y + l.popup_padding;
        self.wrapped(POPUP_TITLE, rect(x, y, inner, 1), font, 1, 2, 1)?;
        if body_h > 0 {
            self.wrapped(
                POPUP_BODY,
                rect(x, y + title_h + l.popup_gap, inner, 1),
                font,
                0,
                6,
                1,
            )?;
        }
        let top = card.y + card.h - l.popup_padding - 2 * button_h - l.popup_gap;
        for (index, handle) in [CANCEL, ACCEPT].into_iter().enumerate() {
            let r = rect(
                x,
                top + index as i16 * (button_h + l.popup_gap),
                inner,
                button_h,
            );
            feedback.options[index] = rect(r.x, r.y - (44 - r.h).max(0) / 2, r.w, r.h.max(44));
            let selected = state.popup_selected == index as u8;
            let invert = selected && l.list.list.selection == 0 && l.list.list.radius > 0;
            crate::settings::view::button(
                self.arena,
                r,
                handle,
                font,
                if invert {
                    0
                } else if selected {
                    2
                } else {
                    1
                },
                if l.list.list.selection == 0 {
                    l.list.list.radius
                } else {
                    0
                },
                u8::from(invert),
            )?;
        }
        Ok(())
    }
    fn fonts(&mut self, state: &State, feedback: &mut Feedback) -> Result<(), Error> {
        let l = self.layout.list;
        let mut style = l.list;
        let top = (l.top_padding + l.header_height).max(style.area.y) + l.spacing;
        let bottom = (style.screen.h - l.hints_height).min(style.area.y + style.area.h);
        style.area = rect(style.area.x, top, style.area.w, (bottom - top).max(0));
        let line = self.rows.line(style.font)?;
        if !(1..=512).contains(&line) {
            return Err(Error::Invalid);
        }
        let line = line as i16;
        let pad = if l.touch != 0 { 8 } else { 4 };
        style.min_touch = (line + 14).max(44);
        style.row_height = style.row_height.max(line + 2 * pad);
        if l.touch != 0 {
            style.row_height = style.row_height.max(style.min_touch).max(56);
            style.row_gap = style.row_gap.max(6);
        }
        let count = self.rows.count;
        if count == 0 && state.phase == font::FAMILIES {
            let height = self.rows.line(style.font)? as i16;
            self.wrapped(
                NO_FONTS,
                rect(style.area.x, style.area.y, style.area.w, height),
                style.font,
                l.list.bold,
                1,
                1,
            )?;
        } else {
            let mut content = Content::new(&mut self.rows, count, l.small_font, pad, 1, 8);
            feedback.list =
                list::view_rows(self.arena, *state.nav(), style, count, 1, &mut content)?;
        }
        self.footer(if state.phase == font::GROUPS { 3 } else { 4 })
    }
}
pub fn view(
    arena: &mut Arena<'_>,
    state: &State,
    layout: &Layout,
    services: &Services,
    feedback: &mut Feedback,
) -> Result<Refresh, Error> {
    let l = layout.list;
    let screen = l.list.screen;
    if !(1..=4096).contains(&screen.w)
        || !(1..=4096).contains(&screen.h)
        || screen.x != 0
        || screen.y != 0
        || l.list.area.x < 0
        || l.list.area.y < 0
        || l.list.area.w <= 0
        || l.list.area.h <= 0
        || i32::from(l.list.area.x) + i32::from(l.list.area.w) > i32::from(screen.w)
        || i32::from(l.list.area.y) + i32::from(l.list.area.h) > i32::from(screen.h)
        || !state.valid()
        || [
            l.top_padding,
            l.header_height,
            l.hints_height,
            l.spacing,
            l.side_padding,
            layout.progress_height,
            layout.popup_padding,
            layout.popup_gap,
            layout.popup_side_margin,
            layout.selection_padding,
        ]
        .iter()
        .any(|v| !(0..=1024).contains(v))
        || l.side_padding * 2 >= screen.w
        || layout.progress_height < 4
    {
        return Err(Error::Invalid);
    }
    arena.push(Command {
        kind: Kind::Clear as u8,
        rect: screen,
        paint: 1,
        ..Command::default()
    })?;
    raw(
        arena,
        Kind::Page,
        rect(0, l.top_padding, screen.w, l.header_height),
        HEADER,
    )?;
    let mut v = View {
        arena,
        layout,
        services,
        rows: services.rows,
    };
    let line = v.line()?;
    let top = (screen.h - line) / 2;
    feedback.last_percent = state.last_percent;
    let progress = state.percent();
    if state.kind == OTA
        && state.phase == ota::PROGRESS
        && (if state.total == 0 {
            0
        } else {
            (state.processed as f32 / state.total as f32 * 50.0) as i32 as u32
        }) == state.last_percent / 2
        || state.kind == SD && state.phase == sd::PROGRESS && progress == state.last_percent
    {
        return Ok(Refresh::None);
    }
    match (state.kind, state.phase) {
        (OTA, ota::CHECKING) => v.text(CHECKING, None, top, 0)?,
        (OTA, ota::CONFIRM) => {
            v.text(CURRENT_VERSION, Some(l.side_padding), screen.h / 6, 0)?;
            v.text(
                NEW_VERSION,
                Some(l.side_padding),
                screen.h / 6 + line + l.spacing,
                0,
            )?;
            if state.popup_active != 0 {
                v.footer(2)?;
                v.popup(state, feedback)?;
            }
        }
        (OTA, ota::PROGRESS) | (SD, sd::PROGRESS) => {
            feedback.last_percent = progress;
            v.text(UPDATING, None, top, u8::from(state.kind == SD))?;
            let y = top + line + l.spacing;
            v.progress(y, progress)?;
            v.text(
                if state.kind == OTA {
                    BYTES
                } else {
                    POWER_WARNING
                },
                None,
                y + layout.progress_height + l.spacing + line + l.spacing,
                0,
            )?;
        }
        (OTA, ota::NO_UPDATE) => {
            v.text(NO_UPDATE, None, top, 1)?;
            v.footer(0)?;
        }
        (OTA, ota::FAILED) | (SD, sd::FAILED) => {
            v.text(UPDATE_FAILED, None, top, 1)?;
            if services.present(DETAIL)? {
                v.text(DETAIL, None, top + line + l.spacing, 0)?;
            }
            v.footer(0)?;
        }
        (OTA, ota::COMPLETE) | (SD, sd::COMPLETE) => {
            v.text(UPDATE_COMPLETE, None, top, 1)?;
            v.wrapped(
                if state.kind == OTA {
                    POWER_HINT
                } else {
                    RESTART_HINT
                },
                rect(
                    l.side_padding,
                    top + line + l.spacing,
                    screen.w - 2 * l.side_padding,
                    1,
                ),
                l.help_font,
                0,
                3,
                1,
            )?;
        }
        (SD, sd::VALIDATING) => v.text(VALIDATING, None, top, 0)?,
        (SD, sd::PICKER | sd::CONFIRM) if state.recovery != 0 => {
            v.text(RECOVERY_HINT, None, top, 0)?
        }
        (FONTS, font::MANIFEST) => v.text(LOADING_FONTS, None, top, 0)?,
        (FONTS, font::GROUPS | font::FAMILIES) => v.fonts(state, feedback)?,
        (FONTS, font::PROGRESS) => {
            v.text(FONT_STATUS, None, top - line, 0)?;
            v.progress(top + l.spacing, progress)?;
            v.footer(1)?;
        }
        (FONTS, font::COMPLETE) => {
            v.text(FONT_INSTALLED, None, top, 1)?;
            v.footer(0)?;
        }
        (FONTS, font::FAILED) => {
            v.text(FONT_FAILED, None, top - line, 1)?;
            if services.present(DETAIL)? {
                v.text(DETAIL, None, top + l.spacing, 0)?;
            }
            v.footer(5)?;
        }
        (CACHE, cache::WARNING) => {
            if state.popup_active != 0 {
                v.footer(2)?;
                v.popup(state, feedback)?;
            } else {
                return Ok(Refresh::None);
            }
        }
        (CACHE, cache::CLEARING) => v.text(CLEARING, None, screen.h / 2, 0)?,
        (CACHE, cache::COMPLETE) => {
            v.text(CLEARED, None, screen.h / 2 - 20, 1)?;
            v.text(CACHE_COUNTS, None, screen.h / 2 + 10, 0)?;
            v.footer(0)?;
        }
        (CACHE, cache::FAILED) => {
            v.text(CLEAR_FAILED, None, screen.h / 2 - 20, 1)?;
            v.text(CHECK_SERIAL, None, screen.h / 2 + 10, 0)?;
            v.footer(0)?;
        }
        _ => {}
    }
    feedback.rendered = 1;
    Ok(Refresh::Fast)
}
