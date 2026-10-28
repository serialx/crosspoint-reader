use crate::{
    draw::{self, Sink},
    scene::*,
};
use xp_core::glyph::{Clip, GlyphTarget};
use xp_core::strip::Strip;

fn screen() -> Rect {
    Rect {
        x: 0,
        y: 0,
        w: 32,
        h: 24,
    }
}
struct Counter(usize);
impl Sink for Counter {
    fn resource_valid(&mut self, _: u32, generation: u32) -> bool {
        generation == 7
    }
    fn paint(&mut self, _: &Command, _: Rect, _: &[u8]) {
        self.0 += 1;
    }
}
#[test]
fn overflow_never_produces_a_scene() {
    let mut commands = [Command::default(); 1];
    let mut text = [0; 4];
    let mut hits = [Interaction::default(); 1];
    let mut arena = Arena::new(&mut commands, &mut text, &mut hits).unwrap();
    arena.push(Command::default()).unwrap();
    assert_eq!(arena.push(Command::default()), Err(Error::Capacity));
    assert!(arena.finish(Refresh::Fast, 7).is_err());
    let mut arena = Arena::new(&mut commands, &mut text, &mut hits).unwrap();
    assert_eq!(
        arena.text(Command::default(), b"four"),
        Err(Error::Capacity)
    );
    assert!(arena.finish(Refresh::Fast, 7).is_err());
    let mut arena = Arena::new(&mut commands, &mut text, &mut hits).unwrap();
    arena
        .hit(Interaction {
            generation: 7,
            ..Interaction::default()
        })
        .unwrap();
    assert_eq!(arena.hit(Interaction::default()), Err(Error::Capacity));
    assert!(arena.finish(Refresh::Fast, 7).is_err());
}
#[test]
fn late_invalid_command_cannot_partially_paint() {
    let mut commands = [Command::default(); 2];
    commands[1].kind = Kind::Pop as u8;
    let scene = Scene {
        commands: &commands,
        text: &[],
        interactions: &[],
        generation: 7,
        refresh: Refresh::Fast,
    };
    let mut counter = Counter(0);
    assert_eq!(
        draw::render(&scene, screen(), &mut counter),
        Err(Error::Invalid)
    );
    assert_eq!(counter.0, 0);
    let commands = [
        Command::default(),
        Command {
            kind: Kind::Bitmap as u8,
            generation: 6,
            ..Command::default()
        },
    ];
    let scene = Scene {
        commands: &commands,
        ..scene
    };
    assert_eq!(
        draw::render(&scene, screen(), &mut counter),
        Err(Error::StaleResource)
    );
    assert_eq!(counter.0, 0);
}
#[test]
fn translated_overflow_cannot_partially_paint() {
    let commands = [
        Command::default(),
        Command {
            kind: Kind::Translate as u8,
            rect: Rect {
                x: 32767,
                ..Rect::default()
            },
            ..Command::default()
        },
        Command {
            rect: Rect {
                x: 1,
                ..Rect::default()
            },
            ..Command::default()
        },
        Command {
            kind: Kind::Pop as u8,
            ..Command::default()
        },
    ];
    let scene = Scene {
        commands: &commands,
        text: &[],
        interactions: &[],
        generation: 0,
        refresh: Refresh::Fast,
    };
    let mut counter = Counter(0);
    assert_eq!(
        draw::render(&scene, screen(), &mut counter),
        Err(Error::Invalid)
    );
    assert_eq!(counter.0, 0);
}
#[test]
fn damage_overflow_and_hit_order() {
    let mut damage = Damage::default();
    for _ in 0..9 {
        damage.add(screen());
    }
    assert_eq!(damage.full, 1);
    assert_eq!(damage.count, 0);
    let hits = [
        Interaction {
            rect: screen(),
            action: 1,
            inputs: 1,
            ..Interaction::default()
        },
        Interaction {
            rect: screen(),
            action: 2,
            inputs: 1,
            ..Interaction::default()
        },
    ];
    let scene = Scene {
        commands: &[],
        text: &[],
        interactions: &hits,
        refresh: Refresh::Fast,
        generation: 0,
    };
    assert_eq!(scene.hit(4, 4, 1).unwrap().action, 2);
    assert_eq!(scene.hit(32, 4, 1), None);
    assert_eq!(scene.hit(4, 4, 2), None);
}
fn target(bytes: &mut [u8], orientation: u8, y: i32, rows: i32) -> GlyphTarget {
    GlyphTarget {
        buffer: bytes.as_mut_ptr(),
        buffer_len: bytes.len() as u32,
        width: 32,
        height: 24,
        stride: 4,
        origin_y: y,
        rows,
        clip: Clip {
            left: 0,
            top: 0,
            right: if orientation & 1 == 0 { 24 } else { 32 },
            bottom: if orientation & 1 == 0 { 32 } else { 24 },
        },
        orientation,
        plane: 0,
        absolute: 0,
        strip: u8::from(rows != 24),
        scanning: 0,
    }
}
#[test]
fn geometry_full_frame_matches_every_strip_orientation() {
    let commands = [
        Command {
            rect: Rect {
                x: -2,
                y: 1,
                w: 23,
                h: 17,
            },
            radius: 6,
            paint: 2,
            ..Command::default()
        },
        Command {
            kind: Kind::Stroke as u8,
            rect: Rect {
                x: 3,
                y: 7,
                w: 19,
                h: 12,
            },
            width: 2,
            ..Command::default()
        },
        Command {
            kind: Kind::Line as u8,
            rect: Rect {
                x: 1,
                y: 1,
                w: 15,
                h: 21,
            },
            ..Command::default()
        },
    ];
    for orientation in 0..4 {
        let mut full = [255; 96];
        let t = target(&mut full, orientation, 0, 24);
        for command in &commands {
            draw::geometry(
                &mut Strip::new(target(&mut full, orientation, 0, 24), &mut full).unwrap(),
                command,
            );
        }
        assert!(t.valid());
        for rows in [1, 3, 8, 24] {
            let mut assembled = [255; 96];
            for y in (0..24).step_by(rows) {
                let mut bytes = [255; 96];
                let length = rows * 4;
                let t = target(&mut bytes[..length], orientation, y as i32, rows as i32);
                let mut strip = Strip::new(t, &mut bytes[..length]).unwrap();
                for command in &commands {
                    draw::geometry(&mut strip, command);
                }
                assembled[y * 4..(y + rows) * 4].copy_from_slice(&bytes[..length]);
            }
            assert_eq!(full, assembled);
        }
    }
}

