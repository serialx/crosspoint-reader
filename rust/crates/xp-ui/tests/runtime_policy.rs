use xp_ui::{
    model::{Input, BACK, CONFIRM, TOUCH_HELD, TOUCH_RELEASED},
    runtime::{next_capacity, Publication, SceneGate},
    runtime_ffi::{
        xp_ui_command_next_capacity, xp_ui_scene_gate_input, xp_ui_scene_gate_publish,
        xp_ui_scene_gate_reject,
    },
};

#[test]
fn recovery_before_input_preserves_held_controls_through_release() {
    let mut gate = SceneGate::default();
    gate.reject();
    assert_eq!(gate.publish(true), Publication::Current);
    for input in [
        Input {
            pressed: CONFIRM,
            held: CONFIRM,
            touch_flags: TOUCH_HELD,
            ..Input::default()
        },
        Input {
            held: CONFIRM,
            touch_flags: TOUCH_HELD,
            ..Input::default()
        },
        Input {
            released: CONFIRM,
            touch_flags: TOUCH_RELEASED,
            ..Input::default()
        },
    ] {
        let admission = gate.input(&input);
        assert!(admission.consume);
        assert!(!admission.retry);
    }
    assert!(
        !gate
            .input(&Input {
                released: CONFIRM,
                touch_flags: TOUCH_RELEASED,
                ..Input::default()
            })
            .consume
    );
}

#[test]
fn stale_paint_preserves_failure_and_failed_back_requests_retry() {
    let mut gate = SceneGate::default();
    assert_eq!(gate.publish(false), Publication::Stale);
    assert!(!gate.input(&Input::default()).consume);
    gate.reject();
    assert_eq!(gate.publish(false), Publication::Stale);
    let admission = gate.input(&Input {
        released: BACK,
        ..Input::default()
    });
    assert!(admission.consume && admission.retry);
    assert!(gate.failed());
    assert_eq!(gate.publish(true), Publication::Current);
    assert!(!gate.failed());
}

#[test]
fn rejected_contact_consumes_swipe_and_does_not_block_independent_buttons() {
    let mut gate = SceneGate::default();
    gate.reject();
    gate.input(&Input {
        touch_flags: TOUCH_HELD,
        ..Input::default()
    });
    gate.publish(true);
    assert!(
        !gate
            .input(&Input {
                pressed: BACK,
                ..Input::default()
            })
            .consume
    );
    gate.reject();
    gate.publish(true);
    assert!(
        gate.input(&Input {
            swipe: 1,
            ..Input::default()
        })
        .consume
    );
    assert!(
        !gate
            .input(&Input {
                swipe: 1,
                ..Input::default()
            })
            .consume
    );
}

#[test]
fn capacity_sequences_keep_screen_steps_and_final_cap() {
    for (start, step, last) in [
        (32, 4, 124),
        (48, 16, 112),
        (32, 16, 112),
        (40, 16, 120),
        (8, 16, 120),
        (24, 16, 120),
    ] {
        let mut current = start;
        let mut penultimate = current;
        while let Some(next) = next_capacity(current, current + 1, step, 128) {
            penultimate = current;
            assert!(next > current && next <= 128);
            current = next;
        }
        assert_eq!(current, 128);
        assert_eq!(penultimate, last);
    }
    assert_eq!(next_capacity(32, 64, 4, 128), Some(64));
    for args in [
        (128, 129, 16, 128),
        (32, 129, 4, 128),
        (32, 33, 0, 128),
        (129, 130, 16, 128),
        (0, 1, 16, 0),
        (32, 33, 16, 256),
    ] {
        assert_eq!(next_capacity(args.0, args.1, args.2, args.3), None);
    }
}

#[test]
fn wire_gate_matches_typed_policy_and_nulls_fail_closed() {
    assert_eq!(core::mem::size_of::<SceneGate>(), 4);
    assert_eq!(core::mem::align_of::<SceneGate>(), 2);
    let mut direct = SceneGate::default();
    let mut wire = SceneGate::default();
    direct.reject();
    unsafe { xp_ui_scene_gate_reject(&mut wire) };
    assert_eq!(unsafe { xp_ui_scene_gate_publish(&mut wire, 1) }, 1);
    direct.publish(true);
    for input in [
        Input {
            held: CONFIRM,
            ..Input::default()
        },
        Input {
            released: CONFIRM,
            ..Input::default()
        },
        Input::default(),
    ] {
        let result = direct.input(&input);
        assert_eq!(
            unsafe { xp_ui_scene_gate_input(&mut wire, &input) },
            u8::from(result.consume) | (u8::from(result.retry) << 1)
        );
    }
    unsafe {
        xp_ui_scene_gate_reject(core::ptr::null_mut());
        assert_eq!(xp_ui_scene_gate_publish(core::ptr::null_mut(), 1), 0);
        assert_eq!(
            xp_ui_scene_gate_input(core::ptr::null_mut(), &Input::default()),
            3
        );
        assert_eq!(xp_ui_scene_gate_input(&mut wire, core::ptr::null()), 3);
        xp_ui_scene_gate_reject(&mut wire);
        assert_eq!(xp_ui_scene_gate_publish(&mut wire, 2), 0);
        assert!(wire.failed());
    }
    assert_eq!(xp_ui_command_next_capacity(124, 125, 4, 128), 128);
    assert_eq!(xp_ui_command_next_capacity(128, 129, 4, 128), 0);
}

