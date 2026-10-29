extern crate std;
use super::hits::{Hits, Layout};
use super::{
    list::{self, Resources, Row, Screen, Theme},
    toolbar,
};
use crate::scene::HitSink;
use crate::{
    model::{self, Input, ListState},
    scene::{Arena, Command, Error, Interaction, Kind, Rect, Refresh},
};
use core::fmt::Write;
use std::{process::Command as Process, string::String};
fn golden(name: &str) -> String {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../test/reader_chrome")
        .join(name);
    let output=Process::new("python3").args(["-c","import gzip,sys;sys.stdout.buffer.write(gzip.decompress(open(sys.argv[1],'rb').read()))"]).arg(path).output().unwrap();
    assert!(output.status.success());
    String::from_utf8(output.stdout).unwrap()
}
fn same(actual: &str, expected: &str) {
    for (i, (a, b)) in actual.lines().zip(expected.lines()).enumerate() {
        assert_eq!(a, b, "line {i}");
    }
    assert_eq!(actual.lines().count(), expected.lines().count());
}
struct Data {
    kind: u8,
    panel: bool,
    page: String,
}
impl Data {
    fn value(&self, handle: u32) -> &str {
        match handle {
            list::TITLE => "Chapter title",
            list::PROGRESS => "12/40   51%",
            list::HEADER => "Contents",
            toolbar::PAGE => &self.page,
            _ => {
                let index = (handle - 1) / 4;
                match (handle - 1) % 4 {
                    0 => {
                        if self.panel {
                            if index % 2 == 1 {
                                "Chapter name with more words to fit beside the value"
                            } else {
                                "Chapter"
                            }
                        } else if index % 3 == 1 {
                            "A long chapter title that wraps across the row beside a value"
                        } else {
                            "Chapter"
                        }
                    }
                    1 => {
                        if self.kind == 2 {
                            "28% - 5/12 - A chapter title"
                        } else {
                            "A clipping preview with enough words to wrap to a second line on a narrow display"
                        }
                    }
                    2 => {
                        if self.panel {
                            "Value"
                        } else {
                            "Portrait"
                        }
                    }
                    _ => "",
                }
            }
        }
    }
}
impl Resources for Data {
    fn window(&mut self, _: u16, _: u16) -> Result<(), Error> {
        Ok(())
    }
    fn row(&mut self, index: u16) -> Result<Row, Error> {
        let base = u32::from(index) * 4 + 1;
        Ok(Row {
            label: base,
            subtitle: if self.kind >= 2 { base + 1 } else { 0 },
            value: if self.panel || self.kind == 1 && index % 2 == 1 {
                base + 2
            } else {
                0
            },
            icon: if self.kind >= 2 { base + 3 } else { 0 },
            toggle: u8::from(self.kind == 1 && index.is_multiple_of(2)),
            checked: u8::from(index.is_multiple_of(4)),
            reserved: [0; 2],
        })
    }
    fn width(&mut self, h: u32, f: i32, _: u8) -> Result<i32, Error> {
        Ok(self.value(h).len() as i32 * if f == 0 { 8 } else { 10 })
    }
    fn height(&mut self, h: u32, f: i32, b: u8, lines: u8, width: i16) -> Result<i32, Error> {
        let n = if lines > 1 && self.width(h, f, b)? > i32::from(width) {
            2
        } else {
            1
        };
        Ok(n * self.line(f)?)
    }
    fn line(&mut self, f: i32) -> Result<i32, Error> {
        Ok(if f == 0 { 18 } else { 24 })
    }
}
fn record(out: &mut String, commands: &[Command], data: &Data) {
    for c in commands {
        let r = c.rect;
        let color = [4, 1, 2, 3][usize::from(c.paint)];
        let paint = if c.paint >= 2 { 2 } else { 1 };
        let corners = if c.corners == 0 { 15 } else { c.corners };
        match Kind::from_byte(c.kind).unwrap() {
            Kind::Fill => writeln!(
                out,
                "fill {} {} {} {} {} {} {} {}",
                r.x, r.y, r.w, r.h, paint, color, c.radius, corners
            )
            .unwrap(),
            Kind::Stroke => writeln!(
                out,
                "stroke {} {} {} {} {} {} {} {} {}",
                r.x, r.y, r.w, r.h, paint, color, c.width, c.radius, corners
            )
            .unwrap(),
            Kind::TextHandle => {
                let text = data.value(c.resource);
                writeln!(
                    out,
                    "text {} {} {} {} {} {} {} {} {} 0 {}",
                    r.x, r.y, r.w, r.h, c.font, c.align, color, c.max_lines, c.style, text
                )
                .unwrap();
                let width = text.len() as i32 * if c.font == 0 { 8 } else { 10 };
                let line = if c.font == 0 { 18 } else { 24 };
                if width <= i32::from(r.w) {
                    let x = i32::from(r.x)
                        + match c.align {
                            1 => (i32::from(r.w) - width) / 2,
                            2 => i32::from(r.w) - width,
                            _ => 0,
                        };
                    writeln!(out, "origin {} {}", x, r.y + ((r.h - line) / 2).max(0)).unwrap();
                }
            }
            Kind::Bitmap => {
                let side = if c.resource >= toolbar::CONTENTS {
                    24
                } else {
                    32
                };
                let mode = if side == 24 { 0 } else { 2 };
                writeln!(
                    out,
                    "bitmap {} {} {} {} {} {} {} {} 0",
                    r.x, r.y, r.w, r.h, side, side, mode, color
                )
                .unwrap();
            }
            Kind::Clip => write!(
                out,
                "clip {} {} {} {}",
                r.x,
                r.y,
                r.w.min(528 - r.x),
                r.h.min(792 - r.y)
            )
            .unwrap(),
            Kind::Pop => write!(out, "clip 0 0 528 792").unwrap(),
            Kind::Triangle => writeln!(
                out,
                "triangle {} {} {} {} {} {} 4",
                r.x,
                r.y,
                r.x,
                r.y + r.h,
                r.x + r.w,
                r.y + r.h / 2
            )
            .unwrap(),
            _ => panic!("unexpected command"),
        }
    }
}
#[test]
fn rich_rows_match_frozen_sdk() {
    let mut actual = String::new();
    for width in [480, 528, 792, 800] {
        for kind in 0..4 {
            for selected in [0, 3, 18] {
                for selection in 0..4 {
                    writeln!(actual, "case {width} {kind} {selected} {selection}").unwrap();
                    let mut data = Data {
                        kind,
                        panel: false,
                        page: String::new(),
                    };
                    let screen = Screen::from_byte(kind).unwrap();
                    let mut content = list::Content::new(&mut data, 19, 24, screen, 0, 0, 4);
                    let mut commands = [Command::default(); 128];
                    let mut hits = [Interaction::default(); 24];
                    let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                    let rect = Rect {
                        x: 0,
                        y: 0,
                        w: width,
                        h: 480,
                    };
                    let style = crate::list::Style {
                        area: rect,
                        screen: rect,
                        min_touch: 44,
                        font: if kind == 1 { 0 } else { 1 },
                        row_height: if kind == 1 { 26 } else { 32 },
                        row_gap: 3,
                        inset: 5,
                        padding: 8,
                        scroll_width: 3,
                        radius: 12,
                        selection,
                        ..crate::list::Style::default()
                    };
                    let next = crate::list::view_rows(
                        &mut arena,
                        ListState::new(selected),
                        style,
                        19,
                        1,
                        &mut content,
                    )
                    .unwrap();
                    let scene = arena.finish(Refresh::Fast, 1).unwrap();
                    check_compact_hits(scene.interactions, if data.panel { 6 } else { 1 });
                    record(&mut actual, scene.commands, &data);
                    writeln!(
                        actual,
                        "nav {} {} {} {}",
                        next.selected,
                        next.top,
                        next.drawn,
                        next.page_rows()
                    )
                    .unwrap();
                    for hit in scene.interactions {
                        let r = hit.rect;
                        writeln!(
                            actual,
                            "hit {} {} {} {} {} {}",
                            r.x, r.y, r.w, r.h, hit.action, hit.value
                        )
                        .unwrap();
                    }
                }
            }
        }
    }
    let expected = golden("widgets.golden.gz");
    same(&actual, expected.split("sheet ").next().unwrap());
}
#[test]
fn toolbar_matches_frozen_sdk() {
    let mut actual = String::new();
    for width in [480, 528, 792, 800] {
        for panel in [0, 1] {
            for dense in [0, 1] {
                for active in [0, 2] {
                    writeln!(actual, "toolbar {width} {panel} {dense} {active}").unwrap();
                    let mut data = Data {
                        kind: 0,
                        panel: true,
                        page: String::new(),
                    };
                    let screen = Rect {
                        x: 0,
                        y: 0,
                        w: width,
                        h: if width < 600 { 792 } else { 480 },
                    };
                    let theme = Theme {
                        screen,
                        safe: screen,
                        body_font: 1,
                        small_font: 0,
                        title_font: 2,
                        row_min: 48,
                        row_gap: 0,
                        row_padding: 8,
                        scroll_width: 3,
                        space_sm: 4,
                        space_md: 8,
                        space_lg: 16,
                        control_radius: 12,
                        sheet_radius: 12,
                        ..Theme::default()
                    };
                    let model = toolbar::Model {
                        panel,
                        dense_rows: dense,
                        active_tool: active,
                        progress_permille: 510,
                        chapter_title: list::TITLE,
                        page_info: list::PROGRESS,
                        panel_title: list::HEADER,
                        ..toolbar::Model::default()
                    };
                    let mut commands = [Command::default(); 80];
                    let mut hits = [Interaction::default(); 32];
                    let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                    let next = toolbar::view(
                        &mut arena,
                        ListState::new(2),
                        model,
                        theme,
                        &mut data,
                        30,
                        16,
                        1,
                    )
                    .unwrap();
                    data.page = std::format!(
                        "{}/{}",
                        next.top / next.page_rows() + 1,
                        (30 + next.page_rows() - 1) / next.page_rows()
                    );
                    let scene = arena.finish(Refresh::None, 1).unwrap();
                    check_compact_hits(scene.interactions, if data.panel { 6 } else { 1 });
                    record(&mut actual, scene.commands, &data);
                    for hit in scene.interactions {
                        let r = hit.rect;
                        let inputs = if hit.action == 5 { 513 } else { 1 };
                        writeln!(
                            actual,
                            "hit {} {} {} {} {} {} {}",
                            r.x, r.y, r.w, r.h, hit.action, hit.value, inputs
                        )
                        .unwrap();
                    }
                }
            }
        }
    }
    let expected = golden("toolbar.golden.gz");
    same(&actual, expected.split("route ").next().unwrap());
}
#[test]
fn toolbar_drag_release_and_suppressed_contact_match_sdk() {
    let mut actual = String::new();
    let hits = [
        Interaction {
            rect: Rect {
                x: 10,
                y: 20,
                w: 100,
                h: 40,
            },
            action: 5,
            inputs: 1,
            ..Interaction::default()
        },
        Interaction {
            rect: Rect {
                x: 120,
                y: 20,
                w: 40,
                h: 40,
            },
            action: 2,
            value: 1,
            inputs: 1,
            ..Interaction::default()
        },
    ];
    for scenario in 0..6 {
        writeln!(actual, "route {scenario}").unwrap();
        let mut contact = toolbar::Contact::default();
        let mut compact_contact = toolbar::Contact::default();
        let mut layout = Layout::new(6, 0);
        for hit in hits {
            layout.push(hit).unwrap();
        }
        for step in 0..4 {
            let mut input = Input::default();
            if step == 0 {
                input.touch_flags = model::TOUCH_HELD;
                input.touch_x = if scenario == 2 { 0 } else { 30 };
                input.touch_y = 30;
            }
            if step == 1 {
                input.touch_flags = model::TOUCH_HELD;
                input.touch_x = if scenario == 3 { 200 } else { 60 };
                input.touch_y = 30;
            }
            if step == 3 {
                input.touch_flags = model::TOUCH_RELEASED;
                input.touch_x = if scenario == 1 { -1 } else { 30 };
                input.touch_y = if scenario == 1 { -1 } else { 30 };
            }
            if scenario == 4 {
                input = Input::default();
                if step == 3 {
                    input.touch_flags = model::TOUCH_RELEASED;
                    input.touch_x = 40;
                    input.touch_y = 30;
                }
            }
            if scenario == 5 {
                input = Input::default();
            }
            let mut permille = -1;
            let effect = contact.route(&hits[..], input, &mut permille);
            let mut compact_permille = -1;
            let compact_effect = compact_contact.route(&layout, input, &mut compact_permille);
            assert_eq!(
                (effect.kind, effect.value, effect.invalidate, permille),
                (
                    compact_effect.kind,
                    compact_effect.value,
                    compact_effect.invalidate,
                    compact_permille
                )
            );
            writeln!(
                actual,
                "event {step} {} {} {permille}",
                effect.kind, effect.value
            )
            .unwrap();
        }
    }
    let expected = golden("toolbar.golden.gz");
    let offset = expected.find("route ").unwrap();
    same(&actual, &expected[offset..]);
}
#[test]
fn footnote_keys_and_highlight_match_frozen_cpp() {
    let mut actual = String::new();
    let keys = [
        model::BACK,
        model::CONFIRM,
        model::POWER,
        model::SCREEN_LEFT,
        model::SCREEN_RIGHT,
        model::SCREEN_UP,
        model::SCREEN_DOWN,
    ];
    for count in [0, 1, 3, 16] {
        for selected in [0, 1, 2, 15] {
            if selected >= count && count != 0 {
                continue;
            }
            for (key, bit) in keys.into_iter().enumerate() {
                for edge in 0..3 {
                    for held in [0, 499, 500, 501, 1000] {
                        let mut state = super::FootnoteState {
                            selected,
                            last_move_ms: 500,
                        };
                        let mut input = Input {
                            held_ms: held,
                            now_ms: 500 + held,
                            ..Input::default()
                        };
                        match edge {
                            0 => input.pressed = bit,
                            1 => input.released = bit,
                            _ => input.held = bit,
                        };
                        let effect = state.update(input, count as u16);
                        writeln!(
                            actual,
                            "{count} {selected} {key} {edge} {held}: {} {} {} {}",
                            state.selected, effect.kind, effect.invalidate, state.last_move_ms
                        )
                        .unwrap();
                    }
                }
            }
        }
    }
    for x in [-10, 0, 1, 12] {
        for y in [-10, 0, 1, 12] {
            for size in [0, 1, 12] {
                let mut commands = [Command::default(); 2];
                let mut arena = Arena::new(&mut commands, &mut [], &mut []).unwrap();
                let r = super::footnote_view(
                    &mut arena,
                    Rect {
                        x: 0,
                        y: 0,
                        w: 480,
                        h: 800,
                    },
                    Rect {
                        x,
                        y,
                        w: size,
                        h: size,
                    },
                    0,
                    0,
                    1,
                    1,
                )
                .unwrap();
                writeln!(
                    actual,
                    "highlight {x} {y} {size}: {} {} {} {} {} {}",
                    r.x,
                    r.y,
                    r.w,
                    r.h,
                    x,
                    y - 1
                )
                .unwrap();
            }
        }
    }
    same(&actual, &golden("footnote-input.golden.gz"));
}
#[test]
fn end_keys_match_frozen_cpp() {
    let mut actual = String::new();
    let bits = [
        model::BACK,
        model::CONFIRM,
        model::NAV_PREVIOUS,
        model::NAV_NEXT,
    ];
    for count in [2, 4] {
        for selected in 0..count {
            for press in [0, 1] {
                for (button, bit) in bits.into_iter().enumerate() {
                    for edge in [0, 1] {
                        for held in [0, 999, 1000, 1001] {
                            let mut state = ListState::new(selected);
                            let mut input = Input {
                                held_ms: held,
                                ..Input::default()
                            };
                            if edge == 0 {
                                input.pressed = bit;
                            } else {
                                input.released = bit;
                            }
                            let effect =
                                super::end_update(&mut state, input, count as u16, press != 0);
                            let action = match effect.kind {
                                1 => 3,
                                2 | 3 => 2,
                                4 => 1,
                                _ => 0,
                            };
                            writeln!(
                                actual,
                                "{count} {selected} {press} {button} {edge} {held}: {action} {}",
                                state.selected
                            )
                            .unwrap();
                        }
                    }
                }
            }
        }
    }
    same(&actual, &golden("end-input.golden.gz"));
}

