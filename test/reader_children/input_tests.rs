use crate::{owned::Chapters, owner_tests};
use xp_ui::{
    model::{EffectKind, Input, BACK, CONFIRM, TOUCH_HELD, TOUCH_RELEASED},
    reader_chrome::{hits, list::Screen},
    scene::{HitSink, Interaction, Rect},
};

#[test]
fn list_failure_consumes_held_inputs_through_release_but_allows_back() {
    let child = Chapters::try_new(owner_tests::runtime(), 2).unwrap();
    child.model.with(|model| {
        model.failed = true;
        let held = Input {
            held: CONFIRM,
            touch_flags: TOUCH_HELD,
            ..Default::default()
        };
        let update = model.input(held, 10, true, Screen::Chapters);
        assert_eq!(update.effect.kind, EffectKind::None as u8);
        assert!(update.retry);
        model.failed = false;
        model.ready = true;
        let release = Input {
            released: CONFIRM,
            touch_flags: TOUCH_RELEASED,
            ..Default::default()
        };
        assert_eq!(
            model.input(release, 10, true, Screen::Chapters).effect.kind,
            EffectKind::None as u8
        );
        assert_eq!(
            model
                .input(
                    Input {
                        released: CONFIRM,
                        ..Default::default()
                    },
                    10,
                    true,
                    Screen::Chapters
                )
                .effect
                .kind,
            EffectKind::Activate as u8
        );
        model.failed = true;
        assert_eq!(
            model
                .input(
                    Input {
                        released: BACK,
                        ..Default::default()
                    },
                    10,
                    true,
                    Screen::Chapters
                )
                .effect
                .kind,
            EffectKind::Cancel as u8
        );
    });
}

#[test]
fn list_touch_uses_only_completed_hits_and_reports_activation_haptic() {
    let child = Chapters::try_new(owner_tests::runtime(), 0).unwrap();
    child.model.with(|model| {
        model.hits = hits::Layout::new(1, 1);
        model
            .hits
            .push(Interaction {
                rect: Rect {
                    x: 10,
                    y: 10,
                    w: 80,
                    h: 40,
                },
                action: 1,
                value: 3,
                generation: 1,
                inputs: 1,
                flags: 0,
            })
            .unwrap();
        let input = Input {
            touch_flags: TOUCH_RELEASED,
            touch_x: 20,
            touch_y: 20,
            ..Default::default()
        };
        assert!(!model.input(input, 10, true, Screen::Chapters).haptic);
        model.ready = true;
        let update = model.input(input, 10, true, Screen::Chapters);
        assert_eq!(update.effect.kind, EffectKind::Activate as u8);
        assert_eq!(update.effect.value, 3);
        assert!(update.haptic);
    });
}

#[test]
fn child_target_survives_the_owner_move_into_application_storage() {
    use crate::{
        child_result::{ChapterSelection, ReaderChildResult},
        owned::ReaderChild,
        resources::Text,
    };
    let chapter = Chapters::try_new(owner_tests::runtime(), 0).unwrap();
    let anchor = Text::copy(b"move-only-anchor").unwrap();
    let pointer = anchor.bytes().as_ptr();
    unsafe {
        *chapter.result.get() = Some(ReaderChildResult::Chapter(Some(ChapterSelection {
            spine: 9,
            anchor,
        })));
    }
    let owner = ReaderChild::Chapters(chapter.into());
    let target = owner.target();
    let stored = Box::new(owner);
    let Some(ReaderChildResult::Chapter(Some(result))) = (unsafe { target.take_result() }) else {
        panic!("missing owned result");
    };
    assert_eq!(result.spine, 9);
    assert_eq!(result.anchor.bytes().as_ptr(), pointer);
    assert!(unsafe { stored.take_result() }.is_none());
}

#[test]
fn failed_list_repaint_suppresses_release_even_without_an_intervening_loop() {
    let child = Chapters::try_new(owner_tests::runtime(), 0).unwrap();
    child.model.with(|model| {
        let frame = unsafe { &*child.frame.get() };
        model.painted(&frame.list, false);
        model.painted(&frame.list, true);
        assert_eq!(
            model
                .input(
                    Input {
                        released: CONFIRM,
                        ..Default::default()
                    },
                    10,
                    true,
                    Screen::Chapters
                )
                .effect
                .kind,
            EffectKind::None as u8
        );
        assert_eq!(
            model
                .input(
                    Input {
                        released: CONFIRM,
                        ..Default::default()
                    },
                    10,
                    true,
                    Screen::Chapters
                )
                .effect
                .kind,
            EffectKind::Activate as u8
        );
    });
}
