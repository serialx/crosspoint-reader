extern crate alloc;
use super::*;
use crate::image::frame::{Target, Writer};
use alloc::vec;

#[allow(clippy::too_many_arguments)]
fn descriptor(
    bytes: &mut [u8],
    w: i32,
    h: i32,
    orientation: u8,
    plane: u8,
    absolute: u8,
    origin: i32,
    rows: i32,
) -> GlyphTarget {
    GlyphTarget {
        buffer: bytes.as_mut_ptr(),
        buffer_len: bytes.len() as u32,
        width: w,
        height: h,
        stride: (w + 7) / 8,
        origin_y: origin,
        rows,
        clip: Clip {
            left: 0,
            top: 0,
            right: if orientation & 1 == 0 { h } else { w },
            bottom: if orientation & 1 == 0 { w } else { h },
        },
        orientation,
        plane,
        absolute,
        strip: 1,
        scanning: 0,
    }
}
#[test]
fn packed_cache_matches_current_writer_all_planes_and_bands() {
    for (w, h) in [(792, 528), (800, 480), (17, 13)] {
        for orientation in 0..4 {
            for plane in 0..3 {
                for absolute in 0..2 {
                    let clear = if plane == 0 || absolute != 0 { 255 } else { 0 };
                    let stride = (w + 7) / 8;
                    let mut expected = vec![clear; (stride * h) as usize];
                    let target = Target {
                        data: expected.as_mut_ptr(),
                        len: expected.len(),
                        stride,
                        origin: 0,
                        rows: h,
                        width: w,
                        height: h,
                        mode: plane,
                        orientation,
                        absolute,
                    };
                    let mut writer = unsafe { Writer::new(target) };
                    let (lw, lh) = if orientation & 1 == 0 { (h, w) } else { (w, h) };
                    let row: alloc::vec::Vec<_> =
                        (0..(lw + 3) / 4).map(|i| ((i * 19) ^ 0x1b) as u8).collect();
                    for y in 0..lh {
                        writer.packed_row(0, y, lw, &row);
                    }
                    for band in [1, 8, 16, 32, 37, 48, 64, h] {
                        let mut assembled = vec![0; expected.len()];
                        for origin in (0..h).step_by(band as usize) {
                            let rows = band.min(h - origin);
                            let mut bytes = vec![clear; (stride * rows) as usize];
                            let target = descriptor(
                                &mut bytes,
                                w,
                                h,
                                orientation,
                                plane,
                                absolute,
                                origin,
                                rows,
                            );
                            let mut strip = Strip::new(target, &mut bytes).unwrap();
                            for y in 0..lh {
                                assert!(strip.packed_row(0, y, lw as u16, &row));
                            }
                            assembled
                                [(origin * stride) as usize..((origin + rows) * stride) as usize]
                                .copy_from_slice(&bytes);
                        }
                        assert_eq!(
                            assembled, expected,
                            "w={w} o={orientation} p={plane} a={absolute} n={band}"
                        );
                    }
                }
            }
        }
    }
}
#[test]
fn fast_fills_equal_pixel_iteration_with_clipping_and_short_input() {
    use embedded_graphics_core::primitives::PointsIter;
    for orientation in 0..4 {
        for plane in 0..3 {
            for absolute in 0..2 {
                for count in [0, 1, 33, 400] {
                    let mut a = vec![0x5a; 21];
                    let mut b = a.clone();
                    let ta = descriptor(&mut a, 19, 17, orientation, plane, absolute, 5, 7);
                    let tb = descriptor(&mut b, 19, 17, orientation, plane, absolute, 5, 7);
                    let area = Rectangle::new(Point::new(-2, -3), Size::new(21, 24));
                    let colors = (0..count).map(|v| Gray2::new(((v / 7) % 4) as u8));
                    let mut sa = GrayStrip(Strip::new(ta, &mut a).unwrap());
                    let mut sb = GrayStrip(Strip::new(tb, &mut b).unwrap());
                    sa.fill_contiguous(&area, colors.clone()).unwrap();
                    sb.draw_iter(area.points().zip(colors).map(|(p, c)| Pixel(p, c)))
                        .unwrap();
                    assert_eq!(a, b, "o={orientation} p={plane} a={absolute} count={count}");
                }
                for color in [BinaryColor::Off, BinaryColor::On] {
                    let mut a = vec![0x5a; 21];
                    let mut b = a.clone();
                    let ta = descriptor(&mut a, 19, 17, orientation, plane, absolute, 5, 7);
                    let tb = descriptor(&mut b, 19, 17, orientation, plane, absolute, 5, 7);
                    let area = Rectangle::new(Point::new(-2, 3), Size::new(21, 20));
                    Strip::new(ta, &mut a)
                        .unwrap()
                        .fill_solid(&area, color)
                        .unwrap();
                    Strip::new(tb, &mut b)
                        .unwrap()
                        .draw_iter(area.points().map(|p| Pixel(p, color)))
                        .unwrap();
                    assert_eq!(a, b);
                }
            }
        }
    }
}
#[test]
fn invalid_storage_and_extreme_offscreen_coordinates_do_not_write() {
    let mut bytes = [0x5a; 21];
    let mut t = descriptor(&mut bytes, 19, 17, 0, 0, 0, 5, 7);
    t.rows = 8;
    assert!(Strip::new(t, &mut bytes).is_none());
    let t = descriptor(&mut bytes, 19, 17, 0, 0, 0, 5, 7);
    let mut strip = Strip::new(t, &mut bytes).unwrap();
    for point in [
        Point::new(i32::MAX, i32::MAX),
        Point::new(i32::MIN, i32::MIN),
    ] {
        strip.draw_iter([Pixel(point, BinaryColor::Off)]).unwrap();
        strip
            .fill_solid(&Rectangle::new(point, Size::new(20, 20)), BinaryColor::Off)
            .unwrap();
        strip
            .fill_contiguous(
                &Rectangle::new(point, Size::new(20, 20)),
                [BinaryColor::Off; 32],
            )
            .unwrap();
        assert!(strip.packed_row(point.x, point.y, 4, &[0]));
    }
    assert_eq!(bytes, [0x5a; 21]);
}
