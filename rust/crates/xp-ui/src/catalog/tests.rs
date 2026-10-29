extern crate std;
use self::std::vec::Vec;
use super::state::*;
use crate::model::*;
#[test]
fn original_catalog_dispatch() {
    let golden = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../test/ui_catalog/input.golden"
    ));
    for line in golden.lines() {
        let values: Vec<i32> = line
            .split_whitespace()
            .skip(1)
            .map(|n| n.parse().unwrap())
            .collect();
        let [mode, flags, event, consumed, result, pending] = values[..] else {
            panic!("bad fixture");
        };
        let mut s = State::new(BROWSER, -1, 0);
        s.mode = mode as u8;
        s.list.selected = i32::from(flags & 2 != 0);
        s.search_pending = u8::from(flags & 4 != 0);
        let mut input = Input {
            touch_x: 0,
            touch_y: 0,
            ..Input::default()
        };
        let buttons = [
            BACK,
            CONFIRM,
            NAV_NEXT,
            NAV_PREVIOUS,
            PAGE_BACK,
            PAGE_FORWARD,
        ];
        if event < 6 {
            input.pressed = buttons[event as usize];
        } else if event < 12 {
            input.released = buttons[(event - 6) as usize];
        } else if event < 18 {
            input.held = buttons[(event - 12) as usize];
        } else if event == 18 {
            input.touch_flags = TOUCH_RELEASED;
        }
        let e = s.custom(input, flags & 1 != 0, if flags & 8 != 0 { 5 } else { 0 });
        assert_eq!(e.is_some(), consumed != 0, "{line}");
        let actual = e.map_or(0, |e| {
            if e.kind == PAGE {
                if e.value == 2 {
                    10
                } else {
                    11
                }
            } else {
                i32::from(e.kind)
            }
        });
        assert_eq!(actual, result, "{line}");
        assert_eq!(s.search_pending, pending as u8, "{line}");
    }
}
#[test]
fn held_search_clears_after_selection_moves() {
    let mut s = State::new(BROWSER, -1, 0);
    s.list.visible = 4;
    s.list.drawn = 4;
    s.list.drawn_count = 14;
    let press = Input {
        pressed: NAV_PREVIOUS,
        ..Input::default()
    };
    assert_eq!(s.update(press, 14, true, 0).kind, 0);
    assert_eq!(s.search_pending, 1);
    let hold = Input {
        held: NAV_PREVIOUS,
        held_ms: 501,
        now_ms: 501,
        ..Input::default()
    };
    s.update(hold, 14, true, 0);
    assert_ne!(s.list.selected, 0);
    assert_eq!(s.search_pending, 0);
    assert_eq!(
        s.update(
            Input {
                released: NAV_PREVIOUS,
                ..Input::default()
            },
            14,
            true,
            0
        )
        .kind,
        0
    );
}

#[test]
fn transfer_cadence_and_cancel() {
    let mut s = State::new(BROWSER, -1, 0);
    s.mode = DOWNLOADING;
    let e = s.update(
        Input {
            touch_flags: TOUCH_RELEASED,
            action: u32::from(CANCEL_TRANSFER),
            ..Input::default()
        },
        0,
        false,
        0,
    );
    assert_eq!(e.kind, CANCEL_TRANSFER);
    assert_eq!(s.cancel, 1);
}

#[test]
fn original_transfer_and_sleep_policy() {
    let source = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../test/ui_catalog/progress.golden"
    ));
    let mut key = None;
    let mut s = State::new(BROWSER, -1, 0);
    for line in source.lines() {
        let fields: Vec<_> = line.split_whitespace().collect();
        let value = |i: usize| fields[i].parse::<u32>().unwrap();
        if fields[0] == "awake" {
            s.mode = value(1) as u8;
            assert_eq!(s.awake(), value(2) != 0, "{line}");
            continue;
        }
        let next = (value(1), value(2));
        if key != Some(next) {
            s.set_mode(DOWNLOADING);
            key = Some(next);
        }
        assert_eq!(
            s.progress(value(3), value(1), value(4)),
            value(5) != 0,
            "{line}"
        );
    }
}

#[test]
fn failed_view_preserves_publication_and_hit_output() {
    use super::{ffi, hits::Hits, view::Layout};
    use crate::{
        ffi::{Buffer, SceneView},
        scene::{Command, Interaction, Rect},
    };
    use core::ffi::{c_char, c_void};
    unsafe extern "C" fn row(_: *mut c_void, _: u16, _: *mut ffi::DataRow) -> u8 {
        0
    }
    unsafe extern "C" fn text(_: *mut c_void, _: u32) -> *const c_char {
        c"No entries".as_ptr()
    }
    unsafe extern "C" fn width(_: *mut c_void, _: *const c_char, _: i32, _: u8) -> i32 {
        40
    }
    unsafe extern "C" fn height(
        _: *mut c_void,
        _: *const c_char,
        _: i32,
        _: u8,
        _: u8,
        _: i16,
    ) -> i32 {
        24
    }
    unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
        24
    }
    let services = ffi::Services {
        context: core::ptr::null_mut(),
        row: Some(row),
        text: Some(text),
        width: Some(width),
        height: Some(height),
        line: Some(line),
        count: 0,
        reserved: 0,
    };
    let state = State::new(BROWSER, -1, 0);
    let mut layout = Layout::default();
    layout.list.screen = Rect {
        x: 0,
        y: 0,
        w: 480,
        h: 800,
    };
    layout.list.area = layout.list.screen;
    let mut commands = [Command::default(); 8];
    let mut buffer = Buffer {
        commands: commands.as_mut_ptr(),
        hits: core::ptr::null_mut(),
        command_capacity: 0,
        hit_capacity: 0,
    };
    let mut next = ListState::new(77);
    let mut scene = SceneView {
        command_count: 77,
        commands: core::ptr::null(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 0,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    let mut hits = Hits {
        rows: 7,
        ..Hits::default()
    };
    assert_eq!(
        unsafe {
            ffi::view(
                &state, &layout, &services, &buffer, &mut next, &mut scene, &mut hits,
            )
        },
        2
    );
    assert_eq!((next.selected, scene.command_count, hits.rows), (77, 77, 7));
    buffer.command_capacity = 8;
    buffer.hit_capacity = 1;
    assert_eq!(
        unsafe {
            ffi::view(
                &state, &layout, &services, &buffer, &mut next, &mut scene, &mut hits,
            )
        },
        0
    );
    assert_eq!((next.selected, scene.command_count, hits.rows), (77, 77, 7));
    buffer.hit_capacity = 0;
    assert_eq!(
        unsafe {
            ffi::view(
                &state, &layout, &services, &buffer, &mut next, &mut scene, &mut hits,
            )
        },
        1
    );
    assert_eq!(hits.rows, 0);
    let mut hit = Interaction {
        action: 99,
        ..Interaction::default()
    };
    assert_eq!(unsafe { ffi::hit(&hits, 1, 1, &mut hit) }, 0);
    assert_eq!(hit.action, 99);
}
