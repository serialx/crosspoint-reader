use crate::percent::{Outcome, State};
use xp_ui::{
    model::{self, Input},
    scene::{Interaction, Rect},
    utility::{CHROME, OK, SLIDER, STEP},
};

fn input() -> Input {
    Input {
        touch_x: -1,
        touch_y: -1,
        ..Input::default()
    }
}
fn hits() -> [Interaction; 5] {
    [
        Interaction {
            rect: Rect {
                x: 0,
                y: 0,
                w: 100,
                h: 100,
            },
            action: CHROME,
            inputs: 1,
            ..Default::default()
        },
        Interaction {
            rect: Rect {
                x: 10,
                y: 20,
                w: 81,
                h: 20,
            },
            action: SLIDER,
            inputs: 3,
            ..Default::default()
        },
        Interaction {
            rect: Rect {
                x: 10,
                y: 60,
                w: 20,
                h: 20,
            },
            action: STEP,
            value: -1,
            inputs: 1,
            ..Default::default()
        },
        Interaction {
            rect: Rect {
                x: 30,
                y: 60,
                w: 20,
                h: 20,
            },
            action: STEP,
            value: 1,
            inputs: 1,
            ..Default::default()
        },
        Interaction {
            rect: Rect {
                x: 50,
                y: 60,
                w: 40,
                h: 20,
            },
            action: OK,
            inputs: 1,
            ..Default::default()
        },
    ]
}
#[test]
fn percent_steps_wrap_and_zero_is_a_real_selection() {
    for (start, key, edge, expected) in [
        (0, model::LEFT, false, 99),
        (99, model::RIGHT, false, 100),
        (100, model::RIGHT, false, 1),
        (0, model::UP, true, 90),
        (90, model::DOWN, true, 100),
        (100, model::DOWN, true, 10),
        (0, model::UP, false, 10),
        (0, model::DOWN, false, 90),
    ] {
        let mut state = State::new(start);
        let update = state.update(
            Input {
                pressed: key,
                ..input()
            },
            None,
            edge,
        );
        assert_eq!(update.outcome, Outcome::Redraw);
        assert_eq!(state.value(), expected);
    }
    let mut state = State::new(0);
    assert_eq!(
        state
            .update(
                Input {
                    released: model::CONFIRM,
                    ..input()
                },
                None,
                true
            )
            .outcome,
        Outcome::Confirm(0)
    );
    assert_eq!(
        state
            .update(
                Input {
                    released: model::BACK,
                    ..input()
                },
                None,
                true
            )
            .outcome,
        Outcome::Cancel
    );
}
#[test]
fn repeat_uses_native_strict_threshold_and_one_shared_clock() {
    let mut state = State::new(40);
    for (now, held_ms, pressed, released, held, expected) in [
        (1000, 0, model::RIGHT, 0, model::RIGHT, 41),
        (1500, 500, 0, 0, model::RIGHT, 41),
        (1501, 501, 0, 0, model::RIGHT, 42),
        (2001, 1001, 0, 0, model::RIGHT, 42),
        (2002, 1002, 0, 0, model::RIGHT, 43),
        (2600, 1600, 0, model::RIGHT, model::RIGHT, 43),
        (2700, 1700, model::LEFT, 0, model::LEFT | model::RIGHT, 43),
    ] {
        state.update(
            Input {
                now_ms: now,
                held_ms,
                pressed,
                released,
                held,
                ..input()
            },
            None,
            true,
        );
        assert_eq!(state.value(), expected, "at {now}");
    }
}
#[test]
fn drag_keeps_grab_across_repaint_and_swallows_release_fallout() {
    let hits = hits();
    let mut state = State::new(20);
    assert_eq!(
        state
            .update(
                Input {
                    touch_x: 50,
                    touch_y: 25,
                    touch_flags: model::TOUCH_HELD,
                    ..input()
                },
                Some(&hits),
                true
            )
            .outcome,
        Outcome::Redraw
    );
    assert_eq!(state.value(), 50);
    state.render_begin();
    state.update(
        Input {
            touch_x: 110,
            touch_y: 200,
            touch_flags: model::TOUCH_HELD,
            ..input()
        },
        Some(&hits),
        true,
    );
    assert_eq!(state.value(), 100);
    let release = state.update(
        Input {
            touch_x: 50,
            touch_y: 25,
            touch_flags: model::TOUCH_RELEASED,
            swipe: model::SWIPE_RIGHT,
            ..input()
        },
        Some(&hits),
        true,
    );
    assert_eq!(release.outcome, Outcome::Redraw);
    assert!(release.feedback);
    assert_eq!(state.value(), 100);
    assert_eq!(
        state
            .update(
                Input {
                    released: model::BACK,
                    swipe: model::SWIPE_RIGHT,
                    ..input()
                },
                Some(&hits),
                true
            )
            .outcome,
        Outcome::None
    );
    assert_eq!(
        state
            .update(
                Input {
                    released: model::BACK,
                    ..input()
                },
                Some(&hits),
                true
            )
            .outcome,
        Outcome::Cancel
    );
}
#[test]
fn unpublished_controls_cannot_route_and_newest_hit_wins() {
    let hits = hits();
    let mut state = State::new(20);
    let tap = Input {
        touch_x: 40,
        touch_y: 65,
        touch_flags: model::TOUCH_RELEASED,
        ..input()
    };
    assert_eq!(state.update(tap, None, true).outcome, Outcome::None);
    assert_eq!(state.value(), 20);
    assert_eq!(
        state.update(tap, Some(&hits), true).outcome,
        Outcome::Redraw
    );
    assert_eq!(state.value(), 21);
    assert!(state.flashing(STEP, 1));
    state.render_begin();
    assert!(state.flashing(STEP, 1));
    state.render_begin();
    assert!(!state.flashing(STEP, 1));
    assert_eq!(
        state
            .update(
                Input {
                    touch_x: 60,
                    touch_y: 65,
                    ..tap
                },
                Some(&hits),
                true
            )
            .outcome,
        Outcome::Confirm(21)
    );
    assert!(!state.flashing(OK, 0));
}
#[test]
fn drag_start_outside_does_not_acquire_slider_later() {
    let hits = hits();
    let mut state = State::new(20);
    state.update(
        Input {
            touch_x: 150,
            touch_y: 150,
            touch_flags: model::TOUCH_HELD,
            ..input()
        },
        Some(&hits),
        true,
    );
    state.render_begin();
    state.update(
        Input {
            touch_x: 50,
            touch_y: 25,
            touch_flags: model::TOUCH_HELD,
            ..input()
        },
        Some(&hits),
        true,
    );
    assert_eq!(state.value(), 20);
    assert_eq!(
        state
            .update(
                Input {
                    touch_x: 150,
                    touch_y: 150,
                    touch_flags: model::TOUCH_RELEASED,
                    ..input()
                },
                Some(&hits),
                true
            )
            .outcome,
        Outcome::Cancel
    );
}
#[test]
fn partial_interaction_generation_is_invisible_until_complete_publication() {
    use crate::percent;
    use xp_ui::model::TOUCH_RELEASED;
    use xp_ui::scene::HitSink;
    let mut state = percent::State::new(50);
    let mut published = percent::Controls::default();
    let mut next = percent::Controls::default();
    let old = Interaction {
        rect: Rect {
            x: 10,
            y: 10,
            w: 20,
            h: 20,
        },
        action: STEP,
        value: -1,
        inputs: 1,
        ..Default::default()
    };
    published.push(old).unwrap();
    let input = Input {
        touch_x: 15,
        touch_y: 15,
        touch_flags: TOUCH_RELEASED,
        ..Default::default()
    };
    next.push(Interaction { value: 1, ..old }).unwrap();
    state.update(input, Some(published.as_slice()), true);
    assert_eq!(state.value(), 49);
    published = next;
    state.update(input, Some(published.as_slice()), true);
    assert_eq!(state.value(), 50);
    for _ in 1..5 {
        next.push(old).unwrap();
    }
    assert!(next.push(old).is_err());
    assert_eq!(published.as_slice().len(), 1);
}
