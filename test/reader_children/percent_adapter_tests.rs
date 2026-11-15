use crate::{abi, owned::Percent, owner_tests, test_resources};
use std::{cell::RefCell, sync::OnceLock};
use xp_core::i18n::ids::*;
use xp_ui::{
    ffi::{Paint, SceneView},
    model,
    reader::percent::Outcome,
    scene::{Command, Kind, Rect},
    utility,
};

#[derive(Default)]
struct Record {
    owner: Option<*const Percent>,
    fail: bool,
    labels: Vec<Vec<u8>>,
    text_paints: Vec<(Vec<u8>, u8)>,
    footer: Vec<Vec<u8>>,
    refreshes: usize,
    commands: usize,
    short_hints: bool,
}
thread_local! { static RECORD: RefCell<Record> = RefCell::new(Record::default()); }
unsafe extern "C" fn translate(key: u16) -> abi::Text {
    abi::Text::from_terminated(match key {
        STR_GO_TO_PERCENT => b"Go to percent\0",
        STR_CONFIRM => b"Confirm\0",
        STR_BACK => b"Back\0",
        STR_SELECT => b"Select\0",
        STR_STEP_HINT_FRONT => {
            if RECORD.with(|r| r.borrow().short_hints) {
                b"F\0"
            } else {
                b"Front buttons:\0"
            }
        }
        STR_STEP_HINT_SIDE => {
            if RECORD.with(|r| r.borrow().short_hints) {
                b"S\0"
            } else {
                b"Side buttons:\0"
            }
        }
        _ => panic!("unexpected translation {key}"),
    })
}
unsafe extern "C" fn width(text: abi::Text, _: i32, _: u8) -> i32 {
    text.bytes().unwrap().len() as i32 * 10
}
unsafe extern "C" fn line(_: i32) -> i32 {
    24
}
unsafe extern "C" fn height(text: abi::Text, _: abi::TextLayout) -> i32 {
    assert_eq!(*text.data.add(text.len), 0);
    24
}
unsafe extern "C" fn text(command: *const Command, _: Rect, text: abi::Text) {
    assert_eq!(*text.data.add(text.len), 0);
    RECORD.with(|r| r.borrow_mut().labels.push(text.bytes().unwrap().to_vec()));
    RECORD.with(|r| {
        r.borrow_mut()
            .text_paints
            .push((text.bytes().unwrap().to_vec(), (*command).paint))
    });
}
unsafe extern "C" fn footer(labels: *const abi::Text) {
    RECORD.with(|r| {
        r.borrow_mut().footer = core::slice::from_raw_parts(labels, 4)
            .iter()
            .map(|s| s.bytes().unwrap().to_vec())
            .collect()
    });
}
unsafe extern "C" fn refresh(mode: u8) {
    assert_eq!(mode, xp_ui::scene::Refresh::Fast as u8);
    RECORD.with(|r| r.borrow_mut().refreshes += 1);
}
unsafe extern "C" fn log(_: u8, _: abi::Text, _: abi::Text) {
    panic!("unexpected diagnostic");
}
unsafe extern "C" fn paint(scene: *const SceneView, paint: *const Paint) -> u8 {
    let scene = &*scene;
    let paint = &*paint;
    RECORD.with(|r| r.borrow_mut().commands = usize::from(scene.command_count));
    let owner = RECORD.with(|r| r.borrow_mut().owner.take());
    if let Some(owner) = owner {
        (*owner).input(
            model::Input {
                pressed: model::RIGHT,
                ..Default::default()
            },
            true,
        );
    }
    if RECORD.with(|r| r.borrow().fail) {
        return 0;
    }
    for command in core::slice::from_raw_parts(scene.commands, usize::from(scene.command_count)) {
        if matches!(command.kind, k if k == Kind::TextHandle as u8 || k == Kind::Bitmap as u8) {
            assert_eq!(
                paint.resource_valid.unwrap()(paint.context, command.resource, command.generation),
                1
            );
            paint.resource.unwrap()(
                paint.context,
                core::ptr::null(),
                command,
                Rect {
                    x: 0,
                    y: 0,
                    w: 480,
                    h: 800,
                },
            );
        }
    }
    1
}
fn services() -> &'static abi::Resources {
    static SERVICES: OnceLock<abi::Resources> = OnceLock::new();
    SERVICES.get_or_init(|| abi::Resources {
        translate,
        text_width: width,
        line_height: line,
        text_box_height: height,
        text_draw: text,
        footer_draw: footer,
        refresh,
        paint_scene: paint,
        ..test_resources::resources()
    })
}
fn layout() -> utility::Layout {
    utility::Layout {
        screen: Rect {
            x: 0,
            y: 0,
            w: 480,
            h: 800,
        },
        touch: 1,
        popup_padding: 16,
        popup_gap: 8,
        popup_side_margin: 16,
        selection_padding: 4,
        space_sm: 4,
        space_md: 8,
        popup_border: 2,
        ..Default::default()
    }
}
#[test]
fn percent_adapter_paints_terminated_labels_and_preserves_input_during_paint() {
    let owner = Percent::try_new(owner_tests::runtime(), 50).unwrap();
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            owner: Some(&*owner),
            ..Default::default()
        }
    });
    unsafe {
        owner
            .render(services(), log, |out| *out = layout())
            .unwrap();
    }
    RECORD.with(|r| {
        let r = r.borrow();
        assert_eq!(r.commands, 18);
        assert!(r.labels.iter().any(|s| s == b"50%"));
        assert_eq!(
            r.footer,
            [
                b"Back".to_vec(),
                b"Select".to_vec(),
                b"-".to_vec(),
                b"+".to_vec()
            ]
        );
        assert_eq!(r.refreshes, 1);
    });
    assert_eq!(owner.model.with(|m| m.state.value()), 51);
    let ok = owner.model.with(|m| {
        *m.controls
            .unwrap()
            .as_slice()
            .iter()
            .find(|h| h.action == utility::OK)
            .unwrap()
    });
    assert_eq!(
        owner
            .input(
                model::Input {
                    touch_flags: model::TOUCH_RELEASED,
                    touch_x: ok.rect.x + 1,
                    touch_y: ok.rect.y + 1,
                    ..Default::default()
                },
                true
            )
            .outcome,
        Outcome::Confirm(51)
    );
}
#[test]
fn percent_adapter_rejects_failed_paint_without_publishing_or_refreshing() {
    let owner = Percent::try_new(owner_tests::runtime(), 0).unwrap();
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    unsafe {
        owner
            .render(services(), log, |out| *out = layout())
            .unwrap();
    }
    RECORD.with(|r| r.borrow_mut().fail = true);
    assert!(unsafe { owner.render(services(), log, |out| *out = layout()) }.is_err());
    assert!(owner.model.with(|m| m.controls.is_none()));
    RECORD.with(|r| assert_eq!(r.borrow().refreshes, 1));
    assert_eq!(
        owner
            .input(
                model::Input {
                    released: model::BACK,
                    ..Default::default()
                },
                true
            )
            .outcome,
        Outcome::Cancel
    );
}

