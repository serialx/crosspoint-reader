use super::{
    definition, paint, popup,
    selection::{Input, Layout, Painted, State, Word},
};
use crate::{model, scene::Rect};
use xp_core::glyph::{Clip, GlyphTarget};
fn word(x: i16, row: u16, page: u8, rtl: u8) -> Word {
    Word {
        x,
        y: row as i16 * 20,
        width: 20,
        height: 20,
        row,
        pageOffset: page,
        isRtl: rtl,
        text: c"word".as_ptr(),
        characterSpacing: 0,
        pageWordIndex: 0,
        reserved: 0,
        fontId: 0,
        startOffset: 0,
        endOffset: 4,
        style: 0,
        paragraphStart: 0,
        discretionaryHyphen: 0,
    }
}
#[test]
fn original_queued_taps_repeat_wrap_and_overflow() {
    let mut s = State::default();
    let input = |now, pressed, held, released| model::Input {
        now_ms: now,
        pressed,
        held,
        released,
        ..Default::default()
    };
    for frame in [
        input(0, model::SCREEN_RIGHT, model::SCREEN_RIGHT, 0),
        input(40, 0, 0, 0),
        input(90, model::SCREEN_RIGHT, model::SCREEN_RIGHT, 0),
        input(130, 0, 0, 0),
        input(180, 0, 0, model::CONFIRM),
        input(220, model::SCREEN_LEFT, model::SCREEN_LEFT, 0),
        input(250, 0, 0, 0),
    ] {
        assert!(s.poll(frame));
    }
    assert_eq!(s.pending_count, 4);
    assert_eq!(
        [
            s.dequeue(),
            s.dequeue(),
            s.dequeue(),
            s.dequeue(),
            s.dequeue()
        ],
        [2, 2, 16, 1, 0]
    );
    for frame in [
        input(1000, model::SCREEN_RIGHT, model::SCREEN_RIGHT, 0),
        input(1499, 0, model::SCREEN_RIGHT, 0),
        input(1500, 0, model::SCREEN_RIGHT, 0),
        input(2000, 0, model::SCREEN_RIGHT, 0),
        input(2010, 0, 0, 0),
    ] {
        assert!(s.poll(frame));
    }
    assert_eq!(
        [s.dequeue(), s.dequeue(), s.dequeue(), s.dequeue()],
        [2, 2, 2, 0]
    );
    assert!(s.poll(input(
        u32::MAX - 100,
        model::SCREEN_LEFT,
        model::SCREEN_LEFT,
        0
    )));
    assert!(s.poll(input(398, 0, model::SCREEN_LEFT, 0)));
    assert_eq!(s.pending_count, 1);
    assert!(s.poll(input(399, 0, model::SCREEN_LEFT, 0)));
    assert_eq!(s.pending_count, 2);
    s.pending_count = 0;
    for i in 0..16 {
        assert!(s.poll(input(i, model::SCREEN_RIGHT, 0, 0)));
    }
    assert!(!s.poll(input(20, model::SCREEN_LEFT, 0, 0)));
    for _ in 0..16 {
        assert_eq!(s.dequeue(), 2);
    }
    assert_eq!(s.dequeue(), 0);
}
#[test]
fn buttons_keep_rtl_edges_range_back_and_result_action() {
    let words = [word(0, 0, 0, 1), word(30, 0, 0, 1), word(0, 1, 0, 0)];
    let mut s = State::init(&words, 0, 100, -1, 1);
    s.buttons(&words, 2);
    assert_eq!(s.selected, 0);
    s.buttons(&words, 1);
    assert_eq!(s.selected, 1);
    s.buttons(&words, 8);
    assert_eq!(s.selected, 2);
    assert_eq!(s.buttons(&words, 16).kind, 0);
    assert_eq!(s.range_start, 2);
    let result = s.buttons(&words, 16);
    assert_eq!((result.kind, result.action), (2, 0));
    s.buttons(&words, 32);
    assert_eq!(s.range_start, -1);
    assert_eq!(s.buttons(&words, 32).kind, 1);
    s.mode = 2;
    s.range_start = 0;
    assert_eq!(s.buttons(&words, 16).action, 1);
    assert_eq!(s.buttons(&words, 32).kind, 1);
}
#[test]
fn drag_requires_motion_and_one_second_dwell() {
    let words = [word(0, 0, 0, 0), word(0, 1, 0, 0), word(0, 2, 1, 0)];
    let layout = Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 200,
            h: 100,
        },
        safe: Rect {
            x: 0,
            y: 0,
            w: 200,
            h: 40,
        },
        line_height: 20,
        touch: 1,
        ..Default::default()
    };
    let mut s = State::init(&words, 0, 200, -1, 0);
    s.update(
        &words,
        &layout,
        Input {
            input: model::Input {
                touch_x: 5,
                touch_y: 5,
                ..Default::default()
            },
            pressed: 1,
            ..Default::default()
        },
    );
    assert_eq!((s.dragging, s.range_start), (1, 0));
    for now in [0, 999] {
        s.update(
            &words,
            &layout,
            Input {
                input: model::Input {
                    touch_x: 5,
                    touch_y: 35,
                    now_ms: now,
                    ..Default::default()
                },
                held: 1,
                ..Default::default()
            },
        );
        assert_eq!(s.page, 0);
    }
    s.update(
        &words,
        &layout,
        Input {
            input: model::Input {
                touch_x: 5,
                touch_y: 35,
                now_ms: 1000,
                ..Default::default()
            },
            held: 1,
            ..Default::default()
        },
    );
    assert_eq!((s.selected, s.page), (2, 1));
    s.update(&words, &layout, Input::default());
    assert_eq!(s.dragging, 0);
}
#[test]
fn definition_repeat_release_and_recursive_back_contract() {
    let mut s = definition::State {
        pages: 5,
        lookup: 1,
        history_depth: 1,
        ..Default::default()
    };
    let frame = |pressed, released, held, held_ms, now_ms| model::Input {
        pressed,
        released,
        held,
        held_ms,
        now_ms,
        ..Default::default()
    };
    s.update(
        frame(model::NAV_NEXT, 0, model::NAV_NEXT, 0, 0),
        false,
        false,
        false,
        528,
        true,
    );
    assert_eq!(s.page, 1);
    s.update(
        frame(0, 0, model::NAV_NEXT, 500, 500),
        false,
        false,
        false,
        528,
        true,
    );
    assert_eq!(s.page, 1);
    s.update(
        frame(0, 0, model::NAV_NEXT, 501, 501),
        false,
        false,
        false,
        528,
        true,
    );
    assert_eq!(s.page, 2);
    s.update(
        frame(0, model::NAV_NEXT, 0, 1000, 1000),
        false,
        false,
        false,
        528,
        true,
    );
    assert_eq!(s.page, 2);
    assert_eq!(
        s.update(
            frame(0, model::BACK, 0, 999, 0),
            false,
            false,
            false,
            528,
            true
        )
        .kind,
        definition::BACK
    );
    assert_eq!(
        s.update(
            frame(0, model::BACK, 0, 1000, 0),
            false,
            false,
            false,
            528,
            true
        )
        .kind,
        definition::CANCEL
    );
    s.history_depth = 0;
    assert_eq!(
        s.update(
            frame(0, model::BACK, 0, 0, 0),
            false,
            false,
            false,
            528,
            true
        )
        .kind,
        definition::FINISH
    );
}
#[test]
fn lookup_errors_and_timer_match_old_dispositions() {
    for (opened, index, result, message, popup_kind) in [
        (false, 0, 0, popup::ERROR, 3),
        (false, 1, 0, popup::LOW_MEMORY, 3),
        (false, 2, 0, popup::READ_FAILED, 3),
        (true, 0, 1, popup::NOT_FOUND, 2),
        (true, 0, 2, popup::LOW_MEMORY, 3),
        (true, 0, 3, popup::DECOMPRESS, 3),
        (true, 0, 4, popup::READ_FAILED, 3),
    ] {
        let mut s = popup::Lookup::new();
        s.result(opened, index, result, false, u32::MAX - 100);
        assert_eq!((s.message, s.popup), (message, popup_kind));
        assert!(!s.expired(1398));
        assert!(s.expired(1399));
    }
}
#[test]
fn invalid_painter_does_not_write_or_call_fonts() {
    let mut bytes = [0x5au8; 8];
    let before = bytes;
    let target = GlyphTarget {
        buffer: bytes.as_mut_ptr(),
        buffer_len: 8,
        width: 8,
        height: 8,
        stride: 1,
        origin_y: 0,
        rows: 8,
        clip: Clip {
            left: 0,
            top: 0,
            right: 8,
            bottom: 8,
        },
        orientation: 3,
        plane: 0,
        absolute: 0,
        strip: 0,
        scanning: 0,
    };
    let words = [word(0, 0, 0, 0)];
    let state = State::init(&words, 0, 8, -1, 0);
    let fonts = paint::Fonts {
        context: core::ptr::null_mut(),
        word: None,
    };
    assert!(unsafe {
        paint::paint(
            &state,
            &words,
            &Layout::default(),
            &Painted::default(),
            &target,
            fonts,
            0,
        )
    }
    .is_err());
    assert_eq!(bytes, before);
}

