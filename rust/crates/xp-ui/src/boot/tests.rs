extern crate std;
use super::*;
use crate::scene::{Arena, Command, Kind, Rect, Refresh};
use std::{format, string::String, vec::Vec};
const GOLDEN: &str = include_str!("../../../../../test/boot_ui/policy.golden");
#[test]
fn original_xtc_navigation() {
    for line in GOLDEN.lines() {
        let v: Vec<&str> = line.split_whitespace().collect();
        if v[0] == "skip" || v[0] == "turn" {
            let count = v[1].parse().unwrap();
            let mut s = XtcState {
                page: v[2].parse().unwrap(),
            };
            let amount = if v[0] == "skip" {
                v[3].parse().unwrap()
            } else if v[3] == "1" {
                1
            } else {
                -1
            };
            assert_eq!(s.turn(count, amount, v[0] == "skip"), v[4] == "1", "{line}");
            assert_eq!(s.page, v[5].parse::<u32>().unwrap(), "{line}");
            if v[0] == "skip" {
                assert_eq!(s.page >= count, v[6] == "1", "{line}");
            }
        }
    }
}
#[test]
fn original_bmp_input_precedence() {
    let mut lines = GOLDEN.lines().peekable();
    while let Some(line) = lines.next() {
        if !line.starts_with("bmp ") {
            continue;
        }
        let v: Vec<i32> = line
            .split_whitespace()
            .skip(1)
            .map(|s| s.parse().unwrap())
            .collect();
        let mut s = ImageState {
            selected: v[0],
            count: 3,
            can_set_cover: 1,
            reserved: [0; 3],
        };
        let mut released = 0;
        for (n, bit) in [BACK, CONFIRM, LEFT, UP, RIGHT, DOWN].iter().enumerate() {
            if v[1] & (1 << n) != 0 {
                released |= bit;
            }
        }
        let i = Input {
            released,
            swipe: match v[2] {
                1 => SWIPE_LEFT,
                2 => SWIPE_RIGHT,
                _ => 0,
            },
            ..Input::default()
        };
        let e = image_update(&mut s, &i);
        let mut actual = String::new();
        match e.kind {
            1 => actual.push_str("browser\n"),
            2 => actual.push_str("confirmation\nsave-cover\n"),
            3 => actual.push_str(&format!("open {}\n", e.value)),
            _ => {}
        }
        let mut expected = String::new();
        while lines.peek().is_some_and(|l| !l.starts_with("bmp ")) {
            expected.push_str(lines.next().unwrap());
            expected.push('\n');
        }
        assert_eq!(actual, expected, "{line}");
    }
}
fn route_trace(route: u8, mode: u8) -> String {
    match route {
        0=>format!("clear\nimage 1 204 336 120 120\ncenter 10 466 1 CrossPoint\ncenter 5 491 0 Sleeping\n{}refresh 1\n",if mode==1 {""} else {"invert-frame\n"}),
        1=>"custom\n".into(),2=>"cover\n".into(),3=>"overlay\n".into(),4=>"clear\nrefresh 1\n".into(),
        // The last screen loop leaves the X3 stub selected before routing cases.
        5=>"image 2 0 744 48 48\ngray-base 0\n".into(),_=>panic!()
    }
}
#[test]
fn original_sleep_routes_and_polarity() {
    let mut lines = GOLDEN.lines().peekable();
    while let Some(line) = lines.next() {
        if !line.starts_with("sleep ") {
            continue;
        }
        let v: Vec<u8> = line
            .split_whitespace()
            .skip(1)
            .map(|s| s.parse().unwrap())
            .collect();
        let p = sleep_plan(v[0], v[1] != 0, v[2] != 0, v[3] != 0, v[4] != 0);
        let mut actual = String::new();
        if p.normalize_polarity != 0 {
            actual.push_str("polarity 0\n");
        }
        if p.invert_frame != 0 {
            actual.push_str("invert-frame\n");
        }
        if p.route != 5 {
            if v[3] != 0 {
                actual.push_str("orientation 0\n");
            }
            actual.push_str(if p.preserve_frame != 0 {
                "preserved-popup\n"
            } else {
                "popup Entering sleep\n"
            });
            if v[3] != 0 {
                actual.push_str("portrait\n");
            }
            if p.release_fonts != 0 {
                actual.push_str("release-fonts\n");
            }
        }
        actual.push_str(&route_trace(p.route, v[0]));
        let mut expected = String::new();
        while lines
            .peek()
            .is_some_and(|l| !l.starts_with("sleep ") && !l.starts_with("crash "))
        {
            expected.push_str(lines.next().unwrap());
            expected.push('\n');
        }
        assert_eq!(actual, expected, "{line}");
    }
}
#[test]
fn boot_crash_and_image_scene_bounds() {
    for (w, h) in [(528, 792), (480, 800), (792, 528), (800, 480)] {
        for kind in 0..9 {
            let l = view::Layout {
                screen: Rect { x: 0, y: 0, w, h },
                title_font: 10,
                small_font: 5,
                error_font: 12,
                side_padding: 10,
                top_padding: 12,
                header_height: 40,
                spacing: 8,
                line_height: 23,
                view: kind,
                description_lines: 10,
                panic_lines: 5,
                ..view::Layout::default()
            };
            let mut commands = [Command::default(); 19];
            let mut arena = Arena::new(&mut commands, &mut [], &mut []).unwrap();
            view::view(&mut arena, &l, &mut |_, _, _| 73).unwrap();
            let scene = arena.finish(Refresh::None, 1).unwrap();
            assert_eq!(
                scene.commands.len(),
                [5, 4, 1, 1, 19, 3, 2, 1, 1][kind as usize]
            );
            if kind == 0 {
                assert_eq!(
                    scene.commands[1].rect,
                    Rect {
                        x: (w - 120) / 2,
                        y: (h - 120) / 2,
                        w: 120,
                        h: 120
                    }
                );
                assert_eq!(scene.commands[2].rect.x, (w - 73) / 2);
                assert_eq!(scene.commands[2].rect.y, h / 2 + 70);
                assert_eq!(scene.commands[4].rect.y, h - 30);
            }
            if kind == 4 {
                assert_eq!(scene.commands[12].resource, view::REASON);
                assert_eq!(scene.commands[12].rect.y, 12 + 40 + 8 + 10 * 23 + 2 * 8);
            }
        }
    }
}
#[test]
fn no_repeat_or_hold_actions_and_crash_press_only() {
    let mut s = ImageState {
        selected: 1,
        count: 3,
        can_set_cover: 0,
        reserved: [0; 3],
    };
    for held in [LEFT, RIGHT, UP, DOWN, CONFIRM] {
        assert_eq!(
            image_update(
                &mut s,
                &Input {
                    held,
                    held_ms: 99999,
                    ..Input::default()
                }
            )
            .kind,
            0
        );
    }
    assert_eq!(
        image_update(
            &mut s,
            &Input {
                released: CONFIRM,
                ..Input::default()
            }
        )
        .kind,
        0
    );
    assert_eq!(
        crash_update(&Input {
            released: BACK,
            ..Input::default()
        })
        .kind,
        0
    );
    assert_eq!(
        crash_update(&Input {
            pressed: BACK,
            ..Input::default()
        })
        .kind,
        1
    );
    assert_eq!(
        crash_update(&Input {
            action: 1,
            ..Input::default()
        })
        .kind,
        1
    );
}
#[test]
fn rejected_scene_leaves_output_unpublished() {
    use crate::ffi::{Buffer, SceneView};
    let mut commands = [Command::default(); 18];
    let b = Buffer {
        commands: commands.as_mut_ptr(),
        hits: core::ptr::null_mut(),
        command_capacity: 18,
        hit_capacity: 0,
    };
    let l = view::Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 528,
            h: 792,
        },
        view: 4,
        description_lines: 10,
        panic_lines: 5,
        ..view::Layout::default()
    };
    let m = ffi::Metrics {
        context: core::ptr::null_mut(),
        width: Some(width),
    };
    unsafe extern "C" fn width(_: *mut core::ffi::c_void, _: u32, _: i32, _: u8) -> i32 {
        73
    }
    let mut out = SceneView {
        commands: core::ptr::null(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 99,
        command_count: 0,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    assert_eq!(unsafe { ffi::view(&l, &m, &b, &mut out) }, 0);
    assert_eq!(out.generation, 99);
    assert!(commands.iter().any(|c| c.kind == Kind::Clear as u8));
}

struct GuardedAllocator;
std::thread_local! { static NO_ALLOC: core::cell::Cell<bool> = const { core::cell::Cell::new(false) }; }
unsafe impl std::alloc::GlobalAlloc for GuardedAllocator {
    unsafe fn alloc(&self, l: std::alloc::Layout) -> *mut u8 {
        if NO_ALLOC.try_with(|v| v.get()).unwrap_or(false) {
            return core::ptr::null_mut();
        }
        unsafe { std::alloc::System.alloc(l) }
    }
    unsafe fn dealloc(&self, p: *mut u8, l: std::alloc::Layout) {
        unsafe { std::alloc::System.dealloc(p, l) }
    }
}
#[global_allocator]
static ALLOCATOR: GuardedAllocator = GuardedAllocator;
#[test]
fn crash_scene_and_input_with_allocator_disabled() {
    let mut commands = [Command::default(); 19];
    let l = view::Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 528,
            h: 792,
        },
        view: 4,
        description_lines: 10,
        panic_lines: 5,
        line_height: 23,
        spacing: 8,
        ..view::Layout::default()
    };
    NO_ALLOC.with(|v| v.set(true));
    let mut arena = Arena::new(&mut commands, &mut [], &mut []).unwrap();
    let result = view::view(&mut arena, &l, &mut |_, _, _| 73);
    let finished = arena.finish(Refresh::Fast, 1).is_ok();
    let effect = crash_update(&Input {
        pressed: BACK,
        ..Input::default()
    });
    NO_ALLOC.with(|v| v.set(false));
    assert!(result.is_ok() && finished);
    assert_eq!(effect.kind, 1);
}
#[test]
fn original_bitmap_placement_float_bits() {
    for line in GOLDEN.lines().filter(|l| l.starts_with("placement ")) {
        let v: Vec<i64> = line
            .split_whitespace()
            .skip(1)
            .map(|s| s.parse().unwrap())
            .collect();
        let p = placement(
            v[0] as i32,
            v[1] as i32,
            v[2] as i32,
            v[3] as i32,
            v[4] != 0,
        );
        assert_eq!(
            [
                i64::from(p.x),
                i64::from(p.y),
                i64::from(p.crop_x.to_bits()),
                i64::from(p.crop_y.to_bits())
            ],
            v[5..],
            "{line}"
        );
    }
}
#[test]
fn original_xtc_pixel_planes_and_truncated_rejection() {
    use xp_core::glyph::{Clip, GlyphTarget};
    for line in GOLDEN.lines().filter(|l| l.starts_with("pixels ")) {
        let fields: Vec<&str> = line.split_whitespace().skip(1).collect();
        let w = fields[0].parse::<u16>().unwrap();
        let h = fields[1].parse::<u16>().unwrap();
        let depth = fields[2].parse::<u8>().unwrap();
        let pass = fields[3].parse::<u8>().unwrap();
        let size = if depth == 2 {
            usize::from(w) * usize::from(h).div_ceil(8) * 2
        } else {
            usize::from(w).div_ceil(8) * usize::from(h)
        };
        let data: Vec<u8> = (0..size).map(|i| (i * 73 + 41) as u8).collect();
        let stride = (i32::from(w) + 7) / 8;
        let mut buffer = std::vec![if pass==0 {255} else {0};(stride*i32::from(h)) as usize];
        let t = GlyphTarget {
            buffer: buffer.as_mut_ptr(),
            buffer_len: buffer.len() as u32,
            width: i32::from(w),
            height: i32::from(h),
            stride,
            origin_y: 0,
            rows: i32::from(h),
            clip: Clip {
                left: 0,
                top: 0,
                right: i32::from(w),
                bottom: i32::from(h),
            },
            orientation: 3,
            plane: 0,
            absolute: 0,
            strip: 0,
            scanning: 0,
        };
        assert!(xtc_pixels(
            &t,
            &mut buffer,
            &data,
            [w, h],
            depth,
            pass,
            &mut |_, _, _| {}
        ));
        let actual: String = (0..usize::from(h))
            .flat_map(|y| (0..usize::from(w)).map(move |x| (y, x)))
            .map(|(y, x)| {
                if buffer[y * stride as usize + x / 8] & (1 << (7 - x % 8)) == 0 {
                    '1'
                } else {
                    '0'
                }
            })
            .collect();
        assert_eq!(actual, fields[4], "{line}");
        let before = buffer.clone();
        assert!(!xtc_pixels(
            &t,
            &mut buffer,
            &data[..data.len() - 1],
            [w, h],
            depth,
            pass,
            &mut |_, _, _| {}
        ));
        assert_eq!(buffer, before);
    }
}
#[test]
fn xtc_pixels_match_across_strip_targets_and_orientations() {
    use xp_core::glyph::{Clip, GlyphTarget};
    for orientation in 0..4 {
        for depth in [1, 2] {
            for pass in 0..if depth == 2 { 3 } else { 1 } {
                let width = 40;
                let height = 32;
                let stride = 5;
                let (w, h) = if orientation & 1 == 0 {
                    (height, width)
                } else {
                    (width, height)
                };
                let bytes = if depth == 2 {
                    w * ((h + 7) / 8) * 2
                } else {
                    ((w + 7) / 8) * h
                };
                let data: Vec<u8> = (0..bytes).map(|n| (n * 73 + 41) as u8).collect();
                let mut full = std::vec![if pass==0 {255} else {0};(stride*height) as usize];
                let mut strips = full.clone();
                let mut target = GlyphTarget {
                    buffer: full.as_mut_ptr(),
                    buffer_len: full.len() as u32,
                    width,
                    height,
                    stride,
                    origin_y: 0,
                    rows: height,
                    clip: Clip {
                        left: 0,
                        top: 0,
                        right: w,
                        bottom: h,
                    },
                    orientation,
                    plane: 0,
                    absolute: 0,
                    strip: 0,
                    scanning: 0,
                };
                assert!(xtc_pixels(
                    &target,
                    &mut full,
                    &data,
                    [w as u16, h as u16],
                    depth,
                    pass,
                    &mut |_, _, _| {}
                ));
                for y in (0..height).step_by(7) {
                    let rows = 7.min(height - y);
                    let band = &mut strips[(y * stride) as usize..((y + rows) * stride) as usize];
                    target.buffer = band.as_mut_ptr();
                    target.buffer_len = band.len() as u32;
                    target.origin_y = y;
                    target.rows = rows;
                    target.strip = 1;
                    assert!(xtc_pixels(
                        &target,
                        band,
                        &data,
                        [w as u16, h as u16],
                        depth,
                        pass,
                        &mut |_, _, _| {}
                    ));
                }
                assert_eq!(
                    full, strips,
                    "orientation={orientation} depth={depth} pass={pass}"
                );
            }
        }
    }
}