#[test]
fn checked_view_rejects_incomplete_or_changed_resources_without_publication() {
    use crate::{
        ffi::{Buffer, SceneView},
        reader_chrome_ffi::{xp_ui_reader_list_select, xp_ui_reader_list_view, Rows},
    };
    struct Context {
        descriptor: *mut Rows,
        fail_row: bool,
        change_generation: bool,
    }
    unsafe extern "C" fn window(p: *mut core::ffi::c_void, _: u16, _: u16) -> u8 {
        let context = unsafe { &mut *p.cast::<Context>() };
        if context.change_generation {
            unsafe {
                (*context.descriptor).generation += 1;
            }
        }
        1
    }
    unsafe extern "C" fn row(p: *mut core::ffi::c_void, index: u16, out: *mut Row) -> u8 {
        if unsafe { (*p.cast::<Context>()).fail_row } {
            return 0;
        }
        unsafe {
            out.write(Row {
                label: u32::from(index) + 1,
                ..Row::default()
            });
        }
        1
    }
    unsafe extern "C" fn width(_: *mut core::ffi::c_void, _: u32, _: i32, _: u8) -> i32 {
        70
    }
    unsafe extern "C" fn height(
        _: *mut core::ffi::c_void,
        _: u32,
        _: i32,
        _: u8,
        _: u8,
        _: i16,
    ) -> i32 {
        24
    }
    unsafe extern "C" fn line(_: *mut core::ffi::c_void, _: i32) -> i32 {
        24
    }
    let mut context = Context {
        descriptor: core::ptr::null_mut(),
        fail_row: false,
        change_generation: false,
    };
    let mut rows = Rows {
        context: (&mut context as *mut Context).cast(),
        window: Some(window),
        row: Some(row),
        text_width: Some(width),
        text_height: Some(height),
        line_height: Some(line),
        count: 20,
        window_capacity: 8,
        generation: 1,
    };
    context.descriptor = &mut rows;
    core::hint::black_box(&context);
    let mut commands = [Command::default(); 128];
    let mut hits = [Interaction::default(); 24];
    let mut buffer = Buffer {
        commands: commands.as_mut_ptr(),
        hits: hits.as_mut_ptr(),
        command_capacity: 128,
        hit_capacity: 0,
    };
    let screen = Rect {
        x: 0,
        y: 0,
        w: 480,
        h: 1200,
    };
    let theme = Theme {
        screen,
        safe: screen,
        row_padding: 8,
        scroll_width: 3,
        ..Theme::default()
    };
    let state = super::ListModel::new(0);
    let mut next = super::ListModel::new(99);
    let mut output = SceneView {
        commands: core::ptr::null(),
        text: core::ptr::null(),
        interactions: core::ptr::null(),
        generation: 99,
        command_count: 42,
        text_len: 0,
        interaction_count: 0,
        refresh: 0,
        reserved: 0,
    };
    let mut layout = Layout::new(1, 99);
    for failure in 0..6 {
        rows.row = if failure == 0 { None } else { Some(row) };
        context.fail_row = failure == 1;
        context.change_generation = failure == 3;
        core::hint::black_box(&context);
        rows.window_capacity = if failure == 2 {
            65
        } else if failure == 5 {
            25
        } else {
            8
        };
        rows.count = if failure == 5 { 25 } else { 20 };
        buffer.command_capacity = if failure == 4 { 1 } else { 128 };
        assert_eq!(
            unsafe {
                xp_ui_reader_list_view(
                    &state,
                    Screen::Chapters as u8,
                    &theme,
                    core::ptr::addr_of!(rows),
                    &buffer,
                    &mut next,
                    &mut output,
                    &mut layout,
                )
            },
            if failure >= 4 { 2 } else { 0 }
        );
        assert_eq!(next.list.selected, 99);
        assert_eq!(output.generation, 99);
        assert_eq!(output.command_count, 42);
        assert_eq!(layout.generation, 99);
    }
    context.fail_row = false;
    context.change_generation = false;
    core::hint::black_box(&context);
    rows.window_capacity = 8;
    buffer.command_capacity = 128;
    assert_eq!(
        unsafe {
            xp_ui_reader_list_view(
                &state,
                Screen::Chapters as u8,
                &theme,
                core::ptr::addr_of!(rows),
                &buffer,
                &mut next,
                &mut output,
                &mut layout,
            )
        },
        1
    );
    assert_eq!(output.generation, rows.generation);
    assert!(output.command_count > 0);
    // The existing popup/delete callbacks may restore a selection after shrinking the source.
    unsafe { xp_ui_reader_list_select(&mut next, 19, 3) };
    assert_eq!(next.list.selected, 2);
    assert_eq!(next.list.follow, 1);
    unsafe { xp_ui_reader_list_select(&mut next, -7, 3) };
    assert_eq!(next.list.selected, 0);
    unsafe { xp_ui_reader_list_select(&mut next, 2, 0) };
    assert_eq!(next.list.selected, 0);
}

