use super::*;
use crate::scene::{Interaction, SceneSink};

pub(super) struct Widgets<'a, S: SceneSink> {
    pub(super) arena: &'a mut S,
    pub(super) layout: &'a Layout,
    pub(super) state: &'a State,
    pub(super) metrics: &'a mut dyn Metrics,
    pub(super) texts: &'a dyn Texts,
}
impl<S: SceneSink> Widgets<'_, S> {
    #[inline(never)]
    pub(super) fn fill(
        &mut self,
        r: Rect,
        paint: u8,
        radius: u8,
        corners: u8,
    ) -> Result<(), Error> {
        self.arena.push(Command {
            rect: r,
            paint,
            radius,
            corners,
            ..Command::default()
        })
    }
    pub(super) fn stroke(&mut self, r: Rect, width: u8, radius: u8) -> Result<(), Error> {
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
    pub(super) fn resource(&mut self, handle: u32, r: Rect) -> Result<(), Error> {
        self.arena.push(Command {
            kind: Kind::Bitmap as u8,
            rect: r,
            resource: handle,
            generation: 1,
            ..Command::default()
        })
    }
    #[allow(clippy::too_many_arguments)]
    pub(super) fn text(
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
    pub(super) fn hit(
        &mut self,
        r: Rect,
        action: u32,
        value: i32,
        inputs: u16,
    ) -> Result<(), Error> {
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
    pub(super) fn line(&mut self, font: i32) -> Result<i16, Error> {
        let n = self.metrics.line_height(font);
        if !(1..=512).contains(&n) {
            return Err(Error::Invalid);
        }
        Ok(n as i16)
    }
    #[allow(clippy::too_many_arguments)]
    pub(super) fn button(
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
    pub(super) fn capsule(
        &mut self,
        r: Rect,
        value: i32,
        max: i32,
        action: u32,
    ) -> Result<(), Error> {
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
    pub(super) fn popup(&mut self, interval: bool) -> Result<(), Error> {
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
}
