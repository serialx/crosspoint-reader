extern crate std;
use super::{ffi, state::*, Feedback, Layout, Services};
use crate::{
    ffi::{Buffer, SceneView},
    scene::{Command, Rect},
    settings::settings_ffi,
};
use core::{
    ffi::{c_char, c_void},
    ptr,
};
unsafe extern "C" fn text(_: *mut c_void, _: u32) -> *const c_char {
    c"fixture".as_ptr()
}
unsafe extern "C" fn width(_: *mut c_void, _: *const c_char, _: i32, _: u8) -> i32 {
    100
}
unsafe extern "C" fn height(_: *mut c_void, _: u32, _: i32, _: i16, _: u8, _: u8) -> i16 {
    24
}
unsafe extern "C" fn row_height(
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
unsafe extern "C" fn row(context: *mut c_void, _: u16, out: *mut settings_ffi::DataRow) -> u8 {
    unsafe {
        *out = settings_ffi::DataRow {
            label: c"fixture".as_ptr(),
            subtitle: if context as usize & 1 == 0 {
                c"detail".as_ptr()
            } else {
                ptr::null()
            },
            value: if context as usize & 2 == 0 {
                c"value".as_ptr()
            } else {
                ptr::null()
            },
            ..Default::default()
        };
    }
    1
}
fn resources(count: u16) -> Services {
    Services {
        rows: settings_ffi::Services {
            context: ptr::null_mut(),
            row: Some(row),
            width: Some(width),
            height: Some(row_height),
            line: Some(line),
            count,
            reserved: 0,
        },
        context: ptr::null_mut(),
        text: Some(text),
        text_height: Some(height),
    }
}
fn layout(width: i16, height: i16) -> Layout {
    let mut layout = Layout::default();
    layout.list.list.screen = Rect {
        x: 0,
        y: 0,
        w: width,
        h: height,
    };
    layout.list.list.area = layout.list.list.screen;
    layout.list.list.row_height = 48;
    layout.list.list.row_gap = 4;
    layout.list.list.padding = 16;
    layout.list.side_padding = 16;
    layout.list.top_padding = 12;
    layout.list.header_height = 36;
    layout.list.hints_height = 40;
    layout.list.spacing = 8;
    layout.progress_height = 24;
    layout.popup_padding = 16;
    layout.popup_gap = 8;
    layout.popup_side_margin = 20;
    layout.selection_padding = 8;
    layout
}
fn scene() -> SceneView {
    SceneView {
        commands: ptr::null(),
        text: ptr::null(),
        interactions: ptr::null(),
        generation: 42,
        command_count: 0,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    }
}
#[test]
fn overflow_preserves_publication_and_recovers() {
    let mut maximum = [0u16; 4];
    for (width, height) in [(480, 800), (528, 792), (792, 528), (800, 480)] {
        for kind in 0..4 {
            for phase in 0..=match kind {
                0 => 7,
                1 => 5,
                2 => 6,
                _ => 3,
            } {
                let mut state = State::new(kind, 0);
                state.phase = phase;
                state.popup_active = 1;
                let mut commands = [Command::default(); 128];
                let mut output = scene();
                let mut feedback = Feedback {
                    last_percent: 555,
                    ..Default::default()
                };
                let mut buffer = Buffer {
                    commands: commands.as_mut_ptr(),
                    hits: ptr::null_mut(),
                    command_capacity: 1,
                    hit_capacity: 0,
                };
                let layout = layout(width, height);
                let resources = resources(20);
                assert_eq!(
                    unsafe {
                        ffi::build(
                            &state,
                            &layout,
                            &resources,
                            &buffer,
                            &mut feedback,
                            &mut output,
                        )
                    },
                    2
                );
                assert_eq!(output.generation, 42);
                assert_eq!(feedback.last_percent, 555);
                state.service(Event::SceneFailed, 0, 0);
                buffer.command_capacity = 128;
                assert_eq!(
                    unsafe {
                        ffi::build(
                            &state,
                            &layout,
                            &resources,
                            &buffer,
                            &mut feedback,
                            &mut output,
                        )
                    },
                    1,
                    "kind={kind} phase={phase} {width}x{height}"
                );
                let snapshot = state;
                unsafe { ffi::publish(&mut state, &snapshot, &feedback) };
                assert_eq!(state.reserved, [0; 3]);
                assert!(output.command_count <= 128);
                maximum[usize::from(kind)] = maximum[usize::from(kind)].max(output.command_count);
            }
        }
    }
    std::println!("maximum commands by OTA/SD/fonts/cache: {maximum:?}");
}
#[test]
fn invalid_geometry_and_state_reject_without_publishing() {
    let resources = resources(1);
    let mut commands = [Command::default(); 128];
    let buffer = Buffer {
        commands: commands.as_mut_ptr(),
        hits: ptr::null_mut(),
        command_capacity: 128,
        hit_capacity: 0,
    };
    for invalid in 0..4 {
        let mut state = State::new(3, 0);
        let mut layout = layout(528, 792);
        match invalid {
            0 => state.popup_selected = 255,
            1 => layout.popup_padding = i16::MAX,
            2 => layout.list.side_padding = -1,
            _ => state.phase = 255,
        }
        let mut output = scene();
        let mut feedback = Feedback::default();
        assert_eq!(
            unsafe {
                ffi::build(
                    &state,
                    &layout,
                    &resources,
                    &buffer,
                    &mut feedback,
                    &mut output,
                )
            },
            0
        );
        assert_eq!(output.generation, 42);
    }
}

#[test]
fn dense_font_catalog_command_bound() {
    let mut maximum = 0;
    for (width, height) in [(480, 800), (528, 792), (792, 528), (800, 480)] {
        for variant in 0..4usize {
            let mut layout = layout(width, height);
            layout.list.list.row_height = 30;
            layout.list.list.row_gap = 0;
            let mut resources = resources(80);
            resources.rows.context = variant as *mut c_void;
            let mut state = State::new(FONTS, 0);
            state.phase = font::FAMILIES;
            let mut commands = [Command::default(); 128];
            let buffer = Buffer {
                commands: commands.as_mut_ptr(),
                hits: ptr::null_mut(),
                command_capacity: 128,
                hit_capacity: 0,
            };
            let mut output = scene();
            let mut feedback = Feedback::default();
            assert_eq!(
                unsafe {
                    ffi::build(
                        &state,
                        &layout,
                        &resources,
                        &buffer,
                        &mut feedback,
                        &mut output,
                    )
                },
                1
            );
            maximum = maximum.max(output.command_count);
        }
    }
    std::println!("dense catalog maximum commands: {maximum}");
    assert!(maximum <= 72);
}