#[test]
fn long_press_popup_release_and_next_remote_click_keep_their_owners() {
    let mut state = super::ListModel::new(1);
    let effect = state.update(Input::default(), 3, true, Screen::Bookmarks, true, -1);
    assert_eq!(effect.kind, super::ACTIONS);
    assert_eq!(effect.value, 1);
    let release = Input {
        released: model::CONFIRM,
        ..Input::default()
    };
    assert_eq!(
        state
            .update(release, 3, true, Screen::Bookmarks, false, -1)
            .kind,
        0
    );
    state.update(Input::default(), 3, true, Screen::Bookmarks, true, -1);
    // The native adapter can consume the physical release while a popup owns input.
    // A remote click supplies both edges and must still activate the next row.
    let click = Input {
        pressed: model::CONFIRM,
        released: model::CONFIRM,
        ..Input::default()
    };
    assert_eq!(
        state
            .update(click, 3, true, Screen::Bookmarks, false, -1)
            .kind,
        model::EffectKind::Activate as u8
    );
    state.revealed = 1;
    let touch = Input {
        action: 1,
        value: 1,
        ..Input::default()
    };
    let effect = state.update(touch, 3, true, Screen::Bookmarks, true, -1);
    assert_eq!(effect.kind, 0);
    assert_eq!(effect.invalidate, 1);
    assert_eq!(state.revealed, -1);
}

