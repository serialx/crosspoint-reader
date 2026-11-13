use super::{abi::XpEpubStatus, app_abi::Text};
use crate::{
    page_render::direct::{xp_native_render_target, Target},
    text_run::Run,
};
use core::{mem::MaybeUninit, ptr, slice};
use xp_core::glyph::{GlyphTarget, Surface};
use xp_ui::{
    network::wifi_resources::{text::truncate_joined, SmallZ},
    reader::status::{self, Facts, Layout},
};

// End each framebuffer loan before text rendering creates its own pixel loan.
unsafe fn shapes(target: &GlyphTarget, paint: impl FnOnce(&mut Surface<'_>)) {
    let buffer = slice::from_raw_parts_mut(target.buffer, target.buffer_len as usize);
    if let Some(mut surface) = Surface::new(target, buffer) {
        paint(&mut surface);
    }
}
#[inline(never)]
pub(super) unsafe fn paint(f: &Facts, value: &XpEpubStatus) {
    let mut target = MaybeUninit::uninit();
    let has_cache = xp_native_render_target(target.as_mut_ptr(), ptr::null_mut()) != 0;
    let mut target = target.assume_init();
    if target.glyphs.buffer.is_null() || !target.glyphs.valid() {
        return;
    }
    paint_target(f, value, &mut target, has_cache);
}
#[inline(never)]
pub(crate) unsafe fn paint_target(
    f: &Facts,
    v: &XpEpubStatus,
    target: &mut Target,
    has_cache: bool,
) {
    // The shared page snapshot omits scan state; native status primitives use it.
    target.glyphs.scanning = if has_cache {
        crate::font_runtime::scanning(target.fonts)
    } else {
        0
    };
    let run = Run::new(target, has_cache);
    let mut layout = Layout::new(f);
    let show_percent = f.show_percent != 0 && v.book_progress >= 0.0;
    if f.show_count != 0 || show_percent {
        let progress = status::progress_text(
            v.page,
            v.pages,
            v.book_progress,
            f.show_count != 0,
            show_percent,
        );
        let width = run.width(f.font, progress.as_z());
        let estimate = v.building != 0 && f.show_count != 0;
        let estimate_width = if estimate {
            run.width(f.estimate_font, b"~\0")
        } else {
            0
        };
        let x = layout.progress(width, estimate_width, estimate);
        if estimate {
            run.draw(
                f.estimate_font,
                x,
                layout.text_y + (run.height(f.font) - run.height(f.estimate_font)) / 2,
                b"~\0",
            );
        }
        run.draw(
            f.font,
            x + estimate_width + if estimate { 2 } else { 0 },
            layout.text_y,
            progress.as_z(),
        );
    }
    if let Some((x, y, w, h)) = status::bar(f, v.page, v.pages, v.book_progress) {
        shapes(&target.glyphs, |s| s.fill(x, y, w, h, true));
    }
    if f.show_battery != 0 {
        let text = status::progress_text(0, 0, f32::from(f.battery_percent), false, true);
        let y = run.battery_y(f.font, layout.text_y, f.battery_height);
        if f.show_battery_percent != 0 {
            run.draw(
                f.font,
                layout.left + f.battery_width + 4,
                layout.text_y,
                text.as_z(),
            );
        }
        shapes(&target.glyphs, |s| battery(s, f, layout.left, y));
        let width = if f.show_battery_percent != 0 {
            run.width(f.font, text.as_z())
        } else {
            0
        };
        layout.battery(f.battery_width, width, f.show_battery_percent != 0);
    }
    if f.clock_mode != 0 && f.clock_available != 0 && f.clock_valid != 0 {
        let width = run.width(f.font, &f.clock);
        let x = layout.clock(width, f.clock_mode);
        run.draw(f.font, x, layout.text_y, &f.clock);
    }
    if f.text_lane() && v.bookmarked != 0 {
        let x = layout.bookmark();
        let bits = slice::from_raw_parts(f.bookmark, 28);
        shapes(&target.glyphs, |s| {
            for y in 0..14 {
                for col in 0..16 {
                    s.pixel(
                        x + col,
                        layout.text_y + 5 + y,
                        bits[(y * 2 + col / 8) as usize] & (1 << (7 - col % 8)) != 0,
                    );
                }
            }
        });
    }
    title(&run, f, &layout, v.title, v.y_offset);
}
unsafe fn title(run: &Run<'_>, f: &Facts, layout: &Layout, text: Text, y_offset: i32) {
    let text = text.bytes().unwrap_or_default();
    let text = &text[..text.iter().position(|&b| b == 0).unwrap_or(text.len())];
    if text.is_empty() {
        return;
    }
    let mut scratch: SmallZ = SmallZ::new();
    let Some(capacity) = text.len().checked_add(if text.len() > 15 { 3 } else { 0 }) else {
        return;
    };
    if scratch
        .try_reserve(capacity)
        .and_then(|_| scratch.write_joined(&[text]))
        .is_err()
    {
        return;
    }
    let mut width = run.width(f.font, scratch.as_z());
    let (margin, available) = layout.title_space(f, width);
    if width > available {
        let Some(capacity) = text.len().checked_add(3) else {
            return;
        };
        if scratch
            .try_reserve(if available > 0 { capacity } else { 0 })
            .and_then(|_| {
                truncate_joined(&mut scratch, &[], text, available, |bytes| {
                    run.width(f.font, bytes)
                })
            })
            .is_err()
        {
            return;
        }
        width = run.width(f.font, scratch.as_z());
    }
    run.draw(
        f.font,
        margin + f.horizontal_margin + f.margin_left + (available - width) / 2,
        layout.text_y - y_offset,
        scratch.as_z(),
    );
}
fn battery(s: &mut Surface<'_>, f: &Facts, x: i32, y: i32) {
    let (w, h) = (f.battery_width, f.battery_height);
    for (dx, dy, width, height) in [
        (1, 0, w - 3, 1),
        (1, h - 1, w - 3, 1),
        (0, 1, 1, h - 2),
        (w - 2, 1, 1, h - 2),
        (w, 4, 1, h - 8),
    ] {
        s.fill(x + dx, y + dy, width, height, true);
    }
    s.pixel(x + w - 1, y + 3, true);
    s.pixel(x + w - 1, y + h - 4, true);
    if f.battery_style != 0 {
        if f.charging != 0 {
            s.fill(x + 2, y + 2, w - 5, h - 4, true);
        } else {
            for (threshold, dx) in [(10, 2), (40, 6), (70, 10)] {
                if f.battery_percent > threshold {
                    s.fill(x + dx, y + 2, 3, h - 4, true);
                }
            }
        }
    } else {
        if w <= 5 || h <= 4 {
            return;
        }
        let mut width = (i32::from(f.battery_percent) * (w - 5) / 100 + 1).min(w - 5);
        if f.charging != 0 {
            width = width.max(8.min(w - 5));
        }
        s.fill(x + 2, y + 2, width, h - 4, true);
    }
    if f.charging != 0 {
        for (dy, (dx, width)) in [
            (4, 2),
            (3, 2),
            (2, 4),
            (3, 2),
            (2, 2),
            (1, 4),
            (2, 2),
            (1, 2),
        ]
        .into_iter()
        .enumerate()
        {
            s.fill(x + 4 + dx, y + 2 + dy as i32, width, 1, false);
        }
    }
}

#[cfg(feature = "app-test")]
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_reader_status(
    facts: *const Facts,
    value: *const XpEpubStatus,
) {
    paint(&*facts, &*value);
}
