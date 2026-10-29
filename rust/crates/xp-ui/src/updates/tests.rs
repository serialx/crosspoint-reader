use super::{state::*, Feedback};
use crate::model::{Input, BACK, CONFIRM};
#[test]
fn progress_covers_partition_and_fonts() {
    for total in [6_553_600, u32::MAX] {
        let mut s = State::new(0, 0);
        let e = s.service(Event::Progress, total, total);
        assert_eq!(s.total, total);
        assert_eq!(s.processed, total);
        assert_eq!(e, Action::None as u8);
    }
}
#[test]
fn recovery_cancel_and_failure_input() {
    for recovery in [0, 1] {
        let mut s = State::new(1, recovery);
        let e = s.service(Event::Picked, 1, 0);
        assert_eq!(
            e,
            if recovery != 0 {
                Action::Picker
            } else {
                Action::Finish
            } as u8
        );
        s.phase = 5;
        let e = s.input(
            Input {
                pressed: CONFIRM,
                ..Input::default()
            },
            &Feedback::default(),
            false,
            0,
            0,
        );
        assert_eq!(
            e.action,
            if recovery != 0 {
                Action::Picker
            } else {
                Action::Finish
            } as u8
        );
    }
}
#[test]
fn transfer_cancel_is_sticky() {
    let mut s = State::new(2, 0);
    s.service(Event::FontBegin, 3, 0);
    s.service(Event::TransferInput, 0, 1);
    s.service(Event::TransferInput, 0, 0);
    assert_eq!((s.cancel_requested, s.home_requested), (1, 1));
    assert_eq!(s.service(Event::FontAborted, 0, 0), Action::Home as u8);
}
#[test]
fn font_error_back_precedes_retry() {
    let mut s = State::new(2, 0);
    s.phase = 6;
    let e = s.input(
        Input {
            pressed: BACK | CONFIRM,
            ..Input::default()
        },
        &Feedback::default(),
        false,
        1,
        1,
    );
    assert_eq!(e.action, Action::None as u8);
    assert_eq!(s.phase, 3);
}
#[test]
fn interest_wrap() {
    let mut s = State::new(2, 0);
    s.phase = 3;
    s.list.last_repeat = u32::MAX - 200;
    let interest = s.interest(u32::MAX - 100);
    assert!(!interest.due(&Input {
        held: 4,
        held_ms: 600,
        now_ms: 299,
        ..Input::default()
    }));
    assert!(interest.due(&Input {
        held: 4,
        held_ms: 600,
        now_ms: 300,
        ..Input::default()
    }));
}
#[test]
fn original_progress_rounding() {
    let source = include_str!("../../../../../test/ui_updates/percent.golden");
    for line in source.lines() {
        let mut fields = line.split_whitespace().map(|v| v.parse::<u32>().unwrap());
        let processed = fields.next().unwrap();
        let total = fields.next().unwrap();
        let float = fields.next().unwrap();
        let integer = fields.next().unwrap();
        for kind in [0, 1, 2] {
            let mut state = State::new(kind, 0);
            state.service(Event::Progress, processed, total);
            assert_eq!(
                state.percent(),
                if kind == 1 { integer } else { float },
                "{line} kind={kind}"
            );
        }
    }
    // The old C3 ROM converted 0/0 to INT32_MAX; zero percent is the approved fix.
    let state = State::new(0, 0);
    assert_eq!(state.percent(), 0);
}

#[test]
fn rejected_scene_blocks_actions_and_preserves_back() {
    use crate::model::{NAV_NEXT, TOUCH_PRESSED, TOUCH_RELEASED};
    for prior_publication in [false, true] {
        for (kind, phase) in [(0, 2), (1, 5), (2, 3), (2, 6), (3, 0)] {
            let mut state = State::new(kind, 0);
            state.phase = phase;
            state.popup_active = 1;
            state.popup_selected = 1;
            state.list.active = 0;
            state.list.pending_scroll = 2;
            state.list.last_repeat = 400;
            state.touch_active = 2;
            // Both allocation failure and painter rejection enter this same transition.
            for _failure in ["build", "paint"] {
                state.service(Event::SceneFailed, 0, 0);
                assert_eq!(state.list.active, -1);
                assert_eq!(state.list.pending_scroll, 0);
                assert_eq!(state.list.last_repeat, 0);
                assert_eq!(state.touch_active, 0);
                let selected = state.list.selected;
                for input in [
                    Input {
                        pressed: CONFIRM | NAV_NEXT,
                        ..Input::default()
                    },
                    Input {
                        released: CONFIRM,
                        ..Input::default()
                    },
                    Input {
                        held: NAV_NEXT,
                        held_ms: 1000,
                        now_ms: 2000,
                        ..Input::default()
                    },
                    Input {
                        touch_flags: TOUCH_PRESSED | TOUCH_RELEASED,
                        ..Input::default()
                    },
                    Input {
                        swipe: crate::model::SWIPE_UP,
                        ..Input::default()
                    },
                ] {
                    let effect = state.input(input, &Feedback::default(), prior_publication, 3, 3);
                    assert_eq!(effect.action, 0, "kind={kind} phase={phase}");
                    assert_eq!(state.list.selected, selected);
                }
            }
            let effect = state.input(
                Input {
                    pressed: BACK,
                    released: BACK,
                    ..Input::default()
                },
                &Feedback::default(),
                false,
                3,
                3,
            );
            assert!(effect.action == Action::Finish as u8 || effect.repaint != 0);
        }
    }
}