#[test]
fn xtc_outside_diagnostics_preserve_order_and_clip_silence() {
    use xp_core::glyph::{Clip, GlyphTarget};
    for orientation in 0..4 {
        let (lw, lh): (usize, usize) = if orientation & 1 == 0 { (4, 8) } else { (8, 4) };
        let (w, h) = (lw + 2, lh + 2);
        for depth in [1, 2] {
            for pass in 0..if depth == 2 { 3 } else { 1 } {
                let plane = w * h.div_ceil(8);
                let mut bytes =
                    std::vec![0; if depth == 2 { plane * 2 } else { w.div_ceil(8) * h }];
                if depth == 2 {
                    bytes[plane..].fill(255);
                }
                let mut frame = [255; 4];
                let mut target = GlyphTarget {
                    buffer: frame.as_mut_ptr(),
                    buffer_len: 4,
                    width: 8,
                    height: 4,
                    stride: 1,
                    origin_y: 0,
                    rows: 4,
                    clip: Clip {
                        left: 0,
                        top: 0,
                        right: 100,
                        bottom: 100,
                    },
                    orientation,
                    plane: 0,
                    absolute: 0,
                    strip: 0,
                    scanning: 0,
                };
                let mut events = Vec::new();
                assert!(xtc_pixels(
                    &target,
                    &mut frame,
                    &bytes,
                    [w as u16, h as u16],
                    depth,
                    pass,
                    &mut |x, y, black| events.push((x, y, black))
                ));
                let expected: Vec<_> = (0..h)
                    .flat_map(|y| (0..w).map(move |x| (x, y)))
                    .filter(|&(x, y)| x >= lw || y >= lh)
                    .map(|(x, y)| (x as i32, y as i32, pass == 0))
                    .collect();
                assert_eq!(
                    events, expected,
                    "orientation={orientation} depth={depth} pass={pass}"
                );
                target.clip.right = lw as i32;
                target.clip.bottom = lh as i32;
                assert!(xtc_pixels(
                    &target,
                    &mut frame,
                    &bytes,
                    [w as u16, h as u16],
                    depth,
                    pass,
                    &mut |_, _, _| panic!("clipped pixels must stay silent")
                ));
            }
        }
    }
}
