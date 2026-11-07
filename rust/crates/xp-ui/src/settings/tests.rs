use super::{
    hits,
    settings_ffi::{self, DataRow, Services},
    view::Layout,
};
use crate::{
    ffi::{Buffer, SceneView},
    model::ListState,
    scene::{Command, Rect},
};
use core::ffi::{c_char, c_void};
unsafe extern "C" fn row(_: *mut c_void, _: u16, o: *mut DataRow) -> u8 {
    unsafe {
        *o = DataRow {
            label: c"Setting".as_ptr(),
            ..DataRow::default()
        };
    }
    1
}
unsafe extern "C" fn width(_: *mut c_void, _: *const c_char, _: i32, _: u8) -> i32 {
    7
}
unsafe extern "C" fn height(_: *mut c_void, _: *const c_char, _: i32, _: u8, _: u8, _: i16) -> i32 {
    1
}
unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
    1
}
fn services(count: u16) -> Services {
    Services {
        context: core::ptr::null_mut(),
        row: Some(row),
        width: Some(width),
        height: Some(height),
        line: Some(line),
        count,
        reserved: 0,
    }
}
fn layout() -> Layout {
    let mut l = Layout {
        kind: 2,
        ..Layout::default()
    };
    l.list.screen = Rect {
        x: 0,
        y: 0,
        w: 480,
        h: 800,
    };
    l.list.area = l.list.screen;
    l.safe = l.list.screen;
    l
}

