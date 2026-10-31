use std::io::Write;
use xp_core::glyph::{Clip, GlyphTarget, Surface};
use xp_ui::{
    dictionary::{
        paint,
        selection::{Layout, Painted, State, Word},
    },
    scene::Rect,
};
unsafe extern "C" fn glyph(
    _: *mut core::ffi::c_void,
    target: *const GlyphTarget,
    _: *const Word,
    x: i32,
    y: i32,
) {
    let t = unsafe { &*target };
    let bytes = unsafe { core::slice::from_raw_parts_mut(t.buffer, t.buffer_len as usize) };
    let mut s = Surface::new(t, bytes).unwrap();
    for j in 1..6 {
        for i in 1..12 {
            if (i + j) % 3 != 0 {
                s.fill(x + i, y + j, 1, 1, true);
            }
        }
    }
}
fn main() {
    let band_rows: usize = std::env::args().nth(1).map_or(528, |v| v.parse().unwrap());
    for count in [240, 720] {
        for orientation in 0..4 {
            let words: Vec<_> = (0..count)
                .map(|i| Word {
                    x: (5 + i % 8 * 60) as i16,
                    y: (10 + (i / 8 % 30) * 24) as i16,
                    width: (24 + i % 3 * 6) as i16,
                    height: 18,
                    row: (i / 8) as u16,
                    pageOffset: (i / 240) as u8,
                    text: c"word".as_ptr(),
                    ..unsafe { core::mem::zeroed() }
                })
                .collect();
            let (width, height) = if orientation % 2 == 0 {
                (528, 792)
            } else {
                (792, 528)
            };
            let layout = Layout {
                screen: Rect {
                    x: 0,
                    y: 0,
                    w: width,
                    h: height,
                },
                safe: Rect {
                    x: 0,
                    y: 0,
                    w: width,
                    h: height,
                },
                ..Default::default()
            };
            let mut state = State::init(&words, 0, width.into(), -1, 0);
            for page in 0..count / 240 {
                state.page = page;
                state.selected = page * 240 + 100;
                state.range_start = page * 240 + 10;
                let mut bytes = vec![255u8; 52272];
                let mut old = Painted::default();
                for selected in [100, 110, 90, 10, 0, 5] {
                    state.selected = page * 240 + selected;
                    let mode = paint::incremental(&state, &words, &layout, &old);
                    for origin in (0..528).step_by(band_rows) {
                        let rows = band_rows.min(528 - origin);
                        let band = &mut bytes[origin * 99..(origin + rows) * 99];
                        let target = GlyphTarget {
                            buffer: band.as_mut_ptr(),
                            buffer_len: band.len() as u32,
                            width: 792,
                            height: 528,
                            stride: 99,
                            origin_y: origin as i32,
                            rows: rows as i32,
                            clip: Clip {
                                left: 0,
                                top: 0,
                                right: width.into(),
                                bottom: height.into(),
                            },
                            orientation,
                            plane: 0,
                            absolute: 0,
                            strip: u8::from(band_rows != 528),
                            scanning: 0,
                        };
                        let fonts = paint::Fonts {
                            context: core::ptr::null_mut(),
                            word: Some(glyph),
                        };
                        unsafe {
                            paint::paint(&state, &words, &layout, &old, &target, fonts, mode)
                        }
                        .unwrap();
                    }
                    std::io::stdout().write_all(&bytes).unwrap();
                    old = Painted {
                        selected: state.selected,
                        range_start: state.range_start,
                        page: state.page,
                        valid: 1,
                        ..Default::default()
                    };
                }
            }
        }
    }
}
