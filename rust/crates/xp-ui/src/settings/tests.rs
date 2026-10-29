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
