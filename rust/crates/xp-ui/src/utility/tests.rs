extern crate std;
use super::*;
use crate::{model::*, scene::Interaction};
use std::{format, string::String};
#[test]
fn frozen_application_traces() {
    let mut actual = String::new();
    for edge in [false, true] {
        for button in 0..8 {
            let mut s = State {
                kind: Screen::Interval as u8,
                value: 20,
                maximum: 100,
                small_step: 1,
                large_step: 10,
                edge_buttons: u8::from(edge),
                ..State::default()
            };
            assert!(s.init());
            let (mut paints, mut done, mut result) = (0, 0, 0);
            for phase in 0..6 {
                let i = Input {
                    pressed: if phase == 0 { 1 << button } else { 0 },
                    released: if phase == 5 { 1 << button } else { 0 },
                    held: if phase < 5 { 1 << button } else { 0 },
                    held_ms: phase * 250,
                    now_ms: 1000 + phase * 250,
                    ..Input::default()
                };
                let e = s.update(&i, None, &Feedback::default());
                paints += i32::from(e.navigation.invalidate);
                if e.navigation.kind != 0 {
                    done += 1;
                    result = if e.navigation.kind == 1 {
                        -1
                    } else {
                        e.navigation.value
                    }
                };
                actual += &format!(
                    "interval {} {} {} {} {} {} {}\n",
                    u8::from(edge),
                    button,
                    phase,
                    s.value,
                    paints,
                    done,
                    result
                );
            }
        }
    }
    for own in [false, true] {
        for start in [1, 60, 100] {
            for button in [0, 1, 4, 5] {
                let mut s = State {
                    kind: Screen::Frontlight as u8,
                    brightness: start,
                    switch_owns_light: u8::from(own),
                    ..State::default()
                };
                assert!(s.init());
                let (mut paints, mut done, mut bw, mut ow) = (0, 0, 0, 0);
                for phase in 0..6 {
                    let i = Input {
                        pressed: if phase == 0 { 1 << button } else { 0 },
                        released: if phase == 5 { 1 << button } else { 0 },
                        held: if phase < 5 { 1 << button } else { 0 },
                        held_ms: phase * 250,
                        now_ms: 1000 + phase * 250,
                        ..Input::default()
                    };
                    let e = s.update(&i, None, &Feedback::default());
                    paints += i32::from(e.navigation.invalidate);
                    done += i32::from(e.navigation.kind != 0);
                    bw += i32::from(e.writes & WRITE_BRIGHTNESS != 0);
                    ow += i32::from(e.writes & WRITE_LIGHT != 0);
                    actual += &format!(
                        "light {} {} {} {} {} {} {} {} {} {} {}\n",
                        u8::from(own),
                        start,
                        button,
                        phase,
                        s.brightness,
                        s.light_on,
                        s.light_changed,
                        paints,
                        done,
                        bw,
                        ow
                    );
                }
            }
        }
    }
    for tile in 0..4 {
        let mut s = State {
            kind: Screen::Frontlight as u8,
            orientation: 3,
            touch_mode: 2,
            touch_restore: 2,
            ..State::default()
        };
        let (mut paints, mut done, mut saves, mut promoted) = (0, 0, 0, 0);
        let hits = [Interaction {
            rect: crate::scene::Rect {
                x: 0,
                y: 0,
                w: 100,
                h: 100,
            },
            action: TILE,
            value: tile,
            inputs: 1,
            ..Interaction::default()
        }];
        for n in 0..2 {
            let e = s.update(
                &Input {
                    touch_flags: TOUCH_RELEASED,
                    touch_x: 10,
                    touch_y: 10,
                    ..Input::default()
                },
                Some(&hits),
                &Feedback::default(),
            );
            paints += i32::from(e.navigation.invalidate);
            done += i32::from(e.navigation.kind != 0);
            saves += i32::from(e.writes & WRITE_SETTINGS != 0);
            promoted += i32::from(e.writes & CLEAN_PARENT != 0);
            // These frozen cases call runTile directly, before the app's tap invalidation.
            if tile == 1 {
                paints -= 1;
            }
            actual += &format!(
                "tile {} {} {} {} {} {} {} {} {} {} {}\n",
                tile,
                n,
                s.inverted,
                s.orientation,
                s.touch_mode,
                s.touch_restore,
                saves,
                u8::from(s.clean_revision != 0),
                promoted,
                done,
                paints
            );
        }
    }
    for dragged in [false, true] {
        for kind in 0..3 {
            let mut s = State {
                kind: Screen::Frontlight as u8,
                dragging: u8::from(dragged),
                ..State::default()
            };
            let e = s.update(
                &Input {
                    touch_flags: TOUCH_RELEASED,
                    touch_x: 20,
                    touch_y: if kind == 0 { 400 } else { 20 },
                    swipe: if kind == 1 { SWIPE_UP } else { 0 },
                    ..Input::default()
                },
                Some(&[]),
                &Feedback {
                    panel_bottom: 300,
                    ..Feedback::default()
                },
            );
            actual += &format!(
                "dismiss {} {} {} {} {} {}\n",
                u8::from(dragged),
                kind,
                u8::from(e.navigation.kind != 0),
                s.dragging,
                u8::from(s.clean_revision != 0),
                e.navigation.invalidate
            );
        }
    }
    let expected = include_str!("../../../../../test/ui_utility/input.golden");
    assert_eq!(actual, expected);
}
#[test]
fn contact_grab_and_release_keeps_last_held_x() {
    let mut s = State {
        kind: Screen::Interval as u8,
        maximum: 100,
        ..State::default()
    };
    assert!(s.init());
    let hits = [Interaction {
        rect: crate::scene::Rect {
            x: 10,
            y: 10,
            w: 100,
            h: 40,
        },
        action: SLIDER,
        inputs: 1 | DRAG,
        ..Interaction::default()
    }];
    for (flags, x, expected) in [
        (TOUCH_PRESSED | TOUCH_HELD, 20, 10),
        (TOUCH_HELD, 150, 100),
        (TOUCH_RELEASED, -1, 100),
    ] {
        s.update(
            &Input {
                touch_flags: flags,
                touch_x: x,
                touch_y: 20,
                ..Input::default()
            },
            Some(&hits),
            &Feedback::default(),
        );
        assert_eq!(s.value, expected);
    }
    assert_eq!(s.contact_held, 0);
    assert_eq!(s.last_drag_x, -1);
    s.update(&Input::default(), Some(&hits), &Feedback::default());
    assert_eq!(s.dragging, 0);
}
#[test]
fn layout_wire_contract() {
    use core::mem::{align_of, offset_of, size_of};
    assert_eq!(
        (
            size_of::<State>(),
            align_of::<State>(),
            offset_of!(State, kind),
            offset_of!(State, last_drag_x)
        ),
        (56, 4, 36, 52)
    );
    assert_eq!(
        (
            size_of::<Update>(),
            align_of::<Update>(),
            offset_of!(Update, writes)
        ),
        (12, 4, 8)
    );
    assert_eq!(
        (
            size_of::<Layout>(),
            align_of::<Layout>(),
            offset_of!(Layout, popup_radius)
        ),
        (80, 4, 62)
    );
    assert_eq!(
        (
            size_of::<Feedback>(),
            align_of::<Feedback>(),
            offset_of!(Feedback, clean_revision)
        ),
        (24, 4, 20)
    );
    assert_eq!(
        size_of::<super::ffi::TextServices>(),
        3 * size_of::<usize>()
    );
    assert_eq!(align_of::<super::ffi::TextServices>(), align_of::<usize>());
    assert_eq!(
        offset_of!(super::ffi::TextServices, text_height),
        2 * size_of::<usize>()
    );
}
#[test]
fn failed_ffi_view_never_publishes() {
    use super::ffi::{xp_ui_utility_frontlight_view, xp_ui_utility_view, TextServices};
    use crate::{
        ffi::{Buffer, Metrics, SceneView},
        scene::{Command, Rect},
    };
    use core::ptr;
    unsafe extern "C" fn text(_: *mut core::ffi::c_void, _: u32) -> *const core::ffi::c_char {
        c"Title".as_ptr()
    }
    unsafe extern "C" fn unknown(_: *mut core::ffi::c_void, _: u32) -> *const core::ffi::c_char {
        ptr::null()
    }
    unsafe extern "C" fn height(
        _: *mut core::ffi::c_void,
        _: u32,
        _: i32,
        _: i16,
        _: u8,
        _: u8,
    ) -> i16 {
        24
    }
    unsafe extern "C" fn width(
        _: *mut core::ffi::c_void,
        _: i32,
        _: *const u8,
        _: u16,
        _: u8,
    ) -> i32 {
        40
    }
    unsafe extern "C" fn line(_: *mut core::ffi::c_void, _: i32) -> i32 {
        24
    }
    let mut commands = [Command::default(); 40];
    let mut hits = [Interaction::default(); 8];
    let metrics = Metrics {
        context: ptr::null_mut(),
        width: Some(width),
        line_height: Some(line),
    };
    let mut texts = TextServices {
        context: ptr::null_mut(),
        text: Some(text),
        text_height: Some(height),
    };
    let layout = Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 480,
            h: 800,
        },
        safe: Rect {
            x: 0,
            y: 0,
            w: 480,
            h: 800,
        },
        popup_padding: 16,
        ..Layout::default()
    };
    let mut state = State::default();
    let mut buffer = Buffer {
        commands: commands.as_mut_ptr(),
        hits: hits.as_mut_ptr(),
        command_capacity: 40,
        hit_capacity: 8,
    };
    for case in 0..7 {
        state.kind = match case {
            0 => 255,
            5 => Screen::Frontlight as u8,
            _ => 0,
        };
        let view = if case == 6 {
            xp_ui_utility_frontlight_view
        } else {
            xp_ui_utility_view
        };
        texts.text = if case == 1 {
            None
        } else if case == 2 {
            Some(unknown)
        } else {
            Some(text)
        };
        texts.text_height = if case == 3 { None } else { Some(height) };
        buffer.command_capacity = if case == 4 { 1 } else { 40 };
        let mut feedback = Feedback {
            panel_bottom: 123,
            ..Feedback::default()
        };
        let mut out = SceneView {
            commands: ptr::null(),
            text: ptr::null(),
            interactions: ptr::null(),
            generation: 777,
            command_count: 0,
            text_len: 0,
            interaction_count: 0,
            refresh: 0,
            reserved: 0,
        };
        assert_eq!(
            unsafe {
                view(
                    &state,
                    &layout,
                    &metrics,
                    &texts,
                    &buffer,
                    &mut feedback,
                    &mut out,
                )
            },
            0
        );
        assert_eq!(out.generation, 777);
        assert_eq!(feedback.panel_bottom, 123);
    }
}
#[test]
fn confirmation_defers_dismissal_and_ignores_unpublished_touch() {
    let mut state = State::default();
    assert!(state.init());
    let f = Feedback::default();
    let touch = Input {
        touch_flags: TOUCH_RELEASED,
        touch_x: 100,
        touch_y: 100,
        ..Input::default()
    };
    assert_eq!(state.update(&touch, None, &f).navigation.kind, 0);
    assert_eq!(state.dismissed, 0);
    assert_eq!(
        state
            .update(
                &Input {
                    released: BACK,
                    ..Input::default()
                },
                None,
                &f
            )
            .navigation
            .kind,
        0
    );
    assert_eq!(state.dismissed, 1);
    assert!(state.interest(10).due(&Input {
        now_ms: 10,
        ..Input::default()
    }));
    assert_eq!(state.update(&Input::default(), None, &f).navigation.kind, 1);
}
#[test]
fn publication_preserves_new_touch_feedback() {
    let mut s = State {
        kind: Screen::Interval as u8,
        maximum: 100,
        small_step: 1,
        ..State::default()
    };
    assert!(s.init());
    let hits = [Interaction {
        rect: crate::scene::Rect {
            x: 0,
            y: 0,
            w: 100,
            h: 100,
        },
        action: STEP,
        value: 1,
        inputs: 1,
        ..Interaction::default()
    }];
    let i = Input {
        touch_flags: TOUCH_RELEASED,
        touch_x: 10,
        touch_y: 10,
        ..Input::default()
    };
    s.update(&i, Some(&hits), &Feedback::default());
    let rendered = s;
    s.update(&i, Some(&hits), &Feedback::default());
    s.publish(&rendered);
    assert!(s.flashing(STEP, 1));
    assert_eq!(s.value, 2);
    let current = s;
    s.update(
        &Input {
            touch_flags: TOUCH_PRESSED,
            ..i
        },
        Some(&hits),
        &Feedback::default(),
    );
    assert!(s.flashing(STEP, 1));
    assert_eq!(s.active_action, STEP);
    s.publish(&current);
    assert_eq!(s.flash_pending, 0);
    assert_eq!(s.active_action, STEP);
}
