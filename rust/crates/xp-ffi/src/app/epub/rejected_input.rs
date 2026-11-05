use super::EpubSession;
use core::mem::MaybeUninit;
use xp_ui::model::{Input, TOUCH_HELD, TOUCH_PRESSED};

impl EpubSession {
    pub(super) fn consume_rejected_input(&self) -> bool {
        if self.model(|state| {
            let gate = &state.scene_gate;
            !gate.failed && gate.suppress_buttons == 0 && !gate.suppress_contact
        }) {
            return false;
        }
        let mut input = MaybeUninit::<Input>::uninit();
        unsafe { (self.leaves.input_snapshot)(input.as_mut_ptr()) };
        let input = unsafe { input.assume_init() };
        let (failed, consume) = self.model(|state| {
            let gate = &mut state.scene_gate;
            let failed = gate.failed;
            let consume = failed
                || u32::from(gate.suppress_buttons) & (input.pressed | input.released | input.held)
                    != 0
                || gate.suppress_contact && (input.touch_flags != 0 || input.swipe != 0);
            let contact = input.touch_flags & (TOUCH_PRESSED | TOUCH_HELD) != 0;
            if failed {
                gate.suppress_buttons |= input.held as u16;
                gate.suppress_contact |= contact;
            }
            gate.suppress_buttons &= input.held as u16;
            if !contact {
                gate.suppress_contact = false;
            }
            (failed, consume)
        });
        if failed {
            self.request_update();
            self.cancel_rejected_input(&input);
        }
        consume
    }
}