#[test]
fn private_wire_rejects_null_records_counts_and_output_before_painting() {
    use super::ffi;
    let mut state = State {
        selected: 71,
        ..Default::default()
    };
    unsafe {
        ffi::selection_init(&mut state, 0, core::ptr::null(), 1, 8, 0, 0);
        assert_eq!(state.selected, 71);
        ffi::selection_init(&mut state, 0, core::ptr::dangling(), 65536, 8, 0, 0);
        assert_eq!(state.selected, 71);
        assert_eq!(
            ffi::selection_paint(
                &state,
                core::ptr::null(),
                1,
                &Layout::default(),
                &Painted::default(),
                core::ptr::null(),
                core::ptr::null(),
                0
            ),
            0
        );
        let words = [word(0, 0, 0, 0)];
        let state = State::init(&words, 0, 8, -1, 0);
        let mut next = Painted {
            selected: 71,
            ..Default::default()
        };
        assert_eq!(
            ffi::selection_view(
                &state,
                words.as_ptr(),
                1,
                &Layout::default(),
                &Painted::default(),
                core::ptr::null(),
                &mut next,
                core::ptr::null_mut()
            ),
            0
        );
        assert_eq!(next.selected, 71);
    }
}

#[test]
fn simultaneous_navigation_and_expired_message_keep_repaint() {
    let mut state = definition::State {
        page: 1,
        pages: 3,
        lookup: 1,
        message_visible: 1,
        ..Default::default()
    };
    let effect = state.update(
        model::Input {
            now_ms: 1500,
            released: model::CONFIRM,
            ..Default::default()
        },
        false,
        false,
        false,
        528,
        true,
    );
    assert_eq!((effect.kind, effect.invalidate), (definition::SELECT, 1));
    let effect = state.update(
        model::Input {
            pressed: model::NAV_NEXT | model::NAV_PREVIOUS,
            ..Default::default()
        },
        false,
        false,
        false,
        528,
        true,
    );
    assert_eq!((state.page, effect.invalidate, effect.value), (1, 1, 1));
}
#[test]
fn painter_borrows_original_word_font_style_and_tracking() {
    unsafe extern "C" fn glyph(
        context: *mut core::ffi::c_void,
        _: *const GlyphTarget,
        word: *const Word,
        x: i32,
        y: i32,
    ) {
        let calls = unsafe { &mut *context.cast::<usize>() };
        let word = unsafe { &*word };
        assert_eq!(
            (word.fontId, word.style, word.characterSpacing),
            (42, 3, -2)
        );
        assert_eq!((x, y), (i32::from(word.x), i32::from(word.y)));
        *calls += 1;
    }
    let mut words = [word(0, 0, 0, 0)];
    words[0].fontId = 42;
    words[0].style = 3;
    words[0].characterSpacing = -2;
    let state = State::init(&words, 0, 8, -1, 0);
    let layout = Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 100,
            h: 100,
        },
        safe: Rect {
            x: 0,
            y: 0,
            w: 100,
            h: 100,
        },
        ..Default::default()
    };
    let mut bytes = [255u8; 8];
    let mut calls = 0usize;
    let target = GlyphTarget {
        buffer: bytes.as_mut_ptr(),
        buffer_len: 8,
        width: 8,
        height: 8,
        stride: 1,
        origin_y: 0,
        rows: 8,
        clip: Clip {
            left: 0,
            top: 0,
            right: 8,
            bottom: 8,
        },
        orientation: 3,
        plane: 0,
        absolute: 0,
        strip: 0,
        scanning: 0,
    };
    let fonts = paint::Fonts {
        context: (&mut calls as *mut usize).cast(),
        word: Some(glyph),
    };
    unsafe {
        paint::paint(
            &state,
            &words,
            &layout,
            &Painted::default(),
            &target,
            fonts,
            0,
        )
        .unwrap();
    }
    assert_eq!(calls, 1);
}

