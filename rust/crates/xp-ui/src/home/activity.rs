//! Small Home state shared by input and paint under the platform model lock.
use super::{grid, HomeState, HomeTouch, HomeUpdate};
use crate::model::{Input, TOUCH_HELD, TOUCH_PRESSED};

#[derive(Clone, Copy, Default)]
pub enum Geometry {
    #[default]
    Unpublished,
    Standard(HomeTouch),
    Grid(grid::Geometry),
}

#[derive(Default)]
enum Entry {
    #[default]
    Initializing,
    PaintRequested,
    Ready,
}

pub enum InputResult {
    Idle,
    RetryPaint,
    Update(HomeUpdate),
}

#[derive(Default)]
pub struct Model {
    state: HomeState,
    geometry: Geometry,
    entry: Entry,
    failed: bool,
    suppress_buttons: u32,
    suppress_contact: bool,
}

impl Model {
    /// An early notification is retained until immutable entry resources exist.
    pub fn begin_render(&mut self) -> Option<HomeState> {
        match self.entry {
            Entry::Ready => Some(self.state),
            Entry::Initializing | Entry::PaintRequested => {
                self.entry = Entry::PaintRequested;
                None
            }
        }
    }

    /// Called after the owner installs book identities and all initial state.
    pub fn complete_entry(&mut self, state: HomeState) -> bool {
        let requested = matches!(self.entry, Entry::PaintRequested);
        self.state = state;
        self.entry = Entry::Ready;
        requested
    }

    pub fn publish(&mut self, geometry: Geometry) -> bool {
        if !matches!(self.entry, Entry::Ready) {
            return false;
        }
        self.geometry = geometry;
        self.failed = false;
        true
    }

    pub fn reject(&mut self) {
        self.failed = true;
        self.geometry = Geometry::Unpublished;
        self.suppress_buttons = u16::MAX.into();
        self.suppress_contact = true;
        self.state.last_repeat = 0;
    }

    pub fn input(&mut self, input: Input) -> InputResult {
        if !matches!(self.entry, Entry::Ready) {
            return InputResult::Idle;
        }
        if self.failed || self.suppress_buttons != 0 || self.suppress_contact {
            let consume = self.failed
                || self.suppress_buttons & (input.pressed | input.released | input.held) != 0
                || self.suppress_contact && (input.touch_flags != 0 || input.swipe != 0);
            if self.failed {
                self.suppress_buttons |= input.held;
                self.suppress_contact |= input.touch_flags & (TOUCH_PRESSED | TOUCH_HELD) != 0;
            }
            self.suppress_buttons &= input.held;
            if input.touch_flags & (TOUCH_PRESSED | TOUCH_HELD) == 0 {
                self.suppress_contact = false;
            }
            if self.failed {
                return InputResult::RetryPaint;
            }
            if consume {
                return InputResult::Idle;
            }
        }
        if !self.state.interest(input.now_ms).due(&input) {
            return InputResult::Idle;
        }
        let update = if self.state.grid != 0 {
            self.state.update_grid(
                input,
                match &self.geometry {
                    Geometry::Grid(g) => Some(g),
                    _ => None,
                },
            )
        } else {
            let geometry = match self.geometry {
                Geometry::Standard(g) => g,
                _ => HomeTouch::default(),
            };
            self.state.update(input, &geometry)
        };
        InputResult::Update(update)
    }

    pub fn painted(&mut self, changed: bool) -> crate::model::Effect {
        self.state.painted(changed)
    }

    pub fn loaded(&mut self) {
        self.state.loaded();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{EffectKind, BACK, CONFIRM};

    #[test]
    fn notification_during_entry_waits_for_complete_resources() {
        let mut model = Model::default();
        assert!(model.begin_render().is_none());
        assert!(!model.publish(Geometry::Standard(HomeTouch::default())));
        assert!(matches!(
            model.input(Input {
                released: CONFIRM,
                ..Input::default()
            }),
            InputResult::Idle
        ));
        let state = HomeState::new(2, false, true, false, true);
        assert!(model.complete_entry(state));
        assert_eq!(model.begin_render().unwrap().books, 2);
        assert!(model.publish(Geometry::Standard(HomeTouch::default())));
        let InputResult::Update(result) = model.input(Input {
            released: CONFIRM,
            ..Input::default()
        }) else {
            panic!("entry never became ready")
        };
        assert_eq!(result.effect.kind, EffectKind::Activate as u8);
        assert_eq!(result.effect.value, 0);
    }

    #[test]
    fn recovered_frame_consumes_contacts_that_spanned_failure() {
        let mut model = Model::default();
        model.complete_entry(HomeState::new(1, false, false, false, true));
        model.reject();
        assert!(matches!(
            model.input(Input {
                held: CONFIRM,
                released: BACK,
                ..Input::default()
            }),
            InputResult::RetryPaint
        ));
        // Back opens the continuing book; rejection must not treat it as Cancel.
        model.publish(Geometry::Standard(HomeTouch::default()));
        assert!(matches!(
            model.input(Input {
                released: CONFIRM,
                ..Input::default()
            }),
            InputResult::Idle
        ));
        let InputResult::Update(result) = model.input(Input {
            released: BACK,
            ..Input::default()
        }) else {
            panic!("fresh input stayed suppressed")
        };
        assert_eq!(result.effect.kind, EffectKind::Activate as u8);
        assert_eq!(result.effect.value, 0);
    }

    #[test]
    fn recovery_before_input_consumes_one_fresh_gesture() {
        let mut model = Model::default();
        model.complete_entry(HomeState::new(1, false, false, false, true));
        model.reject();
        model.publish(Geometry::Standard(HomeTouch::default()));
        assert!(matches!(
            model.input(Input {
                released: CONFIRM,
                ..Input::default()
            }),
            InputResult::Idle
        ));
        assert!(matches!(
            model.input(Input {
                released: CONFIRM,
                ..Input::default()
            }),
            InputResult::Update(_)
        ));
    }
}
