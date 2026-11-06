//! Scene admission policy. Callers hold their model lock and own cancellation.
use crate::model::{Input, TOUCH_HELD, TOUCH_PRESSED};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct SceneGate {
    suppressed_buttons: u16,
    failed: u8,
    suppressed_contact: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Publication {
    Current,
    Stale,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Admission {
    pub consume: bool,
    pub retry: bool,
}

impl SceneGate {
    pub fn failed(&self) -> bool {
        self.failed != 0
    }

    pub fn reject(&mut self) {
        self.failed = 1;
        // Paint may recover before input observes rejection.
        self.suppressed_buttons = u16::MAX;
        self.suppressed_contact = 1;
    }

    pub fn publish(&mut self, current: bool) -> Publication {
        if !current {
            return Publication::Stale;
        }
        self.failed = 0;
        Publication::Current
    }

    pub fn input(&mut self, input: &Input) -> Admission {
        let failed = self.failed();
        let consume = failed
            || u32::from(self.suppressed_buttons) & (input.pressed | input.released | input.held)
                != 0
            || self.suppressed_contact != 0 && (input.touch_flags != 0 || input.swipe != 0);
        let contact = input.touch_flags & (TOUCH_PRESSED | TOUCH_HELD) != 0;
        if failed {
            self.suppressed_buttons |= input.held as u16;
            self.suppressed_contact |= u8::from(contact);
        }
        self.suppressed_buttons &= input.held as u16;
        if !contact {
            self.suppressed_contact = 0;
        }
        Admission {
            consume,
            retry: failed,
        }
    }
}

/// List scratch only; allocation and release order belong to the caller.
pub fn next_capacity(current: u16, minimum: u16, step: u16, limit: u16) -> Option<u16> {
    if step == 0 || limit == 0 || limit > 128 || current >= limit || minimum > limit {
        return None;
    }
    Some(current.checked_add(step)?.min(limit).max(minimum))
}

const _: () = {
    assert!(core::mem::size_of::<SceneGate>() == 4);
    assert!(core::mem::align_of::<SceneGate>() == 2);
    assert!(core::mem::offset_of!(SceneGate, failed) == 2);
    assert!(core::mem::offset_of!(SceneGate, suppressed_contact) == 3);
};
