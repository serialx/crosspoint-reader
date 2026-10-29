//! Boot, sleep and image screen policy. Storage and decoding remain borrowed resources.
pub mod ffi;
pub mod view;
use crate::model::*;

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct SleepPlan {
    pub route: u8,
    pub fallback: u8,
    pub preserve_frame: u8,
    pub invert_frame: u8,
    pub normalize_polarity: u8,
    pub dark_default: u8,
    pub release_fonts: u8,
    pub overlay_cover: u8,
}
#[repr(u8)]
pub enum SleepRoute {
    Default,
    Custom,
    Cover,
    Overlay,
    Blank,
    Quick,
}
pub fn sleep_plan(mode: u8, timeout: bool, quick: bool, reader: bool, inverted: bool) -> SleepPlan {
    use SleepRoute::*;
    if mode == 6 || timeout && quick {
        return SleepPlan {
            route: Quick as u8,
            ..SleepPlan::default()
        };
    }
    let overlay = mode == 7 || mode == 8;
    let route = match mode {
        2 => Custom,
        3 | 8 => Cover,
        4 => {
            if reader {
                Cover
            } else {
                Custom
            }
        }
        5 => Blank,
        7 => Overlay,
        _ => Default,
    };
    SleepPlan {
        route: route as u8,
        fallback: match mode {
            4 => Custom as u8,
            8 => Overlay as u8,
            _ => Default as u8,
        },
        preserve_frame: overlay as u8,
        invert_frame: (overlay && inverted) as u8,
        normalize_polarity: 1,
        dark_default: (mode != 1) as u8,
        release_fonts: overlay as u8,
        overlay_cover: (mode == 8) as u8,
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ImageState {
    pub selected: i32,
    pub count: i32,
    pub can_set_cover: u8,
    pub reserved: [u8; 3],
}
pub fn interest(crash: bool) -> Interest {
    if crash {
        Interest {
            pressed: BACK,
            touch_flags: TOUCH_RELEASED,
            ..Interest::default()
        }
    } else {
        Interest {
            released: BACK | CONFIRM | LEFT | RIGHT | UP | DOWN,
            swipes: (1 << SWIPE_LEFT) | (1 << SWIPE_RIGHT),
            ..Interest::default()
        }
    }
}
pub fn crash_update(i: &Input) -> Effect {
    Effect {
        kind: if i.pressed & BACK != 0 || i.action == 1 {
            EffectKind::Cancel as u8
        } else {
            0
        },
        ..Effect::default()
    }
}
pub fn image_update(s: &mut ImageState, i: &Input) -> Effect {
    if i.released & BACK != 0 {
        return Effect {
            kind: EffectKind::Cancel as u8,
            ..Effect::default()
        };
    }
    let delta = match i.swipe {
        SWIPE_LEFT => 1,
        SWIPE_RIGHT => -1,
        _ => {
            if i.released & CONFIRM != 0 {
                return Effect {
                    kind: if s.can_set_cover != 0 {
                        EffectKind::Activate as u8
                    } else {
                        0
                    },
                    ..Effect::default()
                };
            }
            if i.released & (LEFT | UP) != 0 {
                -1
            } else if i.released & (RIGHT | DOWN) != 0 {
                1
            } else {
                0
            }
        }
    };
    let next = s.selected.saturating_add(delta);
    if delta != 0 && s.selected >= 0 && s.count > 1 && next >= 0 && next < s.count {
        s.selected = next;
        return Effect {
            kind: EffectKind::Scroll as u8,
            value: next,
            ..Effect::default()
        };
    }
    Effect::default()
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct XtcState {
    pub page: u32,
}
impl XtcState {
    pub fn turn(&mut self, count: u32, amount: i32, skip: bool) -> bool {
        let next = if skip {
            // The reader admits its one-past-last sentinel for the end-of-book menu.
            {
                let mut page = (self.page as i32).wrapping_add(amount);
                if page < 0 {
                    page = 0;
                }
                if page > count as i32 {
                    page = count as i32;
                }
                page as u32
            }
        } else if amount > 0 && self.page < count {
            self.page + 1
        } else if amount < 0 && self.page > 0 {
            self.page - 1
        } else {
            self.page
        };
        let changed = next != self.page;
        self.page = next;
        changed
    }
    pub fn restore(&mut self, count: u32, from_end: bool) {
        if from_end {
            self.page = count.saturating_sub(1);
        } else if self.page >= count && count > 0 {
            self.page = count - 1;
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Placement {
    pub x: i32,
    pub y: i32,
    pub crop_x: f32,
    pub crop_y: f32,
}
fn round(value: f32) -> i32 {
    let integer = value as i32;
    let fraction = value - integer as f32;
    if fraction >= 0.5 {
        integer + 1
    } else if fraction <= -0.5 {
        integer - 1
    } else {
        integer
    }
}

pub fn placement(w: i32, h: i32, sw: i32, sh: i32, crop: bool) -> Placement {
    let mut p = Placement::default();
    if w <= 0 || h <= 0 || sw <= 0 || sh <= 0 {
        return p;
    }
    if w > sw || h > sh {
        let mut ratio = w as f32 / h as f32;
        let screen = sw as f32 / sh as f32;
        if ratio > screen {
            if crop {
                p.crop_x = 1.0 - screen / ratio;
                ratio = (1.0 - p.crop_x) * w as f32 / h as f32;
            }
            p.y = round((sh as f32 - sw as f32 / ratio) / 2.0);
        } else {
            if crop {
                p.crop_y = 1.0 - ratio / screen;
                ratio = w as f32 / ((1.0 - p.crop_y) * h as f32);
            }
            p.x = round((sw as f32 - sh as f32 * ratio) / 2.0);
        }
    } else {
        p.x = (sw - w) / 2;
        p.y = (sh - h) / 2;
    }
    p
}

/// Draw decoded XTC page bytes into the caller's current strip without allocating.
pub fn xtc_pixels(
    target: &xp_core::glyph::GlyphTarget,
    frame: &mut [u8],
    data: &[u8],
    size: [u16; 2],
    depth: u8,
    pass: u8,
    outside: &mut dyn FnMut(i32, i32, bool),
) -> bool {
    let w = usize::from(size[0]);
    let h = usize::from(size[1]);
    let plane = w * h.div_ceil(8);
    let needed = if depth == 2 {
        plane * 2
    } else {
        w.div_ceil(8) * h
    };
    if w == 0 || h == 0 || data.len() < needed || pass > 2 {
        return false;
    }
    let Some(mut surface) = xp_core::glyph::Surface::new(target, frame) else {
        return false;
    };
    let (logical_w, logical_h) = if target.orientation & 1 == 0 {
        (target.height, target.width)
    } else {
        (target.width, target.height)
    };
    let mut draw = |x: i32, y: i32, black: bool| {
        if x >= target.clip.left
            && x < target.clip.right
            && y >= target.clip.top
            && y < target.clip.bottom
        {
            if x >= logical_w || y >= logical_h {
                outside(x, y, black);
            } else if target.scanning == 0 {
                surface.pixel(x, y, black);
            }
        }
    };
    for y in 0..h {
        for x in 0..w {
            if depth == 2 {
                let offset = (w - 1 - x) * h.div_ceil(8) + y / 8;
                let bit = 7 - y % 8;
                let value = ((data[offset] >> bit) & 1) * 2 + ((data[plane + offset] >> bit) & 1);
                let write = match pass {
                    0 => value >= 1,
                    1 => value == 1,
                    2 => value == 1 || value == 2,
                    _ => false,
                };
                if write {
                    draw(x as i32, y as i32, pass == 0);
                }
            } else if data[y * w.div_ceil(8) + x / 8] & (1 << (7 - x % 8)) == 0 {
                draw(x as i32, y as i32, true);
            }
        }
    }
    true
}

pub fn image_hints(s: &ImageState) -> u8 {
    (s.can_set_cover != 0) as u8
        | (((s.count > 1 && s.selected > 0) as u8) << 1)
        | (((s.count > 1 && s.selected != -1 && s.selected < s.count - 1) as u8) << 2)
}

#[cfg(test)]
mod tests;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct XtcOverlay {
    pub clear: crate::scene::Rect,
    pub padding_bottom: i32,
    pub draw: u8,
    pub reserved: [u8; 3],
}
pub fn xtc_overlay(
    sw: i16,
    sh: i16,
    top: i16,
    bottom: i16,
    height: i16,
    mode: u8,
    position: u8,
) -> XtcOverlay {
    // Settings: 0 hides, 1 is bottom, 2 is top. Position uses 0=bottom, 1=top.
    if !((mode == 1 && position == 0) || (mode == 2 && position == 1)) || height <= 0 {
        return XtcOverlay::default();
    }
    let y = if position == 0 {
        (i32::from(sh) - i32::from(bottom) - i32::from(height) - 4).max(0)
    } else {
        i32::from(top)
    };
    let h = if position == 0 {
        i32::from(sh) - i32::from(bottom) - y
    } else {
        i32::from(height) + 4
    };
    XtcOverlay {
        clear: crate::scene::Rect {
            x: 0,
            y: y as i16,
            w: sw,
            h: h.max(0) as i16,
        },
        padding_bottom: if position == 0 {
            0
        } else {
            i32::from(sh) - i32::from(height) - i32::from(bottom) - i32::from(top) - 4
        },
        draw: 1,
        reserved: [0; 3],
    }
}
