use super::*;
use crate::{
    ffi::{Buffer, SceneView},
    network::ffi,
};
use core::{mem::MaybeUninit, ptr};

unsafe extern "C" fn row(_: *mut c_void, _: u16, out: *mut settings_ffi::DataRow) -> u8 {
    unsafe {
        out.write(settings_ffi::DataRow {
            label: c"Network".as_ptr(),
            enabled: 1,
            ..settings_ffi::DataRow::default()
        });
    }
    1
}
unsafe extern "C" fn width(_: *mut c_void, _: *const c_char, _: i32, _: u8) -> i32 {
    42
}
unsafe extern "C" fn height(_: *mut c_void, _: *const c_char, _: i32, _: u8, _: u8, _: i16) -> i32 {
    18
}
unsafe extern "C" fn line(_: *mut c_void, _: i32) -> i32 {
    18
}
unsafe extern "C" fn text(_: *mut c_void, _: u32) -> *const c_char {
    c"Network".as_ptr()
}
fn services(count: u16) -> Services {
    Services {
        rows: settings_ffi::Services {
            context: ptr::null_mut(),
            row: Some(row),
            width: Some(width),
            height: Some(height),
            line: Some(line),
            count,
            reserved: 0,
        },
        text: Some(text),
    }
}
fn layout() -> Layout {
    let screen = Rect {
        x: 0,
        y: 0,
        w: 480,
        h: 800,
    };
    let mut l = Layout::default();
    l.base.safe = screen;
    l.base.list.screen = screen;
    l.base.list.area = screen;
    l.base.list.row_height = 40;
    l.base.list.min_touch = 40;
    l.base.header_height = 40;
    l.base.tab_height = 24;
    l.base.spacing = 8;
    l
}

#[test]
fn dormant_safe_builder_matches_parent_wrapper_in_all_phases_and_held_touch() {
    for phase in 0..=9 {
        for active in [-1, 1] {
            let s = State {
                phase,
                count: 20,
                real_count: 19,
                generation: 7,
                list: ListState {
                    top: 3,
                    active,
                    ..ListState::new(4)
                },
                ..State::default()
            };
            let l = layout();
            let mut safe_commands = [Command::default(); 256];
            let (next, hits, count, refresh) =
                build(&mut safe_commands, &s, &l, &mut services(s.count)).unwrap();
            let mut legacy_commands = [Command::default(); 256];
            let buffer = Buffer {
                commands: legacy_commands.as_mut_ptr(),
                command_capacity: 256,
                hits: ptr::null_mut(),
                hit_capacity: 0,
            };
            let mut legacy_next = ListState::new(0);
            let mut legacy_hits = Hits::default();
            let mut scene = MaybeUninit::<SceneView>::uninit();
            assert_eq!(
                unsafe {
                    ffi::wifi_view(
                        &s,
                        &l,
                        &services(s.count),
                        &buffer,
                        &mut legacy_next,
                        scene.as_mut_ptr(),
                        &mut legacy_hits,
                    )
                },
                1
            );
            let scene = unsafe { scene.assume_init() };
            assert_eq!(next, legacy_next);
            assert_eq!(count, usize::from(scene.command_count));
            assert_eq!(refresh as u8, scene.refresh);
            assert_eq!(&safe_commands[..count], &legacy_commands[..count]);
            assert_eq!(hits.rows.count, legacy_hits.rows.count);
            assert_eq!(hits.rows.first_index, legacy_hits.rows.first_index);
            assert_eq!(hits.rows.heights, legacy_hits.rows.heights);
            assert_eq!(hits.buttons, legacy_hits.buttons);
            for y in (0..800).step_by(17) {
                assert_eq!(hits.hit(&s, 100, y), legacy_hits.hit(&s, 100, y));
            }
            if phase == Phase::List as u8 {
                assert!(hits.rows.count > 0);
                assert_eq!(next.active, active);
                assert_eq!(hits.rows.first_index, 3);
            }
        }
    }
}

#[test]
fn dormant_safe_builder_rejects_short_storage_and_invalid_state() {
    let s = State {
        phase: Phase::Scanning as u8,
        ..State::default()
    };
    assert!(matches!(
        build(&mut [], &s, &layout(), &mut services(0)),
        Err(Error::Capacity)
    ));
    let mut commands = [Command::default(); 257];
    assert!(matches!(
        build(&mut commands, &s, &layout(), &mut services(0)),
        Err(Error::Invalid)
    ));
    let invalid = State { phase: 255, ..s };
    assert!(matches!(
        build(&mut commands[..256], &invalid, &layout(), &mut services(0)),
        Err(Error::Invalid)
    ));
}
