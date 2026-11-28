extern crate std;

use super::*;
use crate::model::*;
use core::ffi::CStr;
#[test]
fn frozen_navigation() {
    let events = [
        (1, 0, 1, 100, 0),
        (0, 1, 0, 150, 50),
        (2, 0, 2, 200, 0),
        (0, 2, 0, 250, 50),
        (0, 8, 0, 300, 0),
        (1, 0, 1, 400, 0),
        (0, 0, 1, 901, 501),
        (0, 0, 1, 1401, 1001),
        (0, 0, 1, 1402, 1002),
        (0, 1, 0, 1450, 1050),
        (2, 0, 2, 1600, 0),
        (0, 0, 2, 2101, 501),
        (0, 2, 0, 2150, 550),
        (0, 8, 0, 2200, 0),
        (0, 4, 0, 2300, 0),
    ];
    let mut expected = include_str!("../../../../../test/text_settings/navigation.golden").lines();
    let map = |n: u32| {
        if n == 1 {
            NAV_NEXT
        } else if n == 2 {
            NAV_PREVIOUS
        } else if n == 4 {
            BACK
        } else if n == 8 {
            CONFIRM
        } else {
            0
        }
    };
    for tab in 0..4 {
        for ring in 0..5 {
            let mut s = State::new(tab, 0, 0, 3, 5, 0);
            s.tabs = [ListState::new(1); 4];
            s.tabs[usize::from(tab)].selected = ring;
            for (p, r, h, t, d) in events {
                let e = s.update(Input {
                    pressed: map(p),
                    released: map(r),
                    held: map(h),
                    now_ms: t,
                    held_ms: d,
                    ..Input::default()
                });
                let action = if e.kind == EffectKind::Cancel as u8 {
                    -2
                } else if e.kind == EffectKind::Activate as u8 {
                    e.value
                } else {
                    -1
                };
                let actual = std::format!(
                    "{} {} {} {} {} {} {} {} {}",
                    s.tab,
                    s.tabs[0].selected,
                    s.tabs[1].selected,
                    s.tabs[2].selected,
                    s.tabs[3].selected,
                    action,
                    e.invalidate,
                    s.started_on_tabs,
                    s.last_repeat
                );
                assert_eq!(Some(actual.as_str()), expected.next());
            }
        }
    }
    assert_eq!(expected.next(), None);
}
#[test]
fn edits_match_settings_encoding() {
    for value in 0..=255 {
        let values = Values {
            fields: [value; 11],
        };
        for tab in 2..4 {
            let s = State::new(tab, 0, 0, 1, 1, 0);
            for row in 0..s.count() {
                let edit = s.edit(row, values);
                let field = if tab == 2 { row as u8 } else { row as u8 + 7 };
                assert_eq!(edit.field, field);
                if field == 3 || field >= 7 {
                    assert_eq!(edit.kind, SAVE);
                    assert_eq!(edit.value, i32::from(value == 0));
                } else {
                    assert_eq!(edit.kind, OPTIONS);
                    assert!(edit.selected < edit.count);
                }
            }
        }
    }
    assert_eq!(choice(Field::Word as u8, 6), 200);
    assert_eq!(choice(Field::Margin as u8, 7), 40);
}
#[test]
fn labels_borrow_i18n_and_localize() {
    let mut scratch = [0; 16];
    let en = labels::label(0, 0, 0, 0, &mut scratch);
    let fr = xp_core::i18n::language_from_code(b"FR");
    let translated = labels::label(fr, 0, 0, 0, &mut scratch);
    assert_eq!(unsafe { CStr::from_ptr(en) }.to_bytes(), b"Font");
    let languages = option_env!("XP_I18N_BUILTIN_LANGS").unwrap_or("all").trim();
    if languages.is_empty()
        || languages.eq_ignore_ascii_case("all")
        || languages
            .split(',')
            .any(|code| code.trim().eq_ignore_ascii_case("FR"))
    {
        assert_ne!(unsafe { CStr::from_ptr(en) }, unsafe {
            CStr::from_ptr(translated)
        });
    } else {
        assert_eq!(translated, en);
    }
    assert_eq!(
        translated,
        xp_core::i18n::get(fr, xp_core::i18n::ids::STR_FONT)
    );
    assert_eq!(
        unsafe { CStr::from_ptr(labels::label(0, 3, 1, 125, &mut scratch)) }.to_bytes(),
        b"125%"
    );
    assert!(labels::label(0, 3, 1, 125, &mut []).is_null());
}
#[test]
fn publication_does_not_replace_new_input() {
    let mut live = State::new(2, 0, 0, 1, 1, 0);
    let mut rendered = live;
    live.update(Input {
        pressed: NAV_NEXT,
        ..Input::default()
    });
    rendered.tabs[2].drawn = 4;
    rendered.tabs[2].top = 1;
    live.publish(&rendered, 0);
    assert_eq!(live.tabs[2].selected, 1);
    assert_eq!(live.tabs[2].follow, 1);
    assert_eq!(live.tabs[2].drawn, 4);
}
#[test]
fn preview_geometry_and_line_positions() {
    use preview::*;
    let key = Key {
        font_id: 3,
        margin: 5,
        paragraph_gap: 1,
        ..Key::default()
    };
    let g = geometry(
        crate::scene::Rect {
            x: 10,
            y: 60,
            w: 500,
            h: 180,
        },
        8,
        6,
        14,
        20,
        24,
        key,
    )
    .unwrap();
    assert_eq!(
        g.text,
        crate::scene::Rect {
            x: 23,
            y: 68,
            w: 474,
            h: 144
        }
    );
    assert_eq!(g.caption.y, 218);
    assert_eq!(g.gap, 12);
    assert_eq!(line(0, 3, 144, 20, 24, 12), Some(([0, 108], 3)));
    assert_eq!(line(1, 2, 144, 20, 24, 12), Some(([24, 132], 1)));
    assert_eq!(line(i32::MAX, 2, 144, 20, 24, 12), None);
}
#[test]
fn invalid_descriptors_leave_outputs() {
    use core::ptr;
    unsafe {
        assert_eq!(
            ffi::view(
                ptr::null(),
                ptr::null(),
                ptr::null(),
                ptr::null(),
                ptr::null_mut(),
                ptr::null_mut(),
                ptr::null_mut()
            ),
            0
        );
        assert_eq!(ffi::edit(ptr::null(), 0, ptr::null()).kind, 0);
        assert_eq!(ffi::same(ptr::null(), ptr::null()), 0);
        assert_eq!(ffi::capacity(ptr::null(), 1, 1, 1), usize::MAX);
        assert_eq!(ffi::line(1, 1, 1, 1, 1, 1, ptr::null_mut()), 0);
        assert!(ffi::label(0, 3, 1, 100, ptr::null_mut(), 1).is_null());
        let mut state = State::new(0, 0, 0, 1, 1, 0);
        ffi::init(&mut state, 255, i32::MAX, 0, 1, 1, 0);
        assert_eq!(state.tab, 0);
        state.tabs[0].selected = i32::MAX;
        assert_eq!(
            state
                .update(Input {
                    pressed: NAV_NEXT,
                    ..Input::default()
                })
                .invalidate,
            0
        );
    }
}
#[test]
fn touch_and_swipe_use_published_actions() {
    let mut state = State::new(0, 0, 0, 10, 5, 0);
    assert_eq!(
        state
            .update(Input {
                touch_flags: TOUCH_PRESSED,
                action: 1,
                value: 2,
                ..Input::default()
            })
            .invalidate,
        0
    );
    let effect = state.update(Input {
        touch_flags: TOUCH_RELEASED,
        action: 1,
        value: 2,
        ..Input::default()
    });
    assert_eq!(effect.kind, EffectKind::Activate as u8);
    assert_eq!(effect.value, 2);
    assert_eq!(state.tabs[0].selected, 3);
    state.tabs[0].drawn = 4;
    let effect = state.update(Input {
        swipe: SWIPE_UP,
        ..Input::default()
    });
    assert_eq!(effect.value, 4);
    assert_eq!(state.tabs[0].selected, 3);
    assert_eq!(state.tabs[0].pending_scroll, 4);
    state.update(Input {
        touch_flags: TOUCH_RELEASED,
        action: 2,
        value: 3,
        ..Input::default()
    });
    assert_eq!(state.tab, 3);
    assert_eq!(state.tabs[3].selected, 0);
}