#[test]
fn frozen_cpp_widget_pixels() {
    let golden = include_bytes!("../../../../test/ui_runtime/geometry.golden");
    let mut offset = 0;
    for orientation in 0..4 {
        for radius in [0, 1, 6, 12] {
            for paint in 0..4 {
                let mut pixels = [255; 96];
                let t = target(&mut pixels, orientation, 0, 24);
                let mut strip = Strip::new(t, &mut pixels).unwrap();
                for command in [
                    Command {
                        rect: Rect {
                            x: -2,
                            y: 1,
                            w: 23,
                            h: 17,
                        },
                        radius,
                        paint,
                        ..Command::default()
                    },
                    Command {
                        kind: Kind::Stroke as u8,
                        rect: Rect {
                            x: 3,
                            y: 7,
                            w: 19,
                            h: 12,
                        },
                        radius,
                        width: 2,
                        ..Command::default()
                    },
                    Command {
                        kind: Kind::Line as u8,
                        rect: Rect {
                            x: 1,
                            y: 1,
                            w: 15,
                            h: 21,
                        },
                        ..Command::default()
                    },
                ] {
                    draw::geometry(&mut strip, &command);
                }
                assert_eq!(
                    &pixels,
                    &golden[offset..offset + 96],
                    "orientation={orientation} radius={radius} paint={paint}"
                );
                offset += 96;
            }
        }
    }
}

