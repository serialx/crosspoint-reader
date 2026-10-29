use super::state::*;
use crate::{
    model::Metrics,
    scene::{Arena, Command, Error, Interaction, Kind, Rect, Refresh},
};
pub const TITLE: u32 = 1;
pub const BODY: u32 = 2;
pub const CANCEL: u32 = 3;
pub const CONFIRM: u32 = 4;
pub const READOUT: u32 = 5;
pub const MINIMUM: u32 = 6;
pub const MAXIMUM: u32 = 7;
pub const HINT1: u32 = 8;
pub const HINT2: u32 = 9;
pub const BRIGHT_LABEL: u32 = 10;
pub const WARM_LABEL: u32 = 11;
pub const BRIGHT_VALUE: u32 = 12;
pub const WARM_VALUE: u32 = 13;
pub const ON: u32 = 14;
pub const OFF: u32 = 15;
pub const AUTHOR: u32 = 16;
pub const SOURCE: u32 = 17;
pub const METADATA: u32 = 18;
pub const STATUS: u32 = 19;
pub const COPIES: u32 = 20;
pub const HOLDS: u32 = 21;
pub const DESCRIPTION_HEADING: u32 = 22;
pub const DESCRIPTION: u32 = 23;
pub const ACTION_LABEL: u32 = 24;
pub const MORE_LABEL: u32 = 25;
pub const MINUS: u32 = 26;
pub const PLUS: u32 = 27;
pub const HEADER: u32 = 1000;
pub const FOOTER: u32 = 1001;
pub const COVER: u32 = 1002;
pub const HTML: u32 = 1003;
pub const QR: u32 = 1004;
pub const BATTERY: u32 = 1005;
pub const SUN: u32 = 1010;
pub const SUN_FILLED: u32 = 1011;
pub const MOON: u32 = 1012;
pub const REFRESH_ICON: u32 = 1013;
pub const ORIENTATION_ICON: u32 = 1014;
pub const TOUCH_ICON: u32 = 1015;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Layout {
    pub screen: Rect,
    pub safe: Rect,
    pub body_font: i32,
    pub small_font: i32,
    pub title_font: i32,
    pub readout_font: i32,
    pub top_padding: i16,
    pub header_height: i16,
    pub spacing: i16,
    pub hints_height: i16,
    pub home_top_padding: i16,
    pub battery_height: i16,
    pub popup_padding: i16,
    pub popup_gap: i16,
    pub popup_side_margin: i16,
    pub selection_padding: i16,
    pub space_sm: i16,
    pub space_md: i16,
    pub space_lg: i16,
    pub row_height: i16,
    pub min_touch: i16,
    pub popup_radius: u8,
    pub popup_border: u8,
    pub row_radius: u8,
    pub selection: u8,
    pub control_radius: u8,
    pub capsule_radius: u8,
    pub sheet_radius: u8,
    pub body_bold: u8,
    pub touch: u8,
    pub light_present: u8,
    pub has_warmth: u8,
    pub show_cover: u8,
    pub html_description: u8,
    pub reserved: [u8; 3],
}
pub trait Texts {
    fn present(&self, handle: u32) -> Result<bool, Error>;
    fn height(
        &self,
        handle: u32,
        font: i32,
        width: i16,
        style: u8,
        lines: u8,
    ) -> Result<i16, Error>;
}
fn rect(x: i16, y: i16, w: i16, h: i16) -> Rect {
    Rect { x, y, w, h }
}
#[inline(never)]
fn inset(r: Rect, x: i16, y: i16) -> Rect {
    rect(r.x + x, r.y + y, (r.w - 2 * x).max(0), (r.h - 2 * y).max(0))
}
fn take(r: &mut Rect, h: i16, gap: i16) -> Rect {
    let h = h.min(r.h);
    if h <= 0 || r.w <= 0 {
        return Rect::default();
    }
    let out = rect(r.x, r.y, r.w, h);
    let used = (h + gap).min(r.h);
    r.y += used;
    r.h -= used;
    out
}
struct View<'a, 'b> {
    arena: &'a mut Arena<'b>,
    layout: &'a Layout,
    state: &'a State,
    metrics: &'a mut dyn Metrics,
    texts: &'a dyn Texts,
}
impl View<'_, '_> {
    #[inline(never)]
    fn fill(&mut self, r: Rect, paint: u8, radius: u8, corners: u8) -> Result<(), Error> {
        self.arena.push(Command {
            rect: r,
            paint,
            radius,
            corners,
            ..Command::default()
        })
    }
    fn stroke(&mut self, r: Rect, width: u8, radius: u8) -> Result<(), Error> {
        if width == 0 {
            return Ok(());
        }
        self.arena.push(Command {
            kind: Kind::Stroke as u8,
            rect: r,
            width,
            radius,
            ..Command::default()
        })
    }
    #[inline(never)]
    fn resource(&mut self, handle: u32, r: Rect) -> Result<(), Error> {
        self.arena.push(Command {
            kind: Kind::Bitmap as u8,
            rect: r,
            resource: handle,
            generation: 1,
            ..Command::default()
        })
    }
    #[allow(clippy::too_many_arguments)]
    fn text(
        &mut self,
        handle: u32,
        r: Rect,
        font: i32,
        style: u8,
        align: u8,
        lines: u8,
        paint: u8,
    ) -> Result<(), Error> {
        self.texts.present(handle)?;
        self.arena.push(Command {
            kind: Kind::TextHandle as u8,
            rect: r,
            resource: handle,
            generation: 1,
            font,
            style,
            align,
            max_lines: lines,
            paint,
            ..Command::default()
        })
    }
    #[inline(never)]
    fn hit(&mut self, r: Rect, action: u32, value: i32, inputs: u16) -> Result<(), Error> {
        self.arena.hit(Interaction {
            rect: r,
            action,
            value,
            generation: 1,
            inputs,
            flags: 0,
        })
    }
    #[inline(never)]
    fn line(&mut self, font: i32) -> Result<i16, Error> {
        let n = self.metrics.line_height(font);
        if !(1..=512).contains(&n) {
            return Err(Error::Invalid);
        }
        Ok(n as i16)
    }
    #[allow(clippy::too_many_arguments)]
    fn button(
        &mut self,
        r: Rect,
        label: u32,
        action: u32,
        value: i32,
        font: i32,
        bold: u8,
        radius: u8,
        border: u8,
        focused: bool,
        invert_focus: bool,
    ) -> Result<(), Error> {
        let min = if self.state.kind == Screen::Details as u8 {
            self.layout.min_touch
        } else {
            44
        };
        let mut h = r;
        if h.w < min {
            h.x -= (min - h.w) / 2;
            h.w = min
        }
        if h.h < min {
            h.y -= (min - h.h) / 2;
            h.h = min
        }
        let s = self.layout.screen;
        h.w = h.w.min(s.w);
        h.h = h.h.min(s.h);
        h.x = h.x.clamp(s.x, s.x + s.w - h.w);
        h.y = h.y.clamp(s.y, s.y + s.h - h.h);
        if h.x - s.x < 12 {
            h.w += h.x - s.x;
            h.x = s.x;
        }
        if h.y - s.y < 12 {
            h.h += h.y - s.y;
            h.y = s.y;
        }
        if s.x + s.w - h.x - h.w < 12 {
            h.w = s.x + s.w - h.x;
        }
        if s.y + s.h - h.y - h.h < 12 {
            h.h = s.y + s.h - h.y;
        }
        self.hit(h, action, value, 1)?;
        let matching = self.state.active_action == action && self.state.active_value == value;
        let active = matching;
        let focused = focused || (self.state.flashing(action, value) && border != 2);
        let invert = active || focused && invert_focus;
        self.fill(
            r,
            if invert {
                0
            } else if focused {
                2
            } else {
                1
            },
            radius,
            0,
        )?;
        self.stroke(r, border, radius)?;
        self.text(label, inset(r, 4, 2), font, bold, 1, 1, u8::from(invert))
    }
    fn capsule(&mut self, r: Rect, value: i32, max: i32, action: u32) -> Result<(), Error> {
        if r.w < r.h + 4 || r.h <= 4 {
            return Ok(());
        }
        self.hit(r, action, 0, 1 | DRAG)?;
        let radius = self.layout.capsule_radius.min((r.h / 2) as u8);
        self.fill(r, 1, radius, 0)?;
        let inner = inset(r, 2, 2);
        let cap = inner.h / 2;
        let travel = inner.w - 2 * cap;
        let max = max.max(1);
        let cx = inner.x
            + cap
            + (i64::from(travel) * i64::from(value.clamp(0, max)) / i64::from(max)) as i16;
        let ir = self.layout.capsule_radius.min(cap as u8);
        self.fill(
            rect(inner.x, inner.y, (cx + cap - inner.x).min(inner.w), inner.h),
            0,
            ir,
            0,
        )?;
        self.stroke(r, 2, radius)?;
        let handle = rect(cx - cap, inner.y, 2 * cap, inner.h);
        self.fill(handle, 1, ir, 0)?;
        self.stroke(handle, 2, ir)
    }
    fn popup(&mut self, interval: bool) -> Result<(), Error> {
        let l = *self.layout;
        let line = self.line(l.body_font)?;
        let small = self.line(l.small_font)?;
        let width = (l.screen.w * 3 / 4).min(l.screen.w - 2 * l.popup_side_margin);
        let inner = width - 2 * l.popup_padding;
        if inner <= 0 {
            return Err(Error::Invalid);
        }
        let title_font = if interval { l.small_font } else { l.body_font };
        let body_font = if interval {
            l.readout_font
        } else {
            l.body_font
        };
        let title_lines = if interval { 1 } else { 2 };
        let body_lines = if interval { 1 } else { 3 };
        let body_handle = if interval { READOUT } else { BODY };
        let title_h = if self.texts.present(TITLE)? {
            self.texts
                .height(TITLE, title_font, inner, 1, title_lines)?
        } else {
            0
        };
        let body_h = if self.texts.present(body_handle)? {
            self.texts.height(
                body_handle,
                body_font,
                inner,
                u8::from(interval),
                body_lines,
            )?
        } else {
            0
        };
        let button_h = line + 2 * l.selection_padding;
        let touch = l.touch != 0;
        let slider_h = small + l.space_md + if touch { 56 } else { 24 };
        let hints_h = if touch {
            0
        } else {
            small * 2 + l.space_sm + l.space_md
        };
        let content_h = if interval { slider_h + hints_h } else { 0 };
        let buttons_h = if !interval {
            2 * button_h + l.popup_gap
        } else if touch {
            button_h
        } else {
            0
        };
        let height = (2 * l.popup_padding
            + if title_h > 0 {
                title_h + l.popup_gap
            } else {
                0
            }
            + if body_h > 0 { body_h + l.popup_gap } else { 0 }
            + if content_h > 0 {
                l.popup_gap + content_h
            } else {
                0
            }
            + if buttons_h > 0 {
                l.popup_gap + buttons_h
            } else {
                0
            }
            - if interval { l.space_md } else { 0 })
        .clamp(0, l.screen.h);
        let card = rect(
            l.screen.x + (l.screen.w - width) / 2,
            l.screen.y + (l.screen.h - height) / 2,
            width,
            height,
        );
        self.hit(card, CHROME, 0, 1)?;
        self.fill(card, 1, l.popup_radius, 0)?;
        self.stroke(card, l.popup_border, l.popup_radius)?;
        let area = inset(card, l.popup_padding, l.popup_padding);
        let mut y = area.y;
        if title_h > 0 {
            self.text(
                TITLE,
                rect(area.x, y, area.w, 1),
                title_font,
                1,
                1,
                title_lines,
                0,
            )?;
            y += title_h + l.popup_gap;
        }
        if body_h > 0 {
            self.text(
                body_handle,
                rect(area.x, y, area.w, 1),
                body_font,
                u8::from(interval),
                1,
                body_lines,
                0,
            )?;
        }
        let buttons_y = area.y + area.h - buttons_h;
        if !interval {
            for (i, label) in [CANCEL, CONFIRM].into_iter().enumerate() {
                self.button(
                    rect(
                        area.x,
                        buttons_y + i as i16 * (button_h + l.popup_gap),
                        area.w,
                        button_h,
                    ),
                    label,
                    OPTION,
                    i as i32,
                    l.body_font,
                    0,
                    if l.selection == 0 && l.row_radius > 0 {
                        l.row_radius
                    } else {
                        0
                    },
                    0,
                    self.state.selected == i as u8,
                    l.selection == 0 && l.row_radius > 0,
                )?;
            }
            return Ok(());
        }
        if touch {
            let mut x = area.x;
            for (i, label) in [MINUS, PLUS, CONFIRM].into_iter().enumerate() {
                let w = if i < 2 {
                    button_h
                } else {
                    area.w - 2 * button_h - 2 * l.popup_gap
                };
                self.button(
                    rect(x, buttons_y, w, button_h),
                    label,
                    if i == 2 { OK } else { STEP },
                    if i == 0 {
                        -1
                    } else if i == 1 {
                        1
                    } else {
                        0
                    },
                    l.body_font,
                    l.body_bold,
                    0,
                    0,
                    false,
                    false,
                )?;
                x += w + l.popup_gap;
            }
        }
        let band = rect(
            area.x,
            buttons_y - if buttons_h > 0 { l.popup_gap } else { 0 } - content_h,
            area.w,
            content_h,
        );
        if !touch {
            let y = band.y + slider_h + l.space_md;
            self.text(
                HINT1,
                rect(band.x, y, band.w, small),
                l.small_font,
                0,
                1,
                1,
                0,
            )?;
            self.text(
                HINT2,
                rect(band.x, y + small + l.space_sm, band.w, small),
                l.small_font,
                0,
                1,
                1,
                0,
            )?;
        }
        let caption = rect(band.x, band.y, band.w, small);
        self.text(MINIMUM, caption, l.small_font, 0, 0, 1, 0)?;
        self.text(MAXIMUM, caption, l.small_font, 0, 2, 1, 0)?;
        self.capsule(
            rect(
                band.x,
                band.y + small + l.space_md,
                band.w,
                if touch { 56 } else { 24 },
            ),
            self.state.value - self.state.minimum,
            (self.state.maximum - self.state.minimum).max(1),
            SLIDER,
        )
    }
    fn media_text(
        &mut self,
        area: &mut Rect,
        handle: u32,
        bold: u8,
        lines: u8,
        gap: i16,
    ) -> Result<(), Error> {
        let font = self.layout.body_font;
        let line = self.line(font)?;
        if !self.texts.present(handle)? || area.w <= 0 || area.h < line {
            return Ok(());
        }
        let lines = lines.min((area.h / line).min(255) as u8);
        let h = self
            .texts
            .height(handle, font, area.w, bold, lines)?
            .min(area.h);
        self.text(
            handle,
            rect(area.x, area.y, area.w, h),
            font,
            bold,
            0,
            lines,
            0,
        )?;
        let used = (h + gap).min(area.h);
        area.y += used;
        area.h -= used;
        Ok(())
    }
    fn details(&mut self, feedback: &mut Feedback) -> Result<(), Error> {
        let l = *self.layout;
        let top = l.top_padding + l.header_height + l.spacing;
        let bottom = (l.screen.h - l.hints_height).min(l.safe.y + l.safe.h);
        let top = top.max(l.safe.y);
        let outer = rect(l.safe.x, top, l.safe.w, (bottom - top).max(0));
        self.fill(outer, 1, 0, 0)?;
        let mut body = inset(outer, 16, 16);
        if body.w <= 0 || body.h <= 0 {
            return Ok(());
        }
        let line = self.line(l.body_font)?;
        let action_h = l.row_height.max(l.min_touch).max(line + 4).max(44);
        for (label, action) in [(ACTION_LABEL, PRIMARY), (MORE_LABEL, MORE)] {
            let present = self.texts.present(label)?
                && (action != MORE || self.texts.present(DESCRIPTION)?);
            if present && body.h >= action_h {
                let r = rect(body.x, body.y + body.h - action_h, body.w, action_h);
                body.h -= (action_h + 8).min(body.h);
                self.button(
                    r,
                    label,
                    action,
                    0,
                    l.body_font,
                    l.body_bold,
                    l.control_radius,
                    1,
                    false,
                    false,
                )?;
            }
        }
        if body.h <= 0 {
            return Ok(());
        }
        let mut availability_h = 0;
        for h in [STATUS, COPIES, HOLDS] {
            if self.texts.present(h)? {
                availability_h += line + 6
            }
        }
        if availability_h > 0 {
            availability_h += 10
        }
        let has_source = self.texts.present(SOURCE)?;
        let hero_h = if l.show_cover != 0 {
            192
        } else {
            (if has_source { 4 } else { 3 }) * line + 20 + if has_source { 6 } else { 0 }
        };
        let budget = body.h - availability_h - 12;
        let hero_h = hero_h.min(budget.max(body.h / 2));
        let mut hero = take(&mut body, hero_h, 12);
        self.fill(hero, 1, 0, 0)?;
        if l.show_cover != 0 {
            let w = 128.min(hero.w / 3).min(hero.h * 128 / 192);
            if w > 0 {
                feedback.cover = rect(hero.x, hero.y, w, w * 192 / 128);
                self.resource(COVER, feedback.cover)?;
                let used = (w + 16).min(hero.w);
                hero.x += used;
                hero.w -= used;
            }
        }
        self.media_text(
            &mut hero,
            TITLE,
            1,
            if l.show_cover != 0 { 3 } else { 2 },
            10,
        )?;
        self.media_text(
            &mut hero,
            AUTHOR,
            l.body_bold,
            if l.show_cover != 0 { 2 } else { 1 },
            10,
        )?;
        self.media_text(&mut hero, SOURCE, l.body_bold, 1, 6)?;
        if availability_h > 0 {
            let mut area = take(&mut body, availability_h, 12);
            self.fill(area, 1, 0, 0)?;
            self.fill(rect(area.x, area.y, area.w, 1.min(area.h)), 0, 0, 0)?;
            area.y += 10;
            area.h = (area.h - 10).max(0);
            self.media_text(&mut area, STATUS, 1, 1, 6)?;
            self.media_text(&mut area, COPIES, l.body_bold, 1, 6)?;
            self.media_text(&mut area, HOLDS, l.body_bold, 1, 6)?;
        }
        self.media_text(&mut body, METADATA, l.body_bold, 3, 12)?;
        self.media_text(&mut body, DESCRIPTION_HEADING, 1, 1, 8)?;
        if l.html_description != 0 && body.w > 0 && body.h > 0 {
            feedback.description = body;
            self.resource(HTML, body)?;
        } else {
            self.media_text(&mut body, DESCRIPTION, l.body_bold, 255, 0)?;
        }
        Ok(())
    }
    fn slider_row(&mut self, y: i16, warm: bool) -> Result<(), Error> {
        let l = *self.layout;
        let line = self.line(l.small_font)?;
        let x = l.safe.x + 16;
        let width = l.safe.w - 32;
        let caption = rect(x, y, width, line);
        self.text(
            if warm { WARM_LABEL } else { BRIGHT_LABEL },
            caption,
            l.small_font,
            1,
            0,
            1,
            0,
        )?;
        self.text(
            if warm { WARM_VALUE } else { BRIGHT_VALUE },
            caption,
            l.small_font,
            1,
            2,
            1,
            0,
        )?;
        let y = y + line + l.space_md;
        let mut right = x + width;
        let step = if warm { WARMTH_STEP } else { BRIGHTNESS_STEP };
        if !warm && self.state.switch_owns_light == 0 {
            let r = rect(right - 56, y, 56, 56);
            self.hit(r, TOGGLE, 0, 1)?;
            let active = self.state.active_action == TOGGLE;
            self.fill(r, u8::from(!active), l.control_radius, 0)?;
            self.stroke(r, 2, l.control_radius)?;
            self.arena.push(Command {
                kind: Kind::Bitmap as u8,
                rect: rect(r.x + 12, r.y + 12, 32, 32),
                resource: if self.state.light_on != 0 {
                    SUN_FILLED
                } else {
                    SUN
                },
                generation: 1,
                paint: u8::from(active),
                ..Command::default()
            })?;
            right -= 56 + l.space_md;
        }
        self.button(
            rect(x, y, 56, 56),
            MINUS,
            step,
            -1,
            l.title_font,
            1,
            l.control_radius,
            2,
            false,
            false,
        )?;
        let plus = right - 56;
        self.button(
            rect(plus, y, 56, 56),
            PLUS,
            step,
            1,
            l.title_font,
            1,
            l.control_radius,
            2,
            false,
            false,
        )?;
        self.capsule(
            rect(
                x + 56 + l.space_md,
                y,
                plus - l.space_md - x - 56 - l.space_md,
                56,
            ),
            i32::from(if warm {
                self.state.warmth
            } else {
                self.state.brightness
            }),
            100,
            if warm { WARMTH } else { BRIGHTNESS },
        )
    }
    fn frontlight(&mut self, feedback: &mut Feedback) -> Result<(), Error> {
        let l = *self.layout;
        let line = self.line(l.small_font)?;
        let battery = l.battery_height.max(line);
        let row_h = line + l.space_md + 56 + 2 * l.space_md;
        let mut bottom = l.space_md + battery + l.space_md;
        if l.light_present != 0 {
            bottom += row_h;
            if l.has_warmth != 0 {
                bottom += row_h
            }
            bottom += l.space_sm
        }
        if l.touch != 0 {
            bottom += 48 + l.space_sm + line
        }
        bottom += 2 * l.space_lg + 5 + l.space_md;
        feedback.panel_bottom = bottom;
        let sheet_h = bottom + l.safe.y.max(0);
        self.fill(rect(0, 0, l.screen.w, sheet_h), 1, l.sheet_radius, 12)?;
        self.fill(rect(0, sheet_h - 2, l.screen.w, 2), 0, 0, 0)?;
        self.fill(
            rect(
                (l.screen.w - 72) / 2,
                sheet_h - l.space_lg - l.space_md - 5,
                72,
                5,
            ),
            0,
            2,
            0,
        )?;
        self.resource(
            BATTERY,
            rect(
                0,
                l.top_padding,
                l.screen.w,
                l.home_top_padding - l.top_padding,
            ),
        )?;
        let mut y = l.safe.y + l.space_md + battery + l.space_md;
        if l.light_present != 0 {
            self.slider_row(y, false)?;
            y += row_h;
            if l.has_warmth != 0 {
                self.slider_row(y, true)?;
                y += row_h;
            }
            y += l.space_sm;
        }
        if l.touch != 0 {
            let row = rect(l.safe.x + 16, y, l.safe.w - 32, 48 + l.space_sm + line);
            for (i, handle) in [MOON, REFRESH_ICON, ORIENTATION_ICON, TOUCH_ICON]
                .into_iter()
                .enumerate()
            {
                let left = row.x + row.w * i as i16 / 4;
                let right = row.x + row.w * (i as i16 + 1) / 4;
                let slot = rect(left, y, right - left, row.h);
                let circle = rect(left + (slot.w - 48) / 2, y, 48, 48);
                let toggle = i == 0 || i == 3;
                let enabled =
                    i == 0 && self.state.inverted != 0 || i == 3 && self.state.touch_mode != 0;
                if toggle && enabled {
                    self.fill(circle, 0, 24, 0)?
                } else if toggle
                    || self.state.active_action == TILE && self.state.active_value == i as i32
                {
                    self.stroke(circle, 2, 24)?
                }
                let icon = rect(circle.x + 8, circle.y + 8, 32, 32);
                self.arena.push(Command {
                    kind: Kind::Bitmap as u8,
                    rect: icon,
                    resource: handle,
                    generation: 1,
                    paint: u8::from(enabled),
                    style: if i == 2 {
                        self.state.orientation % 4
                    } else {
                        0
                    },
                    ..Command::default()
                })?;
                if i == 2 {
                    let r = match self.state.orientation % 4 {
                        0 => rect(4, 29, 24, 3),
                        1 => rect(0, 4, 3, 24),
                        2 => rect(4, 0, 24, 3),
                        _ => rect(29, 4, 3, 24),
                    };
                    self.fill(rect(icon.x + r.x, icon.y + r.y, r.w, r.h), 0, 0, 0)?;
                }
                if toggle {
                    self.text(
                        if enabled { ON } else { OFF },
                        rect(slot.x, y + 48 + l.space_sm, slot.w, line),
                        l.small_font,
                        0,
                        1,
                        1,
                        0,
                    )?;
                }
                self.hit(slot, TILE, i as i32, 1)?;
            }
        }
        Ok(())
    }
}
pub fn view(
    arena: &mut Arena<'_>,
    state: &State,
    layout: &Layout,
    metrics: &mut dyn Metrics,
    texts: &dyn Texts,
    _previous: Feedback,
) -> Result<(Feedback, Refresh), Error> {
    let screen = validate(state, layout)?;
    if screen == Screen::Frontlight {
        return Err(Error::Invalid);
    }
    let mut v = View {
        arena,
        layout,
        state,
        metrics,
        texts,
    };
    let mut feedback = Feedback {
        clean_revision: state.clean_revision,
        ..Feedback::default()
    };
    if matches!(screen, Screen::Confirmation | Screen::Details | Screen::Qr) {
        v.arena.push(Command {
            kind: Kind::Clear as u8,
            rect: layout.screen,
            paint: 1,
            ..Command::default()
        })?;
    }
    if screen == Screen::Confirmation {
        v.resource(FOOTER, layout.screen)?;
    }
    match screen {
        Screen::Confirmation => v.popup(false)?,
        Screen::Interval => v.popup(true)?,
        Screen::Details => {
            v.resource(
                HEADER,
                rect(0, layout.top_padding, layout.screen.w, layout.header_height),
            )?;
            v.details(&mut feedback)?;
        }
        Screen::Frontlight => return Err(Error::Invalid),
        Screen::Qr => {
            v.resource(
                HEADER,
                rect(0, layout.top_padding, layout.screen.w, layout.header_height),
            )?;
            v.resource(
                QR,
                rect(
                    20,
                    layout.top_padding + layout.header_height + layout.spacing,
                    layout.screen.w - 40,
                    layout.screen.h
                        - layout.top_padding
                        - layout.header_height
                        - 2 * layout.spacing
                        - 40,
                ),
            )?;
        }
    }
    if matches!(screen, Screen::Interval | Screen::Details | Screen::Qr) {
        v.resource(FOOTER, layout.screen)?;
    }
    Ok((feedback, Refresh::Fast))
}

