//! Loading popup composition; native code only supplies theme facts and refresh.
use super::abi::{Resources, Text};
use crate::{
    page_render::direct::{xp_native_render_target, Target},
    text_run::Run,
};
use core::{mem::MaybeUninit, ptr, slice};
use xp_core::{glyph::Surface, strip::Strip};
use xp_ui::{
    dictionary::popup::Layout,
    scene::{Command, Kind, Rect},
};

#[repr(C)]
#[derive(Default)]
pub struct Facts {
    pub layout: Layout,
    pub progress_height: i16,
    pub progress_outline: u8,
    pub progress_clamp: u8,
    pub progress_fill_black: u8,
    pub progress_outline_black: u8,
}
const _: () = assert!(core::mem::size_of::<Facts>() == 40);

impl Resources {
    pub unsafe fn popup_draw(&self, text: Text) -> Rect {
        #[cfg(any(test, feature = "app-test"))]
        {
            (self.popup_draw)(text)
        }
        #[cfg(not(any(test, feature = "app-test")))]
        {
            let mut facts = Facts::default();
            (self.popup_facts)(&mut facts);
            let (mut target, cache) = target();
            let rect = paint(&facts, text.bytes().unwrap_or_default(), &mut target, cache);
            (self.refresh)(xp_ui::scene::Refresh::Fast as u8);
            rect
        }
    }
    pub unsafe fn popup_progress(&self, rect: Rect, percent: i32) {
        #[cfg(any(test, feature = "app-test"))]
        {
            (self.popup_progress)(rect, percent);
        }
        #[cfg(not(any(test, feature = "app-test")))]
        {
            let mut facts = Facts::default();
            (self.popup_facts)(&mut facts);
            let (target, _) = target();
            progress(&facts, rect, percent, &target);
            (self.refresh)(xp_ui::scene::Refresh::Fast as u8);
        }
    }
}
unsafe fn target() -> (Target, bool) {
    let mut target = MaybeUninit::uninit();
    let cache = xp_native_render_target(target.as_mut_ptr(), ptr::null_mut()) != 0;
    let mut target = target.assume_init();
    target.glyphs.scanning = if cache {
        crate::font_runtime::scanning(target.fonts)
    } else {
        0
    };
    (target, cache)
}

#[inline(never)]
pub(crate) unsafe fn paint(f: &Facts, text: &[u8], target: &mut Target, cache: bool) -> Rect {
    let l = &f.layout;
    let run = Run::new(target, cache);
    let text_width = run.width_styled(l.font, text, l.bold);
    let text_height = run.height(l.font);
    let (mx, my, frame) = (
        i32::from(l.margin_x),
        i32::from(l.margin_y),
        i32::from(l.frame),
    );
    let (w, h) = (text_width + mx * 2, text_height + my * 2);
    let x = (i32::from(l.screen.w) - w) / 2;
    let y = (f32::from(l.screen.h) * l.top_ratio) as i32;
    if !target.glyphs.buffer.is_null() && target.glyphs.valid() {
        // Geometry's exclusive framebuffer loan ends before text lends it again.
        {
            let bytes =
                slice::from_raw_parts_mut(target.glyphs.buffer, target.glyphs.buffer_len as usize);
            if let Some(mut strip) = Strip::new(ptr::read(&target.glyphs), bytes) {
                let rounded = l.radius > 0;
                for (x, y, w, h, radius, paint) in [
                    (
                        x - frame,
                        y - frame,
                        w + frame * 2,
                        h + frame * 2,
                        if rounded { l.radius + l.frame } else { 0 },
                        u8::from(rounded),
                    ),
                    (x, y, w, h, l.radius, u8::from(!rounded)),
                ] {
                    xp_ui::draw::geometry(
                        &mut strip,
                        &Command {
                            kind: Kind::Fill as u8,
                            rect: Rect {
                                x: x as i16,
                                y: y as i16,
                                w: w as i16,
                                h: h as i16,
                            },
                            radius: radius as u8,
                            paint,
                            ..Command::default()
                        },
                    );
                }
            }
        }
        run.draw_styled(
            l.font,
            x + (w - text_width) / 2,
            y + my + i32::from(l.text_y),
            text,
            l.bold,
            l.inverted != 0,
        );
    }
    Rect {
        x: x as i16,
        y: y as i16,
        w: w as i16,
        h: h as i16,
    }
}

#[inline(never)]
pub(crate) unsafe fn progress(f: &Facts, r: Rect, percent: i32, target: &Target) {
    let height = i32::from(f.progress_height);
    let width = (i32::from(r.w) - i32::from(f.layout.margin_x) * 2).max(0);
    if width <= 0 || height <= 0 || target.glyphs.buffer.is_null() {
        return;
    }
    let x = i32::from(r.x) + (i32::from(r.w) - width) / 2;
    let y = i32::from(r.y) + i32::from(r.h) - i32::from(f.layout.margin_y) / 2 - height / 2 - 1;
    let percent = if f.progress_clamp != 0 {
        percent.clamp(0, 100)
    } else {
        percent
    };
    let fill = width.wrapping_mul(percent) / 100;
    let bytes = slice::from_raw_parts_mut(target.glyphs.buffer, target.glyphs.buffer_len as usize);
    if let Some(mut surface) = Surface::new(&target.glyphs, bytes) {
        if f.progress_outline != 0 {
            for (dx, dy, w, h) in [
                (0, 0, width, 1),
                (width - 1, 0, 1, height),
                (0, height - 1, width, 1),
                (0, 0, 1, height),
            ] {
                surface.fill(x + dx, y + dy, w, h, f.progress_outline_black != 0);
            }
        }
        if fill > 0 {
            surface.fill(x, y, fill, height, f.progress_fill_black != 0);
        }
    }
}

#[cfg(feature = "app-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_popup(
    facts: *const Facts,
    text: Text,
    rect: *mut Rect,
    percent: i32,
    is_progress: u8,
) {
    let (mut target, cache) = target();
    if is_progress != 0 {
        progress(&*facts, *rect, percent, &target);
    } else {
        *rect = paint(
            &*facts,
            text.bytes().unwrap_or_default(),
            &mut target,
            cache,
        );
    }
}