#[test]
fn frozen_cpp_list_geometry_and_origins() {
    extern crate std;
    use core::fmt::Write;
    let mut actual = std::string::String::new();
    for width in [480, 528, 792, 800] {
        for selection in [0, 2, 11] {
            for selection_style in 0..4 {
                writeln!(actual, "case {width} {selection} {selection_style}").unwrap();
                let mut commands = [Command::default(); 64];
                let mut hits = [Interaction::default(); 24];
                let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                let next = crate::list::view(
                    &mut arena,
                    crate::model::ListState::new(selection),
                    crate::list::Style {
                        area: Rect {
                            x: 0,
                            y: 0,
                            w: width,
                            h: 240,
                        },
                        screen: Rect {
                            x: 0,
                            y: 0,
                            w: width,
                            h: 240,
                        },
                        min_touch: 44,
                        font: 1,
                        row_height: 32,
                        row_gap: 3,
                        inset: 5,
                        padding: 8,
                        scroll_width: 3,
                        radius: 12,
                        selection: selection_style,
                        ..crate::list::Style::default()
                    },
                    12,
                    0,
                )
                .unwrap();
                let scene = arena.finish(Refresh::Fast, 0).unwrap();
                for command in scene.commands {
                    let r = command.rect;
                    match Kind::from_byte(command.kind).unwrap() {
                        Kind::Fill => {
                            let color = [4, 1, 2, 3][command.paint as usize];
                            let paint = if command.paint >= 2 { 2 } else { 1 };
                            writeln!(
                                actual,
                                "fill {} {} {} {} {} {} {} 15",
                                r.x, r.y, r.w, r.h, paint, color, command.radius
                            )
                            .unwrap();
                        }
                        Kind::TextHandle => {
                            let text = if command.resource == 2 {
                                "A long chapter title that wraps at the right edge of the list"
                            } else {
                                "Chapter"
                            };
                            let color = [4, 1, 2, 3][command.paint as usize];
                            writeln!(
                                actual,
                                "text {} {} {} {} 1 0 {} 1 0 0 {}",
                                r.x, r.y, r.w, r.h, color, text
                            )
                            .unwrap();
                            if text.len() * 10 <= r.w as usize {
                                writeln!(
                                    actual,
                                    "origin {} {}",
                                    r.x,
                                    r.y + ((r.h - 24) / 2).max(0)
                                )
                                .unwrap();
                            }
                        }
                        Kind::Clip => {
                            write!(
                                actual,
                                "clip {} {} {} {}",
                                r.x,
                                r.y,
                                r.w.min(528 - r.x),
                                r.h.min(792 - r.y)
                            )
                            .unwrap();
                        }
                        Kind::Pop => {
                            write!(actual, "clip 0 0 528 792").unwrap();
                        }
                        Kind::Triangle => {
                            writeln!(
                                actual,
                                "triangle {} {} {} {} {} {} 4",
                                r.x,
                                r.y,
                                r.x,
                                r.y + r.h,
                                r.x + r.w,
                                r.y + r.h / 2
                            )
                            .unwrap();
                        }
                        _ => panic!("unexpected command"),
                    }
                }
                writeln!(
                    actual,
                    "nav {} {} {} {}",
                    next.selected,
                    next.top,
                    next.drawn,
                    next.page_rows()
                )
                .unwrap();
                for hit in scene.interactions {
                    let r = hit.rect;
                    writeln!(
                        actual,
                        "hit {} {} {} {} {} {}",
                        r.x, r.y, r.w, r.h, hit.action, hit.value
                    )
                    .unwrap();
                }
            }
        }
    }
    let expected = include_str!("../../../../test/ui_runtime/layout.golden");
    assert_eq!(actual.lines().count(), expected.lines().count());
    for (index, (a, b)) in actual.lines().zip(expected.lines()).enumerate() {
        assert_eq!(a, b, "line {index}");
    }
}