#[test]
fn menu_options_effects_cancellation_and_capabilities_match_cpp() {
    use super::menu;
    let mut actual = String::new();
    for orientation in 0..4 {
        for page_turn in 0..5 {
            let state = menu::State {
                orientation,
                page_turn,
                ..menu::State::default()
            };
            for action in 0..19 {
                let r = state.activate(action);
                writeln!(
                    actual,
                    "activate {orientation} {page_turn} {action}: {} {} {} {} {}",
                    r.effect, r.action, r.orientation, r.page_turn, r.cancelled
                )
                .unwrap();
            }
            let r = state.cancel();
            writeln!(
                actual,
                "cancel {orientation} {page_turn}: {} {} {} {} {}",
                r.effect, r.action, r.orientation, r.page_turn, r.cancelled
            )
            .unwrap();
            for selection in 0..4 {
                let mut next = state;
                let r = next.choose(0, selection);
                writeln!(
                    actual,
                    "orientation {orientation} {page_turn} {selection}: {} {} {}",
                    r.effect, r.orientation, r.page_turn
                )
                .unwrap();
            }
            for selection in 0..5 {
                let mut next = state;
                let r = next.choose(1, selection);
                writeln!(
                    actual,
                    "page-turn {orientation} {page_turn} {selection}: {} {} {}",
                    r.effect, r.orientation, r.page_turn
                )
                .unwrap();
            }
        }
    }
    for option in 0..2 {
        let mut state = menu::State {
            orientation: 3,
            page_turn: 4,
            reserved: [0; 2],
        };
        let before = state;
        let r = state.choose(option, -1);
        assert_eq!(state, before);
        writeln!(
            actual,
            "popup-cancel {option}: {} {} {} {} {}",
            r.effect, r.action, r.orientation, r.page_turn, r.cancelled
        )
        .unwrap();
    }
    let actions = [
        0, 1, 9, 12, 10, 11, 3, 4, 5, 18, 8, 7, 6, 13, 14, 15, 16, 17, 2,
    ];
    for caps in 0..8 {
        write!(actual, "capabilities {caps}:").unwrap();
        for action in actions {
            if action == menu::FRONTLIGHT && (caps & 1 == 0 || caps & 2 != 0)
                || action == menu::BLUETOOTH && caps & 4 == 0
            {
                continue;
            }
            write!(
                actual,
                " {action}={}",
                menu::State::default().activate(action).effect
            )
            .unwrap();
        }
        writeln!(actual).unwrap();
    }
    same(&actual, &golden("menu-input.golden.gz"));
}
#[test]
fn menu_invalid_requests_are_cancelled_noops() {
    use super::menu;
    use crate::reader_chrome_ffi::{xp_ui_reader_menu_activate, xp_ui_reader_menu_choose};
    let mut state = menu::State {
        orientation: 3,
        page_turn: 4,
        reserved: [0; 2],
    };
    let before = state;
    for action in [-1, 19, i32::MIN, i32::MAX] {
        let r = unsafe { xp_ui_reader_menu_activate(&state, action) };
        assert_eq!(r.effect, 0);
        assert_eq!(r.cancelled, 1);
        assert_eq!(state, before);
    }
    for (option, selection) in [
        (0, -1),
        (0, 4),
        (1, -1),
        (1, 5),
        (2, 0),
        (255, 1),
        (1, i32::MAX),
    ] {
        let r = unsafe { xp_ui_reader_menu_choose(&mut state, option, selection) };
        assert_eq!(r.effect, 0);
        assert_eq!(r.cancelled, 1);
        assert_eq!(state, before);
    }
}

