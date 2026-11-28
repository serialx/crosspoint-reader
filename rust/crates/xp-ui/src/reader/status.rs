//! Reader status geometry and fixed-buffer counters.
use core::fmt::{self, Write};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Facts {
    pub screen_width: i32,
    pub screen_height: i32,
    pub margin_left: i32,
    pub margin_right: i32,
    pub margin_bottom: i32,
    pub status_height: i32,
    pub progress_height: i32,
    pub status_margin: i32,
    pub horizontal_margin: i32,
    pub corner: i32,
    pub offset_y: i32,
    pub battery_width: i32,
    pub battery_height: i32,
    pub font: i32,
    pub estimate_font: i32,
    pub bookmark: *const u8,
    pub battery_percent: u16,
    pub show_count: u8,
    pub show_percent: u8,
    pub title_mode: u8,
    pub show_battery: u8,
    pub show_battery_percent: u8,
    pub clock_mode: u8,
    pub bar_mode: u8,
    pub bar_height: u8,
    pub clock_available: u8,
    pub clock_valid: u8,
    pub charging: u8,
    pub battery_style: u8,
    pub clock: [u8; 9],
}
impl Facts {
    pub fn text_lane(&self) -> bool {
        self.show_count != 0
            || self.show_percent != 0
            || self.title_mode != 2
            || self.show_battery != 0
            || (self.clock_mode != 0 && self.clock_available != 0)
    }
}
pub struct Counter {
    bytes: [u8; 32],
    len: usize,
}
impl Counter {
    pub fn as_z(&self) -> &[u8] {
        &self.bytes[..=self.len]
    }
}
impl Write for Counter {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let n = text.len().min(31 - self.len);
        self.bytes[self.len..self.len + n].copy_from_slice(&text.as_bytes()[..n]);
        self.len += n;
        Ok(())
    }
}
pub fn progress_text(
    page: i32,
    pages: i32,
    percent: f32,
    count: bool,
    show_percent: bool,
) -> Counter {
    let mut out = Counter {
        bytes: [0; 32],
        len: 0,
    };
    if count {
        let _ = write!(out, "{page}/{pages}");
    }
    if show_percent {
        if count {
            let _ = out.write_str("  ");
        }
        if percent.is_sign_negative() {
            let _ = out.write_str("-");
        }
        let abs = percent.abs();
        if abs.is_nan() {
            let _ = out.write_str("nan");
        } else if abs.is_infinite() {
            let _ = out.write_str("inf");
        } else {
            // Reader percentages and u16 battery values fit u32; saturate malformed inputs.
            let mut rounded = abs as u32;
            let fraction = abs - rounded as f32;
            if fraction > 0.5 || (fraction == 0.5 && rounded & 1 != 0) {
                rounded = rounded.saturating_add(1);
            }
            // Integer formatting preserves %.0f ties without the floating formatter's heap work.
            let _ = write!(out, "{rounded}");
        }
        let _ = out.write_str("%");
    }
    out
}
pub struct Layout {
    pub text_y: i32,
    pub left: i32,
    pub right: i32,
    pub left_width: i32,
    pub right_width: i32,
}
impl Layout {
    pub fn new(f: &Facts) -> Self {
        Self {
            text_y: f.screen_height - f.status_height - f.margin_bottom - 4 + f.offset_y,
            left: f.horizontal_margin + f.margin_left.max(f.corner) + 1,
            right: f.screen_width - f.horizontal_margin - f.margin_right.max(f.corner),
            left_width: 0,
            right_width: 0,
        }
    }
    pub fn progress(&mut self, width: i32, estimate: i32, show_estimate: bool) -> i32 {
        self.right_width = width + estimate + if show_estimate { 2 } else { 0 };
        self.right - self.right_width
    }
    pub fn battery(&mut self, width: i32, text: i32, show_percent: bool) {
        self.left_width += width + if show_percent { text + 4 } else { 0 };
    }
    pub fn clock(&mut self, width: i32, mode: u8) -> i32 {
        if mode == 2 {
            let x = self.left + self.left_width + if self.left_width > 0 { 10 } else { 0 };
            self.left_width += width + 10;
            x
        } else if mode == 1 {
            let x =
                self.right - self.right_width - if self.right_width > 0 { 10 } else { 0 } - width;
            self.right_width += width + 10;
            x
        } else {
            0
        }
    }
    pub fn bookmark(&mut self) -> i32 {
        let gap = if self.left_width > 0 { 4 } else { 0 };
        let x = self.left + self.left_width + gap;
        self.left_width += 16 + gap;
        x
    }
    pub fn title_space(&self, f: &Facts, width: i32) -> (i32, i32) {
        let screen = f.screen_width - 2 * f.horizontal_margin - f.margin_left - f.margin_right;
        let left = self.left_width + 30;
        let right = self.right_width + 30;
        let margin = left.max(right);
        if width > screen - 2 * margin {
            (left, screen - left - right)
        } else {
            (margin, screen - 2 * margin)
        }
    }
}
pub fn bar(f: &Facts, page: i32, pages: i32, percent: f32) -> Option<(i32, i32, i32, i32)> {
    if f.bar_mode == 2 || (f.bar_mode == 0 && percent < 0.0) {
        return None;
    }
    let progress = if f.bar_mode == 0 {
        percent as u32
    } else if pages > 0 {
        (page as f32 / pages as f32 * 100.0) as u32
    } else {
        0
    };
    Some((
        0,
        f.screen_height - f.margin_bottom - i32::from(f.bar_height) + 1,
        ((f.screen_width as u32).wrapping_mul(progress) / 100) as i32,
        i32::from(f.bar_height) + f.margin_bottom - 1,
    ))
}

const _: () = {
    let wide = cfg!(target_pointer_width = "64");
    assert!(core::mem::size_of::<Facts>() == if wide { 96 } else { 88 });
    assert!(core::mem::offset_of!(Facts, bookmark) == if wide { 64 } else { 60 });
    assert!(core::mem::offset_of!(Facts, clock) == if wide { 86 } else { 78 });
};
