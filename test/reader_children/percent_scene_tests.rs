use xp_ui::{
    model::Metrics,
    scene::{Arena, Command, Error, Rect},
    utility::{self, Layout, Texts},
};
struct Fonts;
impl Metrics for Fonts {
    fn width(&mut self, _: i32, bytes: &[u8], _: u8) -> i32 {
        bytes.len() as i32 * 10
    }
    fn line_height(&mut self, _: i32) -> i32 {
        24
    }
}
impl Texts for Fonts {
    fn present(&self, _: u32) -> Result<bool, Error> {
        Ok(true)
    }
    fn height(&self, _: u32, _: i32, _: i16, _: u8, _: u8) -> Result<i16, Error> {
        Ok(24)
    }
}
#[test]
fn shared_percent_drawing_fits_eighteen_inline_commands() {
    for orientation in 0..4 {
        let (w, h) = if orientation % 2 == 0 {
            (480, 800)
        } else {
            (800, 480)
        };
        for touch in [0, 1] {
            for value in 0..=100 {
                for (active, active_value) in [
                    (0, 0),
                    (utility::CHROME, 0),
                    (utility::SLIDER, 0),
                    (utility::STEP, -1),
                    (utility::STEP, 1),
                    (utility::OK, 0),
                ] {
                    for flash_pending in 0..=2 {
                        let screen = Rect { x: 0, y: 0, w, h };
                        let layout = Layout {
                            screen,
                            safe: screen,
                            touch,
                            popup_padding: 16,
                            popup_gap: 8,
                            popup_side_margin: 16,
                            selection_padding: 4,
                            space_sm: 4,
                            space_md: 8,
                            space_lg: 16,
                            popup_radius: 8,
                            popup_border: 2,
                            capsule_radius: 28,
                            ..Default::default()
                        };
                        let state = utility::State {
                            kind: utility::Screen::Interval as u8,
                            minimum: 0,
                            maximum: 100,
                            value,
                            active_action: active,
                            active_value,
                            flash_pending,
                            ..Default::default()
                        };
                        let mut commands = [Command::default(); 18];
                        let mut controls = xp_ui::reader::percent::Controls::default();
                        let mut arena = Arena::with_hit_sink(&mut commands, &mut controls).unwrap();
                        let (_, refresh) = utility::view(
                            &mut arena,
                            &state,
                            &layout,
                            &mut Fonts,
                            &Fonts,
                            Default::default(),
                        )
                        .unwrap();
                        let scene = arena.finish(refresh, 1).unwrap();
                        assert!(scene.commands.len() <= 18);
                        assert_eq!(scene.commands.len(), if touch != 0 { 18 } else { 14 });
                        assert_eq!(controls.as_slice().len(), if touch != 0 { 5 } else { 2 });
                    }
                }
            }
        }
    }
}

#[test]
fn percent_command_overflow_reports_and_preserves_the_complete_interaction_generation() {
    use xp_ui::scene::{Interaction, Refresh};
    let owner = crate::owned::Percent::try_new(crate::owner_tests::runtime(), 50).unwrap();
    let frame = unsafe { &mut *owner.frame.get() };
    let mut arena =
        Arena::with_truncated_commands(&mut frame.commands, &mut frame.controls).unwrap();
    for index in 0..19 {
        arena
            .push(Command {
                resource: index,
                ..Default::default()
            })
            .unwrap();
    }
    arena
        .hit(Interaction {
            rect: Rect {
                x: 10,
                y: 10,
                w: 30,
                h: 30,
            },
            action: utility::OK,
            generation: 1,
            inputs: 1,
            ..Default::default()
        })
        .unwrap();
    let mut diagnostic = Vec::new();
    let scene = arena
        .finish_reporting_overflow(Refresh::Fast, 1, |count| diagnostic.push(count))
        .unwrap();
    assert_eq!(diagnostic, [1]);
    assert_eq!(scene.commands.len(), 18);
    assert_eq!(scene.commands.last().unwrap().resource, 17);
    assert_eq!(frame.controls.as_slice().len(), 1);
    assert_eq!(frame.controls.as_slice()[0].action, utility::OK);
    owner.publish(frame.controls);
    let update = owner.input(
        xp_ui::model::Input {
            touch_x: 15,
            touch_y: 15,
            touch_flags: xp_ui::model::TOUCH_RELEASED,
            ..Default::default()
        },
        true,
    );
    assert_eq!(update.outcome, xp_ui::reader::percent::Outcome::Confirm(50));

    let mut commands = [Command::default(); 18];
    let mut strict = Arena::new(&mut commands, &mut [], &mut []).unwrap();
    for _ in 0..18 {
        strict.push(Command::default()).unwrap();
    }
    assert_eq!(strict.push(Command::default()), Err(Error::Capacity));
    assert!(matches!(
        strict.finish(Refresh::Fast, 1),
        Err(Error::Capacity)
    ));
}