use core::ffi::c_void;
use xp_ui::{
    ffi::{Buffer, SceneView},
    reader_chrome::{
        hits::{Hits, Layout},
        list::{self, Resources, Row, Screen, Theme},
        runtime::{ListRequest, RowSlot, ThemeFacts},
        ListModel,
    },
    reader_chrome_ffi::{xp_ui_reader_list_view, Rows},
    scene::{Command, Error, Rect},
};

#[derive(Default)]
struct Data {
    windows: Vec<(u16, u16)>,
    tall: bool,
    fail: bool,
}
impl Resources for Data {
    fn window(&mut self, first: u16, count: u16) -> Result<(), Error> {
        self.windows.push((first, count));
        if self.fail {
            Err(Error::Invalid)
        } else {
            Ok(())
        }
    }
    fn row(&mut self, index: u16) -> Result<Row, Error> {
        Ok(Row {
            label: u32::from(index) * 4 + 1,
            ..Row::default()
        })
    }
    fn width(&mut self, _: u32, _: i32, _: u8) -> Result<i32, Error> {
        Ok(40)
    }
    fn height(&mut self, _: u32, _: i32, _: u8, _: u8, _: i16) -> Result<i32, Error> {
        Ok(if self.tall { 1000 } else { 24 })
    }
    fn line(&mut self, _: i32) -> Result<i32, Error> {
        Ok(if self.tall {
            24 + self.windows.len() as i32 * 4
        } else {
            24
        })
    }
}
unsafe extern "C" fn window(p: *mut c_void, first: u16, count: u16) -> u8 {
    u8::from(
        unsafe { &mut *p.cast::<Data>() }
            .window(first, count)
            .is_ok(),
    )
}
unsafe extern "C" fn row(p: *mut c_void, index: u16, out: *mut Row) -> u8 {
    unsafe { out.write((&mut *p.cast::<Data>()).row(index).unwrap()) };
    1
}
unsafe extern "C" fn width(_: *mut c_void, _: u32, _: i32, _: u8) -> i32 {
    40
}
unsafe extern "C" fn height(p: *mut c_void, _: u32, _: i32, _: u8, _: u8, _: i16) -> i32 {
    if unsafe { (*p.cast::<Data>()).tall } {
        1000
    } else {
        24
    }
}
unsafe extern "C" fn line(p: *mut c_void, font: i32) -> i32 {
    unsafe { &mut *p.cast::<Data>() }.line(font).unwrap()
}
fn theme() -> Theme {
    Theme {
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
            h: 730,
        },
        row_padding: 8,
        row_min: 32,
        scroll_width: 3,
        header_height: 40,
        spacing: 8,
        space_sm: 4,
        space_md: 8,
        space_lg: 16,
        ..Theme::default()
    }
}

