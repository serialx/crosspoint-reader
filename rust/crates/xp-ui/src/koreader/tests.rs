use super::*;
use crate::{
    model::{self, Input},
    scene::{Arena, Command, Interaction, Rect},
};
#[test]
fn timer_preserves_unsigned_wrap_and_zero_sentinel() {
    let mut s = State {
        smart: 1,
        phase: Phase::Uploaded as u8,
        ..State::default()
    };
    s.mark_return(u32::MAX - 15);
    assert_eq!(s.auto_return_at, 1184);
    let i = Input {
        now_ms: u32::MAX - 15,
        ..Input::default()
    };
    assert!(s.interest(i.now_ms).due(&i));
    assert_eq!(s.update(&i).kind, 1);
    s.mark_return(u32::MAX - 1199);
    assert_eq!(s.auto_return_at, 0);
    assert_eq!(s.interest(10).deadline_enabled, 0);
    assert_eq!(
        s.update(&Input {
            now_ms: 10,
            ..Input::default()
        })
        .kind,
        0
    );
}
#[test]
fn press_release_and_terminal_policies() {
    for phase in 0..12 {
        let mut s = State {
            phase,
            ..State::default()
        };
        assert_eq!(
            s.update(&Input {
                pressed: model::CONFIRM,
                ..Input::default()
            })
            .kind,
            0
        );
    }
    let mut s = State {
        phase: Phase::Result as u8,
        ..State::default()
    };
    let effect = s.update(&Input {
        released: model::DOWN | model::CONFIRM | model::BACK,
        ..Input::default()
    });
    assert_eq!((effect.kind, effect.value, s.selected), (2, 1, 1));
    assert_eq!(s.update(&Input::default()).kind, 1);
    assert_eq!(s.update(&Input::default()).kind, 0);
    let mut auth = State {
        kind: 1,
        phase: Phase::Authenticated as u8,
        ..State::default()
    };
    assert_eq!(
        auth.update(&Input {
            released: model::CONFIRM,
            ..Input::default()
        })
        .kind,
        0
    );
    assert_eq!(
        auth.update(&Input {
            pressed: model::CONFIRM,
            ..Input::default()
        })
        .value,
        3
    );
    assert_eq!(
        auth.update(&Input {
            touch_flags: model::TOUCH_RELEASED,
            touch_x: -1,
            touch_y: -1,
            ..Input::default()
        })
        .kind,
        0
    );
    assert_eq!(
        auth.update(&Input {
            touch_flags: model::TOUCH_RELEASED,
            touch_x: 10,
            touch_y: 20,
            ..Input::default()
        })
        .kind,
        2
    );
}
#[test]
fn release_routes_published_row_without_requiring_a_press() {
    let mut s = State {
        phase: Phase::Result as u8,
        ..State::default()
    };
    assert_eq!(
        s.update(&Input {
            action: 1,
            value: 1,
            touch_flags: model::TOUCH_PRESSED,
            ..Input::default()
        })
        .invalidate,
        0
    );
    let effect = s.update(&Input {
        action: 1,
        value: 1,
        touch_flags: model::TOUCH_RELEASED,
        ..Input::default()
    });
    assert_eq!((effect.kind, effect.value), (2, 1));
    let effect = s.update(&Input {
        action: 0,
        value: 0,
        touch_flags: model::TOUCH_RELEASED,
        ..Input::default()
    });
    assert_eq!(effect.kind, 0);
    assert_eq!(
        s.update(&Input {
            swipe: model::SWIPE_DOWN,
            ..Input::default()
        })
        .kind,
        0
    );
}
#[test]
fn command_budget_and_invalid_layout() {
    for (w, h) in [(528, 792), (792, 528), (480, 800), (800, 480)] {
        for kind in 0..3 {
            for phase in 0..12 {
                let mut commands = [Command::default(); 32];
                let mut hits = [Interaction::default(); 2];
                let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                let mut l = Layout::default();
                l.list.screen = Rect { x: 0, y: 0, w, h };
                l.list.area = l.list.screen;
                l.safe = l.list.screen;
                l.header = Rect {
                    x: 0,
                    y: 10,
                    w,
                    h: 38,
                };
                l.header_bottom = 48;
                l.hints_height = 48;
                l.spacing = 8;
                l.body_height = 24;
                l.small_height = 18;
                l.fixed_height = 18;
                l.list.row_height = 44;
                l.space_xs = 2;
                l.space_sm = 4;
                l.space_md = 8;
                l.space_lg = 16;
                l.has_device = 1;
                view::view(
                    &mut arena,
                    State {
                        kind,
                        phase,
                        ..State::default()
                    },
                    l,
                )
                .unwrap();
                let scene = arena.finish(crate::scene::Refresh::Fast, 1).unwrap();
                assert!(scene.commands.len() <= if kind == 0 { 32 } else { 6 });
                let mut commands = [Command::default(); 32];
                let mut hits = [Interaction::default(); 2];
                let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                l.space_lg = i16::MAX;
                assert!(view::view(
                    &mut arena,
                    State {
                        kind,
                        phase,
                        ..State::default()
                    },
                    l
                )
                .is_err());
            }
        }
    }
}
#[test]
fn wire_layouts() {
    assert_eq!(core::mem::size_of::<State>(), 12);
    assert_eq!(core::mem::size_of::<Options>(), 12);
    assert_eq!(core::mem::offset_of!(Options, local_percentage), 0);
    assert_eq!(core::mem::offset_of!(Options, smart), 4);
}