#[test]
fn percent_adapter_resolves_contact_index_in_the_new_generation() {
    use xp_ui::scene::{HitSink, Interaction};
    let owner = Percent::try_new(owner_tests::runtime(), 50).unwrap();
    let mut controls = xp_ui::reader::percent::Controls::default();
    for (action, rect) in [
        (
            utility::CHROME,
            Rect {
                x: 0,
                y: 0,
                w: 100,
                h: 100,
            },
        ),
        (
            utility::SLIDER,
            Rect {
                x: 10,
                y: 10,
                w: 30,
                h: 30,
            },
        ),
    ] {
        controls
            .push(Interaction {
                action,
                rect,
                inputs: 1,
                ..Default::default()
            })
            .unwrap();
    }
    owner.publish(controls);
    owner.input(
        model::Input {
            touch_x: 15,
            touch_y: 15,
            touch_flags: model::TOUCH_PRESSED,
            ..Default::default()
        },
        true,
    );
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    unsafe {
        owner
            .render(services(), log, |out| *out = layout())
            .unwrap();
    }
    // In the new dialog generation index 1 is minus, even though the old
    // generation's index 1 was the slider. SDK highlights the new index.
    RECORD.with(|r| assert!(r.borrow().text_paints.contains(&(b"-".to_vec(), 1))));
}

#[test]
fn percent_reuses_owner_labels_and_terminates_shorter_values() {
    let owner = Percent::try_new(owner_tests::runtime(), 100).unwrap();
    RECORD.with(|r| *r.borrow_mut() = Record::default());
    let backing = unsafe { &(*owner.frame.get()).labels as *const _ };
    unsafe {
        owner
            .render(services(), log, |out| *out = layout())
            .unwrap();
    }
    RECORD.with(|r| assert!(r.borrow().labels.iter().any(|label| label == b"100%")));
    owner.input(
        model::Input {
            pressed: model::RIGHT,
            ..Default::default()
        },
        true,
    );
    RECORD.with(|r| {
        *r.borrow_mut() = Record {
            short_hints: true,
            ..Default::default()
        }
    });
    unsafe {
        owner
            .render(services(), log, |out| {
                *out = layout();
                out.touch = 0;
            })
            .unwrap();
    }
    assert_eq!(backing, unsafe { &(*owner.frame.get()).labels as *const _ });
    RECORD.with(|r| {
        let r = r.borrow();
        assert!(r.labels.iter().any(|label| label == b"1%"));
        assert!(r.labels.iter().any(|label| label == b"F 1%"));
        assert!(r.labels.iter().any(|label| label == b"S 10%"));
    });
}
