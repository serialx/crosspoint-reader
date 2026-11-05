//! Percent dialog input, including the shared SDK's drag and tap-flash semantics.
use crate::{
    model::{self, Input},
    scene::{Error, HitSink, Interaction},
    utility::{OK, SLIDER, STEP},
};

/// The dialog has one chrome guard, one slider and three buttons. Build separately
/// and copy into the model critical section only after the complete frame paints.
#[derive(Clone, Copy, Default)]
pub struct Controls {
    hits: [Interaction; 5],
    count: u8,
}

impl Controls {
    pub fn as_slice(&self) -> &[Interaction] {
        &self.hits[..usize::from(self.count)]
    }
}

impl HitSink for Controls {
    fn push(&mut self, hit: Interaction) -> Result<(), Error> {
        let slot = self
            .hits
            .get_mut(usize::from(self.count))
            .ok_or(Error::Capacity)?;
        *slot = hit;
        self.count += 1;
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    None,
    Redraw,
    Cancel,
    Confirm(i32),
}

#[derive(Clone, Copy, Debug)]
pub struct Update {
    pub outcome: Outcome,
    pub feedback: bool,
}
impl Update {
    fn new(outcome: Outcome) -> Self {
        Self {
            outcome,
            feedback: false,
        }
    }
}

#[derive(Clone, Copy)]
pub struct State {
    value: i32,
    last_repeat: u32,
    active: i16,
    last_drag_x: i16,
    contact_held: bool,
    dragging: bool,
    flash: Option<(u32, i32)>,
    flash_ticks: u8,
}

impl State {
    pub fn new(value: i32) -> Self {
        Self {
            value,
            last_repeat: 0,
            active: -1,
            last_drag_x: -1,
            contact_held: false,
            dragging: false,
            flash: None,
            flash_ticks: 0,
        }
    }

    pub fn value(&self) -> i32 {
        self.value
    }

    fn step(&mut self, delta: i32) {
        let raw = self.value + delta;
        self.value = if raw > 0 && raw % 100 == 0 {
            100
        } else {
            raw.rem_euclid(100)
        };
    }

    pub fn render_begin(&mut self) {
        if self.flash_ticks > 0 {
            self.flash_ticks -= 1;
            if self.flash_ticks == 0 {
                self.flash = None;
            }
        }
    }

    pub fn flashing(&self, action: u32, value: i32) -> bool {
        self.flash == Some((action, value))
    }

    fn route(&mut self, input: Input, hits: &[Interaction]) -> Option<(u32, i32, Option<i32>)> {
        let pressed = input.touch_flags & model::TOUCH_PRESSED != 0;
        let released = input.touch_flags & model::TOUCH_RELEASED != 0;
        let held = input.touch_flags & model::TOUCH_HELD != 0;
        let find = |mask| {
            hits.iter()
                .enumerate()
                .rev()
                .find(|(_, hit)| {
                    hit.flags & 1 == 0
                        && hit.inputs & mask != 0
                        && hit.rect.contains(input.touch_x, input.touch_y)
                })
                .map(|(index, _)| index as i16)
        };
        if held && !self.contact_held {
            self.active = find(2).unwrap_or(-1);
            self.last_drag_x = -1;
        }
        if held {
            self.contact_held = true;
        }
        if released {
            self.contact_held = false;
        }
        if pressed {
            self.active = find(1).unwrap_or(-1);
        }
        let active = usize::try_from(self.active)
            .ok()
            .and_then(|index| hits.get(index));
        let drag = active.filter(|hit| hit.flags & 1 == 0 && hit.inputs & 2 != 0);
        if held {
            if let Some(hit) = drag {
                self.last_drag_x = input.touch_x;
                return Some((hit.action, hit.value, Some(permille(hit, input.touch_x))));
            }
        }
        if released {
            self.active = -1;
            if self.last_drag_x >= 0 {
                if let Some(hit) = drag {
                    let value = permille(hit, self.last_drag_x);
                    self.last_drag_x = -1;
                    return Some((hit.action, hit.value, Some(value)));
                }
            }
            self.last_drag_x = -1;
            return find(1).map(|index| {
                let hit = &hits[index as usize];
                (
                    hit.action,
                    hit.value,
                    (hit.inputs & 2 != 0).then(|| permille(hit, input.touch_x)),
                )
            });
        }
        None
    }

    /// `hits` is the last complete generation, or None before publication/after reset.
    pub fn update(
        &mut self,
        input: Input,
        hits: Option<&[Interaction]>,
        edge_buttons: bool,
    ) -> Update {
        let touch = input.touch_flags != 0;
        let routed = hits.is_some() && touch;
        if let Some(event) = hits
            .filter(|_| touch)
            .and_then(|hits| self.route(input, hits))
        {
            let (action, value, position) = event;
            if position.is_some() {
                self.dragging = true;
            }
            let outcome = match action {
                SLIDER => {
                    if let Some(position) = position {
                        self.value = ((position * 100 + 500) / 1000).clamp(0, 100);
                    }
                    Outcome::Redraw
                }
                STEP => {
                    self.step(value);
                    Outcome::Redraw
                }
                OK => {
                    self.flash = None;
                    self.flash_ticks = 0;
                    Outcome::Confirm(self.value)
                }
                _ => Outcome::Redraw,
            };
            if position.is_none() && action != OK {
                self.flash = Some((action, value));
                self.flash_ticks = 2;
            }
            return Update {
                outcome,
                feedback: input.touch_flags & model::TOUCH_RELEASED != 0,
            };
        }
        if hits.is_some() && self.dragging {
            // UiAppHost returns an empty snapshot when it did not route touch this frame.
            if !routed || input.touch_flags & model::TOUCH_HELD == 0 {
                self.dragging = false;
            }
            return Update::new(Outcome::None);
        }
        if routed && input.touch_flags & model::TOUCH_RELEASED != 0 && input.touch_x >= 0 {
            return Update::new(Outcome::Cancel);
        }
        if input.released & model::BACK != 0 {
            return Update::new(Outcome::Cancel);
        }
        match input.swipe {
            model::SWIPE_RIGHT => {
                self.step(10);
                return Update::new(Outcome::Redraw);
            }
            model::SWIPE_LEFT => {
                self.step(-10);
                return Update::new(Outcome::Redraw);
            }
            _ => {}
        }
        if input.released & model::CONFIRM != 0 {
            return Update::new(Outcome::Confirm(self.value));
        }
        let mut changed = false;
        for (key, delta) in [
            (model::LEFT, -1),
            (model::RIGHT, 1),
            (model::UP, if edge_buttons { -10 } else { 10 }),
            (model::DOWN, if edge_buttons { 10 } else { -10 }),
        ] {
            if input.pressed & key != 0 {
                self.step(delta);
                changed = true;
            }
            if (input.pressed | input.released) & key != 0 {
                self.last_repeat = 0;
            } else if input.held & key != 0
                && input.held_ms > 500
                && input.now_ms.wrapping_sub(self.last_repeat) > 500
            {
                self.step(delta);
                self.last_repeat = input.now_ms;
                changed = true;
            }
        }
        Update::new(if changed {
            Outcome::Redraw
        } else {
            Outcome::None
        })
    }
}

fn permille(hit: &Interaction, x: i16) -> i32 {
    if hit.rect.w <= 1 {
        0
    } else {
        ((i32::from(x) - i32::from(hit.rect.x)) * 1000 / (i32::from(hit.rect.w) - 1)).clamp(0, 1000)
    }
}