#[test]
fn list_edges_repeat_scroll_and_cancel() {
    use crate::model::{EffectKind, Input, ListState};
    let mut state = ListState::new(0);
    state.drawn = 4;
    state.update(
        Input {
            pressed: 4,
            held: 4,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(state.selected, 1);
    state.update(
        Input {
            held: 4,
            held_ms: 501,
            now_ms: 501,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(state.selected, 4);
    state.update(
        Input {
            held: 4,
            held_ms: 900,
            now_ms: 900,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(state.selected, 4);
    state.update(
        Input {
            released: 4,
            held_ms: 1001,
            now_ms: 1001,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(state.selected, 4);
    assert_eq!(state.last_repeat, 0);
    let effect = state.update(
        Input {
            swipe: 1,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(effect.kind, EffectKind::Scroll as u8);
    assert_eq!(state.selected, 4);
    assert_eq!(state.pending_scroll, 4);
    let effect = state.update(
        Input {
            released: 1 | 2,
            ..Input::default()
        },
        10,
        false,
    );
    assert_eq!(effect.kind, EffectKind::Cancel as u8);
    let effect = state.update(
        Input {
            released: 2,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(effect.kind, EffectKind::Activate as u8);
    assert_eq!(effect.value, 4);
    let effect = state.update(
        Input {
            touch_flags: 2,
            action: 1,
            value: 8,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(effect.kind, EffectKind::Activate as u8);
    assert_eq!(effect.value, 8);
    let effect = state.update(
        Input {
            touch_flags: 2,
            action: 1,
            value: 99,
            ..Input::default()
        },
        10,
        true,
    );
    assert_eq!(effect.kind, EffectKind::None as u8);
}

#[test]
fn frozen_cpp_message_origins_and_ffi_failure() {
    extern crate std;
    use core::ffi::c_void;
    use std::{fmt::Write, string::String};
    let mut actual = String::new();
    for width in [480, 528, 792, 800] {
        for text_width in [0, 73, 900] {
            for style in 0..4 {
                let mut commands = [Command::default(); 2];
                let metrics = crate::ffi::Metrics {
                    context: core::ptr::from_ref(&text_width).cast_mut().cast::<c_void>(),
                    width: Some(width_callback),
                    line_height: Some(height_callback),
                };
                unsafe extern "C" fn width_callback(
                    p: *mut c_void,
                    _: i32,
                    _: *const u8,
                    _: u16,
                    _: u8,
                ) -> i32 {
                    unsafe { *p.cast::<i32>() }
                }
                unsafe extern "C" fn height_callback(_: *mut c_void, _: i32) -> i32 {
                    23
                }
                let screen = Rect {
                    x: 0,
                    y: 0,
                    w: width,
                    h: if width == 528 { 792 } else { 480 },
                };
                assert_eq!(
                    unsafe {
                        crate::ffi::xp_ui_message_view(
                            &metrics,
                            screen,
                            0,
                            style,
                            c"hello".as_ptr().cast(),
                            5,
                            commands.as_mut_ptr(),
                            1,
                        )
                    },
                    0
                );
                assert_eq!(commands, [Command::default(); 2]);
                assert_eq!(
                    unsafe {
                        crate::ffi::xp_ui_message_view(
                            &metrics,
                            screen,
                            0,
                            style,
                            c"hello".as_ptr().cast(),
                            5,
                            commands.as_mut_ptr(),
                            2,
                        )
                    },
                    2
                );
                assert_eq!(commands[0].kind, Kind::Clear as u8);
                writeln!(
                    actual,
                    "case {width} {text_width} {style}\nclear\ntext {} {} {style}\nrefresh 1",
                    commands[1].rect.x, commands[1].rect.y
                )
                .unwrap();
            }
        }
    }
    assert_eq!(
        actual,
        include_str!("../../../../test/ui_runtime/message.golden")
    );
}

#[test]
fn invalid_overlay_keeps_frozen_parent_unpainted() {
    let commands = [Command {
        rect: screen(),
        ..Command::default()
    }];
    let parent = Scene {
        commands: &commands,
        text: &[],
        interactions: &[],
        refresh: Refresh::None,
        generation: 7,
    };
    let bad = [Command {
        kind: Kind::Page as u8,
        generation: 6,
        ..Command::default()
    }];
    let overlay = Scene {
        commands: &bad,
        ..parent
    };
    let mut sink = Counter(0);
    assert_eq!(
        draw::render_layers(&[&parent, &overlay], screen(), &mut sink),
        Err(Error::StaleResource)
    );
    assert_eq!(sink.0, 0);
    assert_eq!(
        draw::render_layers(&[&parent, &parent], screen(), &mut sink),
        Ok(())
    );
    assert_eq!(sink.0, 2);
    assert_eq!(
        draw::render_layers(&[&parent; 5], screen(), &mut sink),
        Err(Error::Capacity)
    );
    assert_eq!(sink.0, 2);
}

#[test]
fn frozen_cpp_top_corner_pixels() {
    let golden = include_bytes!("../../../../test/ui_runtime/top-corners.golden");
    let mut offset = 0;
    for orientation in 0..4 {
        for radius in [0, 1, 6, 12] {
            for paint in 0..4 {
                let mut pixels = [255; 96];
                let t = target(&mut pixels, orientation, 0, 24);
                let mut strip = Strip::new(t, &mut pixels).unwrap();
                for command in [
                    Command {
                        rect: Rect {
                            x: -2,
                            y: 1,
                            w: 23,
                            h: 17,
                        },
                        radius,
                        paint,
                        ..Command::default()
                    },
                    Command {
                        kind: Kind::Stroke as u8,
                        rect: Rect {
                            x: 3,
                            y: 7,
                            w: 19,
                            h: 12,
                        },
                        radius,
                        width: 2,
                        corners: 3,
                        ..Command::default()
                    },
                    Command {
                        kind: Kind::Line as u8,
                        rect: Rect {
                            x: 1,
                            y: 1,
                            w: 15,
                            h: 21,
                        },
                        ..Command::default()
                    },
                ] {
                    draw::geometry(&mut strip, &command);
                }
                assert_eq!(
                    &pixels,
                    &golden[offset..offset + 96],
                    "orientation={orientation} radius={radius} paint={paint}"
                );
                offset += 96;
            }
        }
    }
}