#[test]
fn end_scene_fits_its_small_arena() {
    for height in [160, 480, 800, 960] {
        for selection in 0..4 {
            for count in 0..=4 {
                let screen = Rect {
                    x: 0,
                    y: 0,
                    w: 480,
                    h: height,
                };
                let theme = Theme {
                    screen,
                    safe: screen,
                    row_min: 48,
                    scroll_width: 3,
                    spacing: 8,
                    selection,
                    ..Theme::default()
                };
                let mut resources = Data {
                    kind: 0,
                    panel: false,
                    page: String::new(),
                };
                let mut commands = [Command::default(); 16];
                let mut hits = [Interaction::default(); 4];
                let mut arena = Arena::new(&mut commands, &mut [], &mut hits).unwrap();
                list::view(
                    &mut arena,
                    ListState::new(0),
                    Screen::End,
                    theme,
                    &mut resources,
                    count,
                    4,
                    1,
                    -1,
                )
                .unwrap();
                arena.finish(Refresh::None, 1).unwrap();
            }
        }
    }
}

fn check_compact_hits(hits: &[Interaction], row_action: u8) {
    let mut layout = Layout::new(row_action, 1);
    for h in hits {
        layout.push(*h).unwrap();
    }
    assert!(layout.valid());
    assert_eq!(layout.len(), hits.len());
    for h in hits {
        assert!((0..layout.len()).any(|i| layout.get(i) == Some(*h)));
        for x in [
            h.rect.x - 1,
            h.rect.x,
            h.rect.x + h.rect.w / 2,
            h.rect.x + h.rect.w,
        ] {
            for y in [
                h.rect.y - 1,
                h.rect.y,
                h.rect.y + h.rect.h / 2,
                h.rect.y + h.rect.h,
            ] {
                assert_eq!(
                    layout.hit(x, y),
                    hits.iter().rev().find(|h| h.rect.contains(x, y)).copied()
                );
            }
        }
    }
}
#[test]
fn compact_sink_limits_and_publication_keep_old_input_geometry() {
    let mut old = Layout::new(1, 1);
    let row = Interaction {
        rect: Rect {
            x: 4,
            y: 80,
            w: 400,
            h: 40,
        },
        action: 1,
        value: 0,
        generation: 1,
        inputs: 1,
        flags: 0,
    };
    old.push(row).unwrap();
    let mut commands = [Command::default(); 1];
    let mut pending = Layout::new(1, 2);
    let mut arena = Arena::with_hit_sink(&mut commands, &mut pending).unwrap();
    arena
        .hit(Interaction {
            value: 12,
            generation: 2,
            ..row
        })
        .unwrap();
    let scene = arena.finish(Refresh::Fast, 2).unwrap();
    assert!(scene.interactions.is_empty());
    // Input arriving after layout but before paint/publication still names row zero.
    let mut live = super::ListModel::new(12);
    let mut built = live;
    built.list.top = 12;
    let hit = old.hit(20, 90).unwrap();
    let effect = live.update(
        Input {
            action: hit.action,
            value: hit.value,
            touch_flags: model::TOUCH_RELEASED,
            ..Input::default()
        },
        30,
        true,
        Screen::Chapters,
        false,
        -1,
    );
    assert_eq!(effect.value, 0);
    live.list.publish(&built.list, 0);
    assert_eq!(live.list.selected, 0);
    old = pending;
    assert_eq!(old.hit(20, 90).unwrap().value, 12);
    let mut layout = Layout::new(1, 1);
    for i in 0..24 {
        layout
            .push(Interaction {
                value: i,
                rect: Rect {
                    y: i as i16 * 40,
                    ..row.rect
                },
                ..row
            })
            .unwrap();
    }
    assert_eq!(
        layout.push(Interaction {
            value: 24,
            rect: Rect { y: 960, ..row.rect },
            ..row
        }),
        Err(Error::Capacity)
    );
    for _ in 0..7 {
        layout.push(Interaction { action: 2, ..row }).unwrap();
    }
    assert_eq!(
        layout.push(Interaction { action: 2, ..row }),
        Err(Error::Capacity)
    );
    layout.row_count = 25;
    assert!(layout.hit(20, 90).is_none());
    layout.row_count = 24;
    layout.control_count = 8;
    assert!(layout.hit(20, 90).is_none());
}