#[test]
fn typed_list_preparation_matches_unchanged_checked_ffi_for_every_screen() {
    for kind in [
        Screen::Menu,
        Screen::Chapters,
        Screen::Bookmarks,
        Screen::Clippings,
        Screen::End,
        Screen::Panel,
    ] {
        for count in [0, 1, 20, 200] {
            for tall in [false, true] {
                let snapshot = ListModel::new(i32::from(count) - 1);
                let mut direct = Data {
                    tall,
                    ..Data::default()
                };
                let mut wire = Data {
                    tall,
                    ..Data::default()
                };
                let rows = Rows {
                    context: (&mut wire as *mut Data).cast(),
                    window: Some(window),
                    row: Some(row),
                    text_width: Some(width),
                    text_height: Some(height),
                    line_height: Some(line),
                    count,
                    window_capacity: 24,
                    generation: 7,
                };
                let mut a = [Command::default(); 128];
                let mut b = a;
                let mut next_a = snapshot;
                let mut next_b = snapshot;
                let mut hits_a = Layout::default();
                let mut hits_b = hits_a;
                let mut output = SceneView {
                    commands: core::ptr::null(),
                    text: core::ptr::null(),
                    interactions: core::ptr::null(),
                    generation: 99,
                    command_count: 0,
                    text_len: 0,
                    interaction_count: 0,
                    refresh: 0,
                    reserved: 0,
                };
                let theme = theme();
                let request = ListRequest {
                    snapshot: &snapshot,
                    kind,
                    theme: &theme,
                    count,
                    window_capacity: 24,
                    generation: 7,
                };
                let actual = request
                    .prepare(&mut a, &mut direct, &mut next_a, &mut hits_a)
                    .unwrap();
                let buffer = Buffer {
                    commands: b.as_mut_ptr(),
                    hits: core::ptr::null_mut(),
                    command_capacity: 128,
                    hit_capacity: 0,
                };
                assert_eq!(
                    unsafe {
                        xp_ui_reader_list_view(
                            &snapshot,
                            kind as u8,
                            &theme,
                            &rows,
                            &buffer,
                            &mut next_b,
                            &mut output,
                            &mut hits_b,
                        )
                    },
                    1
                );
                assert_eq!(actual, output.command_count);
                assert_eq!(&a[..usize::from(actual)], &b[..usize::from(actual)]);
                assert_eq!(next_a.list, next_b.list);
                assert_eq!(direct.windows, wire.windows);
                assert_eq!(hits_a.len(), hits_b.len());
                for i in 0..hits_a.len() {
                    assert_eq!(hits_a.get(i), hits_b.get(i));
                }
                assert_eq!(output.refresh, 1);
                if count == 200 && tall && matches!(kind, Screen::Chapters | Screen::Clippings) {
                    assert_eq!(direct.windows.len(), 9);
                    assert_eq!(next_a.list.follow, 0);
                }
            }
        }
    }
}

#[test]
fn capacity_retry_reuses_snapshot_and_scroll_publication_preserves_new_input() {
    let mut snapshot = ListModel::new(10);
    snapshot.list.pending_scroll = 2;
    snapshot.list.follow = 0;
    let theme = theme();
    let request = ListRequest {
        snapshot: &snapshot,
        kind: Screen::Chapters,
        theme: &theme,
        count: 100,
        window_capacity: 8,
        generation: 1,
    };
    let mut data = Data::default();
    let mut next = snapshot;
    let mut hits = Layout::default();
    assert_eq!(
        request.prepare(
            &mut [Command::default(); 1],
            &mut data,
            &mut next,
            &mut hits
        ),
        Err(Error::Capacity)
    );
    let mut commands = [Command::default(); 128];
    request
        .prepare(&mut commands, &mut data, &mut next, &mut hits)
        .unwrap();
    assert_eq!(snapshot.list.pending_scroll, 2);
    assert_eq!(next.list.top, 2);
    let mut live = snapshot;
    live.list.pending_scroll = 5;
    live.list.publish(&next.list, snapshot.list.pending_scroll);
    assert_eq!(live.list.pending_scroll, 3);
    live.list.navigation_revision += 1;
    live.list.publish(&next.list, snapshot.list.pending_scroll);
    assert_eq!(live.list.pending_scroll, 3);
    data.fail = true;
    assert_eq!(
        request.prepare(&mut commands, &mut data, &mut next, &mut hits),
        Err(Error::Invalid)
    );
}

#[test]
fn row_slots_exclude_special_resources_and_preserve_optional_slots() {
    for (slot, want) in [
        (RowSlot::Label, 29),
        (RowSlot::Subtitle, 30),
        (RowSlot::Value, 31),
        (RowSlot::Icon, 32),
    ] {
        assert_eq!(slot.encode(7), want);
        assert_eq!(RowSlot::decode(want), Some((7, slot)));
    }
    assert_eq!(RowSlot::decode(0), None);
    assert_eq!(RowSlot::decode(list::HEADER), None);
    assert_eq!(RowSlot::decode(262145), None);
    assert_eq!(RowSlot::decode(262144), Some((65535, RowSlot::Icon)));
}

#[test]
fn explicit_theme_facts_compose_bookmark_touch_and_spacing_only() {
    for touch in [0, 1] {
        for bookmarks in [false, true] {
            let sampled = Theme {
                touch,
                row_min: 48,
                body_font: 17,
                help_font: -1,
                ..theme()
            };
            let composed = ThemeFacts {
                sampled,
                body_line_height: 42,
                help_font: 9,
                bookmark_help_font: 11,
            }
            .compose(bookmarks)
            .unwrap();
            assert_eq!(composed.help_font, if bookmarks { 11 } else { 9 });
            assert_eq!(composed.row_min, if touch != 0 { 0 } else { 48 });
            assert_eq!(
                (composed.space_sm, composed.space_md, composed.space_lg),
                (7, 8, 16)
            );
            assert_eq!(composed.safe, sampled.safe);
            assert_eq!(composed.body_font, 17);
        }
    }
    assert!(ThemeFacts {
        sampled: theme(),
        body_line_height: -1,
        help_font: 9,
        bookmark_help_font: 11
    }
    .compose(false)
    .is_err());
}