pub fn frontlight_view(
    arena: &mut Arena<'_>,
    state: &State,
    layout: &Layout,
    metrics: &mut dyn Metrics,
    texts: &dyn Texts,
    previous: Feedback,
) -> Result<(Feedback, Refresh), Error> {
    if validate(state, layout)? != Screen::Frontlight {
        return Err(Error::Invalid);
    }
    let mut v = View {
        arena,
        layout,
        state,
        metrics,
        texts,
    };
    let mut feedback = Feedback {
        clean_revision: state.clean_revision,
        ..Feedback::default()
    };
    v.frontlight(&mut feedback)?;
    Ok((
        feedback,
        if previous.clean_revision != state.clean_revision {
            Refresh::Half
        } else {
            Refresh::Fast
        },
    ))
}

#[inline(never)]
fn validate(state: &State, layout: &Layout) -> Result<Screen, Error> {
    let screen = Screen::from_byte(state.kind).ok_or(Error::Invalid)?;
    if [
        layout.top_padding,
        layout.header_height,
        layout.spacing,
        layout.hints_height,
        layout.home_top_padding,
        layout.battery_height,
        layout.popup_padding,
        layout.popup_gap,
        layout.popup_side_margin,
        layout.selection_padding,
        layout.space_sm,
        layout.space_md,
        layout.space_lg,
        layout.row_height,
        layout.min_touch,
    ]
    .iter()
    .any(|v| !(0..=512).contains(v))
        || layout.safe.x < 0
        || layout.safe.y < 0
        || i32::from(layout.safe.x) + i32::from(layout.safe.w) > i32::from(layout.screen.w)
        || i32::from(layout.safe.y) + i32::from(layout.safe.h) > i32::from(layout.screen.h)
        || i64::from(state.maximum) - i64::from(state.minimum) > i64::from(i32::MAX)
    {
        return Err(Error::Invalid);
    }
    if layout.screen.x != 0
        || layout.screen.y != 0
        || !(1..=4096).contains(&layout.screen.w)
        || !(1..=4096).contains(&layout.screen.h)
        || layout.safe.w < 0
        || layout.safe.h < 0
        || layout.popup_padding < 0
        || layout.popup_side_margin < 0
        || layout.popup_gap < 0
        || state.minimum > state.maximum
    {
        return Err(Error::Invalid);
    }
    Ok(screen)
}