#[test]
fn typed_preparation_matches_wire_output_and_failed_publication() {
    for kind in 0..12 {
        for touch in 0..2 {
            for capacity in [0, 8, 24, 32, 48, 128] {
                let mut l = layout();
                l.kind = kind;
                l.touch = touch;
                let state = ListState::new(5);
                let mut wire_commands = [Command::default(); 128];
                let mut typed_commands = wire_commands;
                let buffer = Buffer {
                    commands: wire_commands.as_mut_ptr(),
                    hits: core::ptr::null_mut(),
                    command_capacity: capacity,
                    hit_capacity: 0,
                };
                let mut wire_next = ListState::new(555);
                let mut wire_hits = hits::Layout::default();
                let mut scene: SceneView = unsafe { core::mem::zeroed() };
                let wire = unsafe {
                    settings_ffi::view(
                        &state,
                        &l,
                        &services(6),
                        &buffer,
                        &mut wire_next,
                        &mut scene,
                        &mut wire_hits,
                    )
                };
                let mut next = ListState::new(555);
                let mut routing = hits::Layout::default();
                let result = super::view::prepare(
                    &state,
                    &l,
                    &mut services(6),
                    6,
                    &mut typed_commands[..usize::from(capacity)],
                    &mut next,
                    &mut routing,
                );
                if wire == 1 {
                    let prepared = result.expect("wire succeeded");
                    assert_eq!(prepared.command_count, scene.command_count);
                    assert_eq!(prepared.refresh as u8, scene.refresh);
                    assert_eq!(next.selected, wire_next.selected);
                    assert_eq!(next.navigation_revision, wire_next.navigation_revision);
                    assert_eq!(routing.count, wire_hits.count);
                    assert_eq!(routing.disabled, wire_hits.disabled);
                    for i in 0..usize::from(scene.command_count) {
                        assert_eq!(typed_commands[i], wire_commands[i]);
                    }
                    for y in 0..800 {
                        assert_eq!(routing.hit(100, y), wire_hits.hit(100, y));
                    }
                } else {
                    assert_eq!(
                        result.err(),
                        Some(if wire == 2 {
                            crate::scene::Error::Capacity
                        } else {
                            crate::scene::Error::Invalid
                        })
                    );
                    assert_eq!(next.selected, 555);
                    assert_eq!(routing.count, 0);
                }
            }
        }
    }
}
#[test]
fn too_many_visible_rows_preserves_publication() {
    // The real timezone resource table is the longest list in this family.
    let source = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../src/util/Timezones.cpp"
    ));
    let count = source
        .lines()
        .filter(|s| s.trim_start().starts_with("{\""))
        .count() as u16;
    assert!(count > 32);
    let mut commands = [Command::default(); 256];
    let buffer = Buffer {
        commands: commands.as_mut_ptr(),
        hits: core::ptr::null_mut(),
        command_capacity: 256,
        hit_capacity: 0,
    };
    let state = ListState::new(0);
    let mut next = ListState::new(555);
    let mut scene = SceneView {
        command_count: 123,
        commands: core::ptr::null(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 0,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    let mut hits = hits::Layout {
        count: 9,
        ..hits::Layout::default()
    };
    assert_eq!(
        unsafe {
            settings_ffi::view(
                &state,
                &layout(),
                &services(count),
                &buffer,
                &mut next,
                &mut scene,
                &mut hits,
            )
        },
        2
    );
    assert_eq!(
        (next.selected, scene.command_count, hits.count),
        (555, 123, 9)
    );
}
#[test]
fn invalid_kind_and_callback_do_not_publish() {
    let mut commands = [Command::default(); 128];
    let buffer = Buffer {
        commands: commands.as_mut_ptr(),
        hits: core::ptr::null_mut(),
        command_capacity: 128,
        hit_capacity: 0,
    };
    let state = ListState::new(0);
    let mut next = ListState::new(555);
    let mut scene = SceneView {
        command_count: 123,
        commands: core::ptr::null(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 0,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    let mut hits = hits::Layout {
        count: 9,
        ..hits::Layout::default()
    };
    let mut l = layout();
    l.kind = 255;
    assert_eq!(
        unsafe {
            settings_ffi::view(
                &state,
                &l,
                &services(1),
                &buffer,
                &mut next,
                &mut scene,
                &mut hits,
            )
        },
        0
    );
    let mut s = services(1);
    s.row = None;
    assert_eq!(
        unsafe {
            settings_ffi::view(
                &state,
                &layout(),
                &s,
                &buffer,
                &mut next,
                &mut scene,
                &mut hits,
            )
        },
        0
    );
    assert_eq!(
        (next.selected, scene.command_count, hits.count),
        (555, 123, 9)
    );
}
#[test]
fn touch_uses_published_geometry_until_replacement() {
    use crate::scene::{HitSink, Interaction};
    let mut published = hits::Layout::default();
    for i in 0..3 {
        published
            .push(Interaction {
                rect: Rect {
                    x: 0,
                    y: 100 + i * 40,
                    w: 480,
                    h: 36,
                },
                value: i32::from(i),
                action: 1,
                generation: 1,
                inputs: 1,
                flags: 0,
            })
            .unwrap();
    }
    let mut next = hits::Layout::default();
    for i in 0..3 {
        next.push(Interaction {
            rect: Rect {
                x: 0,
                y: 200 + i * 40,
                w: 480,
                h: 36,
            },
            value: i32::from(i) + 10,
            action: 1,
            generation: 1,
            inputs: 1,
            flags: 0,
        })
        .unwrap();
    }
    assert_eq!(published.hit(100, 150).unwrap().value, 1);
    assert!(next.hit(100, 150).is_none());
    published = next;
    assert!(published.hit(100, 150).is_none());
    assert_eq!(published.hit(100, 250).unwrap().value, 11);
}

#[test]
fn long_touch_selects_without_moving_viewport() {
    use crate::model::{Input, ListState};
    let mut state = ListState {
        selected: 0,
        top: 1,
        follow: 0,
        active: 1,
        ..ListState::new(0)
    };
    let next = state;
    let input = Input {
        action: 2,
        value: 1,
        ..Input::default()
    };
    let effect = unsafe { super::settings_ffi::update(&mut state, &input, 8) };
    assert_eq!((effect.kind, effect.value), (4, 1));
    assert_eq!(
        (state.selected, state.active, state.top, state.follow),
        (1, -1, 1, 0)
    );
    state.publish(&next, 0);
    assert_eq!(state.selected, 1);
    for value in [-1, 8] {
        let input = Input {
            action: 2,
            value,
            ..Input::default()
        };
        let effect = unsafe { super::settings_ffi::update(&mut state, &input, 8) };
        assert_eq!(effect.kind, 0);
        assert_eq!(state.selected, 1);
    }
}
