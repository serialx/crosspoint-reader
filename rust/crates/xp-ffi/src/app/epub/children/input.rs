use super::owned::ListPublication;
use xp_ui::{
    model::{Effect, EffectKind, Input, BACK, TOUCH_HELD, TOUCH_PRESSED, TOUCH_RELEASED},
    reader_chrome::list::Screen,
};

pub(super) struct Update {
    pub(super) effect: Effect,
    pub(super) retry: bool,
    pub(super) haptic: bool,
}

impl ListPublication {
    pub(super) fn input(
        &mut self,
        mut input: Input,
        count: u16,
        present: bool,
        kind: Screen,
    ) -> Update {
        let consume = suppressed(
            input,
            self.failed,
            &mut self.suppress_buttons,
            &mut self.suppress_contact,
        );
        if consume {
            return Update {
                effect: Effect {
                    kind: if self.failed && input.released & BACK != 0 {
                        EffectKind::Cancel as u8
                    } else {
                        EffectKind::None as u8
                    },
                    ..Default::default()
                },
                retry: self.failed,
                haptic: false,
            };
        }
        if self.ready && input.touch_flags != 0 {
            if let Some(hit) = self.hits.hit(input.touch_x, input.touch_y) {
                input.action = hit.action;
                input.value = hit.value;
            }
        }
        let effect = self.state.update(input, count, present, kind, false, -1);
        Update {
            effect,
            retry: false,
            haptic: input.action != 0
                && input.touch_flags & TOUCH_RELEASED != 0
                && effect.kind == EffectKind::Activate as u8,
        }
    }
}

// Native RustActivityAdapter keeps rejected held gestures suppressed through release.
pub(super) fn suppressed(input: Input, failed: bool, buttons: &mut u32, touch: &mut bool) -> bool {
    let consume = failed
        || *buttons & (input.pressed | input.released | input.held) != 0
        || *touch && (input.touch_flags != 0 || input.swipe != 0);
    let contact = input.touch_flags & (TOUCH_PRESSED | TOUCH_HELD) != 0;
    if failed {
        *buttons |= input.held;
        *touch |= contact;
    }
    *buttons &= input.held;
    if !contact {
        *touch = false;
    }
    consume
}