#[test]
fn prepaint_activation_and_successful_republication() {
    for published in [false, true] {
        let mut state = State::new(3, 0);
        state.popup_selected = 1;
        let input = Input {
            released: CONFIRM,
            ..Input::default()
        };
        assert_eq!(
            state
                .input(input, &Feedback::default(), published, 0, 0)
                .action,
            Action::Clear as u8
        );
        state.popup_active = 1;
        state.service(Event::SceneFailed, 0, 0);
        assert_eq!(
            state.input(input, &Feedback::default(), false, 0, 0).action,
            0
        );
        let snapshot = state;
        unsafe { super::ffi::publish(&mut state, &snapshot, &Feedback::default()) };
        assert_eq!(state.scene_failed, 0);
        assert_eq!(
            state.input(input, &Feedback::default(), true, 0, 0).action,
            Action::Clear as u8
        );
    }
}

#[test]
fn popup_release_does_not_require_delayed_press() {
    use crate::model::{TOUCH_PRESSED, TOUCH_RELEASED};
    use crate::scene::Rect;
    let f = Feedback {
        popup: Rect {
            x: 0,
            y: 0,
            w: 100,
            h: 100,
        },
        options: [
            Rect {
                x: 10,
                y: 10,
                w: 80,
                h: 20,
            },
            Rect {
                x: 10,
                y: 40,
                w: 80,
                h: 20,
            },
        ],
        ..Feedback::default()
    };
    for kind in [OTA, CACHE] {
        for option in 0..2 {
            for flags in [TOUCH_RELEASED, TOUCH_PRESSED | TOUCH_RELEASED] {
                let mut s = State::new(kind, 0);
                s.phase = if kind == OTA {
                    ota::CONFIRM
                } else {
                    cache::WARNING
                };
                s.popup_active = 1;
                s.popup_selected = 1 - option;
                let e = s.input(
                    Input {
                        touch_flags: flags,
                        touch_x: 20,
                        touch_y: if option == 0 { 20 } else { 50 },
                        ..Input::default()
                    },
                    &f,
                    true,
                    0,
                    0,
                );
                assert_eq!(
                    e.action,
                    if option == 0 {
                        Action::Finish
                    } else if kind == OTA {
                        Action::Install
                    } else {
                        Action::Clear
                    } as u8
                );
                assert_eq!(s.popup_active, 0);
                assert_eq!(e.reserved, 0);
            }
        }
    }
}

#[test]
fn shrinking_font_publication_clamps_only_unchanged_navigation() {
    for navigated in [false, true] {
        let mut state = State::new(FONTS, 0);
        state.phase = font::FAMILIES;
        state.list.selected = 1;
        let snapshot = state;
        let mut feedback = Feedback {
            list: snapshot.list,
            ..Feedback::default()
        };
        feedback.list.selected = 0;
        if navigated {
            state.list.navigation_revision += 1;
            state.list.selected = 2;
        }
        unsafe {
            super::ffi::publish(&mut state, &snapshot, &feedback);
        }
        assert_eq!(state.list.selected, if navigated { 2 } else { 0 });
        if !navigated {
            let e = state.input(
                Input {
                    released: CONFIRM,
                    ..Input::default()
                },
                &feedback,
                true,
                1,
                1,
            );
            assert_eq!((e.action, e.value), (Action::FontActivate as u8, 0));
        }
    }
}

#[test]
fn older_font_publication_preserves_touch_selection() {
    use crate::{model::TOUCH_RELEASED, scene::Rect, settings::hits};
    let mut state = State::new(FONTS, 0);
    state.phase = font::FAMILIES;
    let feedback = Feedback {
        list: state.list,
        rows: hits::Layout {
            first_row: Rect {
                x: 0,
                y: 0,
                w: 100,
                h: 20,
            },
            count: 4,
            uniform: 1,
            ..hits::Layout::default()
        },
        ..Feedback::default()
    };
    let initial = state;
    unsafe { super::ffi::publish(&mut state, &initial, &feedback) };
    let snapshot = state;
    let input = Input {
        touch_flags: TOUCH_RELEASED,
        touch_x: 10,
        touch_y: 50,
        ..Input::default()
    };
    let effect = unsafe { super::ffi::input(&mut state, &input, &feedback, 1, 4, 3) };
    assert_eq!(
        (effect.action, effect.value),
        (Action::FontActivate as u8, 2)
    );
    assert_eq!(state.list.selected, 2);
    unsafe { super::ffi::publish(&mut state, &snapshot, &feedback) };
    assert_eq!(state.list.selected, 2);
}