#[test]
fn real_row_shapes_have_bounded_command_growth() {
    for height in [480, 800, 960, 8192] {
        for (kind, count, window, bound) in [
            (0, 100, 24, 56),
            (1, 19, 19, 88),
            (2, 100, 24, 112),
            (3, 100, 8, 48),
        ] {
            for selection in 0..4 {
                let screen = Rect {
                    x: 0,
                    y: 0,
                    w: 480,
                    h: height,
                };
                let theme = Theme {
                    screen,
                    safe: screen,
                    body_font: 1,
                    small_font: 0,
                    title_font: 1,
                    fixed_title_font: 1,
                    help_font: 0,
                    row_min: 32,
                    row_gap: 3,
                    row_padding: 8,
                    scroll_width: 3,
                    spacing: 8,
                    header_height: 60,
                    tab_height: 24,
                    selection,
                    ..Theme::default()
                };
                let mut resources = Data {
                    kind,
                    panel: false,
                    page: String::new(),
                };
                let mut commands = [Command::default(); 128];
                let mut layout = Layout::new(1, 1);
                let mut arena = Arena::with_hit_sink(&mut commands, &mut layout).unwrap();
                list::view(
                    &mut arena,
                    ListState::new(0),
                    Screen::from_byte(kind).unwrap(),
                    theme,
                    &mut resources,
                    count,
                    window,
                    1,
                    if kind == 2 { 0 } else { -1 },
                )
                .unwrap();
                let scene = arena.finish(Refresh::Fast, 1).unwrap();
                assert!(
                    scene.commands.len() <= bound,
                    "kind {kind} height {height}: {} > {bound}",
                    scene.commands.len()
                );
                assert!(layout.valid());
            }
        }
    }
}