#[test]
fn long_selection_view_has_a_fixed_nineteen_command_bound() {
    use crate::{
        ffi::{Buffer, SceneView},
        scene::Command,
    };
    let words = [word(0, 0, 0, 0); 720];
    let mut state = State::init(&words, 0, 528, 0, 719);
    state.range_start = 0;
    state.selected = 719;
    let layout = Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 528,
            h: 792,
        },
        safe: Rect {
            x: 0,
            y: 0,
            w: 528,
            h: 760,
        },
        line_height: 20,
        label_width: 80,
        label_height: 20,
        padding: 8,
        touch: 1,
        ..Default::default()
    };
    let mut commands = [Command::default(); 20];
    let mut next = Painted::default();
    let mut scene: SceneView = unsafe { core::mem::zeroed() };
    for capacity in [20, 18] {
        let buffer = Buffer {
            commands: commands.as_mut_ptr(),
            hits: core::ptr::null_mut(),
            command_capacity: capacity,
            hit_capacity: 0,
        };
        let result = unsafe {
            super::ffi::selection_view(
                &state,
                words.as_ptr(),
                720,
                &layout,
                &Painted::default(),
                &buffer,
                &mut next,
                &mut scene,
            )
        };
        assert_eq!(result, if capacity == 20 { 1 } else { 2 });
        assert_eq!(scene.command_count, 19);
    }
}
