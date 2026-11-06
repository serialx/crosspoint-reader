use super::owned::ListPublication;
use xp_ui::{
    model::{Effect, EffectKind, Input, BACK, TOUCH_RELEASED},
    reader_chrome::list::Screen,
};

pub(super) struct Update {
    pub(super) effect: Effect,
    pub(super) retry: bool,
    pub(super) haptic: bool,
    pub(super) long_haptic: bool,
}

impl ListPublication {
    pub(super) fn input(
        &mut self,
        input: Input,
        count: u16,
        present: bool,
        kind: Screen,
    ) -> Update {
        self.input_with_gesture(input, count, present, kind, false, None)
    }

    pub(super) fn clipping_input(
        &mut self,
        input: Input,
        count: u16,
        long_press: bool,
        long_touch: Option<(i16, i16)>,
    ) -> Update {
        self.input_with_gesture(
            input,
            count,
            true,
            Screen::Clippings,
            long_press,
            long_touch,
        )
    }

    fn input_with_gesture(
        &mut self,
        mut input: Input,
        count: u16,
        present: bool,
        kind: Screen,
        mut long_press: bool,
        long_touch: Option<(i16, i16)>,
    ) -> Update {
        let admission = self.gate.input(&input);
        if admission.consume {
            return Update {
                effect: Effect {
                    kind: if admission.retry && input.released & BACK != 0 {
                        EffectKind::Cancel as u8
                    } else {
                        EffectKind::None as u8
                    },
                    ..Default::default()
                },
                retry: admission.retry,
                haptic: false,
                long_haptic: false,
            };
        }
        if self.ready && input.touch_flags != 0 {
            if let Some(hit) = self.hits.hit(input.touch_x, input.touch_y) {
                input.action = hit.action;
                input.value = hit.value;
            }
        }
        let mut long_hit = false;
        if self.ready {
            if let Some(hit) = long_touch.and_then(|(x, y)| self.hits.hit(x, y)) {
                if hit.action == 1 {
                    input.action = hit.action;
                    input.value = hit.value;
                    long_press = true;
                    long_hit = true;
                }
            }
        }
        let effect = self
            .state
            .update(input, count, present, kind, long_press, -1);
        Update {
            effect,
            retry: false,
            haptic: input.action != 0
                && input.touch_flags & TOUCH_RELEASED != 0
                && effect.kind == EffectKind::Activate as u8,
            long_haptic: long_hit
                && (effect.kind == xp_ui::reader_chrome::ACTIONS || effect.invalidate != 0),
        }
    }
}
