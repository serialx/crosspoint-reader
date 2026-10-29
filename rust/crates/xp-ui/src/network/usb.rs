//! USB ownership policy. The facade binds storage, time, diagnostics and reboot.
use crate::model::{Input, BACK, POWER};
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub host_wait_at: u32,
    pub start_failure_at: u32,
    pub forced_disconnect_at: u32,
    pub host_suspend_at: u32,
    pub phase: u8,
    pub preparing: u8,
    pub start_failed: u8,
    pub restart_requested: u8,
    pub forced_disconnect_requested: u8,
    pub host_suspend_pending: u8,
    pub reserved: [u8; 2],
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
pub enum Phase {
    Unsupported = 0,
    Waiting = 1,
    Connected = 2,
    Ejected = 3,
    Disconnected = 4,
    IoError = 5,
}
impl Phase {
    fn read(n: u8) -> Option<Self> {
        Some(match n {
            0 => Self::Unsupported,
            1 => Self::Waiting,
            2 => Self::Connected,
            3 => Self::Ejected,
            4 => Self::Disconnected,
            5 => Self::IoError,
            _ => return None,
        })
    }
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Call {
    Now = 0,
    Paint = 1,
    PaintWait = 2,
    Delay = 3,
    Diagnostic = 4,
    Begin = 5,
    End = 6,
    State = 7,
    Suspended = 8,
    Disconnect = 9,
    Restart = 10,
    PreparingStatus = 11,
    CancelPreparing = 12,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Diagnostic {
    StartFailure = 0,
    SuspendTimeout = 1,
    WaitTimeout = 2,
    StartTimeout = 3,
    IoFailure = 4,
    DisconnectFailure = 5,
    DisconnectTimeout = 6,
}

#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub call: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    pub publish: Option<unsafe extern "C" fn(*mut c_void, *const State)>,
}
pub trait Leaves {
    fn call(&mut self, operation: Call, value: u32) -> u32;
    fn publish(&mut self, state: &State);
}
impl Leaves for Services {
    fn call(&mut self, operation: Call, value: u32) -> u32 {
        self.call
            .map_or(0, |f| unsafe { f(self.context, operation as u32, value) })
    }
    fn publish(&mut self, state: &State) {
        if let Some(f) = self.publish {
            unsafe { f(self.context, state) }
        }
    }
}
impl State {
    fn paint(&self, leaves: &mut dyn Leaves) {
        leaves.publish(self);
        leaves.call(Call::Paint, 0);
    }
    fn restart(&mut self, leaves: &mut dyn Leaves) {
        if self.restart_requested != 0 {
            return;
        }
        self.restart_requested = 1;
        leaves.publish(self);
        leaves.call(Call::End, 0);
        leaves.call(Call::Delay, 20);
        leaves.call(Call::Restart, 0);
    }
    pub fn prevent_sleep(&self) -> bool {
        self.phase == Phase::Connected as u8
            || self.start_failed == 0 && self.phase == Phase::IoError as u8
    }
    pub fn run(&mut self, leaves: &mut dyn Leaves, event: u8, input: Input, home: bool) {
        if Phase::read(self.phase).is_none() {
            return;
        }
        match event {
            0 => {
                leaves.publish(self);
                if leaves.call(Call::PaintWait, 0) == 0 {
                    return;
                }
                self.begin(leaves);
            }
            1 => {
                if self.restart_requested == 0 && self.preparing == 0 {
                    leaves.call(Call::End, 0);
                }
            }
            2 => self.tick(leaves, input, home),
            _ => {}
        }
        leaves.publish(self);
    }
    fn begin(&mut self, leaves: &mut dyn Leaves) {
        if leaves.call(Call::Begin, 0) == 0 {
            leaves.call(Call::Diagnostic, Diagnostic::StartFailure as u32);
            self.preparing = 0;
            self.start_failed = 1;
            self.phase = Phase::IoError as u8;
            self.start_failure_at = leaves.call(Call::Now, 0);
        } else {
            self.preparing = 0;
            self.phase = Phase::Waiting as u8;
            self.host_wait_at = leaves.call(Call::Now, 0);
        }
        self.paint(leaves);
    }
    fn tick(&mut self, leaves: &mut dyn Leaves, input: Input, home: bool) {
        if self.preparing != 0 {
            if self.restart_requested != 0 {
                return;
            }
            if input.pressed & (BACK | POWER) != 0 || home {
                self.restart_requested = 1;
                leaves.publish(self);
                leaves.call(Call::CancelPreparing, 0);
            } else {
                match leaves.call(Call::PreparingStatus, 0) {
                    1 => self.begin(leaves),
                    2 => {
                        leaves.call(Call::Paint, 0);
                    }
                    _ => {}
                }
            }
            return;
        }
        if self.start_failed == 0 {
            let next = leaves.call(Call::State, 0) as u8;
            if Phase::read(next).is_none() {
                return;
            }
            if self.phase != next {
                self.phase = next;
                self.paint(leaves);
            }
        }
        if self.phase == Phase::Connected as u8 && leaves.call(Call::Suspended, 0) != 0 {
            if self.host_suspend_pending == 0 {
                self.host_suspend_pending = 1;
                self.host_suspend_at = leaves.call(Call::Now, 0);
            } else if leaves.call(Call::Now, 0).wrapping_sub(self.host_suspend_at) >= 2000 {
                leaves.call(Call::Diagnostic, Diagnostic::SuspendTimeout as u32);
                self.restart(leaves);
                return;
            }
        } else {
            self.host_suspend_pending = 0;
        }
        if self.phase == Phase::Waiting as u8
            && leaves.call(Call::Now, 0).wrapping_sub(self.host_wait_at) >= 300000
        {
            leaves.call(Call::Diagnostic, Diagnostic::WaitTimeout as u32);
            self.restart(leaves);
            return;
        }
        if self.start_failed != 0
            && leaves
                .call(Call::Now, 0)
                .wrapping_sub(self.start_failure_at)
                >= 30000
        {
            leaves.call(Call::Diagnostic, Diagnostic::StartTimeout as u32);
            self.restart(leaves);
            return;
        }
        if self.start_failed == 0 && self.phase == Phase::IoError as u8 {
            if self.forced_disconnect_requested == 0 {
                self.forced_disconnect_requested = 1;
                self.forced_disconnect_at = leaves.call(Call::Now, 0);
                leaves.call(Call::Diagnostic, Diagnostic::IoFailure as u32);
                if leaves.call(Call::Disconnect, 0) == 0 {
                    leaves.call(Call::Diagnostic, Diagnostic::DisconnectFailure as u32);
                }
            } else if leaves
                .call(Call::Now, 0)
                .wrapping_sub(self.forced_disconnect_at)
                >= 1000
            {
                leaves.call(Call::Diagnostic, Diagnostic::DisconnectTimeout as u32);
                self.restart(leaves);
            }
            return;
        }
        if (self.phase == Phase::Waiting as u8 || self.start_failed != 0)
            && (input.pressed & (BACK | POWER) != 0 || home)
        {
            self.restart(leaves);
            return;
        }
        if matches!(
            Phase::read(self.phase),
            Some(Phase::Ejected | Phase::Disconnected | Phase::Unsupported)
        ) {
            self.restart(leaves);
        }
    }
}

const _: () = {
    assert!(Phase::Unsupported as u32 == 0);
    assert!(Phase::Waiting as u32 == 1);
    assert!(Phase::Connected as u32 == 2);
    assert!(Phase::Ejected as u32 == 3);
    assert!(Phase::Disconnected as u32 == 4);
    assert!(Phase::IoError as u32 == 5);
    assert!(Call::Now as u32 == 0);
    assert!(Call::Paint as u32 == 1);
    assert!(Call::PaintWait as u32 == 2);
    assert!(Call::Delay as u32 == 3);
    assert!(Call::Diagnostic as u32 == 4);
    assert!(Call::Begin as u32 == 5);
    assert!(Call::End as u32 == 6);
    assert!(Call::State as u32 == 7);
    assert!(Call::Suspended as u32 == 8);
    assert!(Call::Disconnect as u32 == 9);
    assert!(Call::Restart as u32 == 10);
    assert!(Call::PreparingStatus as u32 == 11);
    assert!(Call::CancelPreparing as u32 == 12);
    assert!(Diagnostic::StartFailure as u32 == 0);
    assert!(Diagnostic::SuspendTimeout as u32 == 1);
    assert!(Diagnostic::WaitTimeout as u32 == 2);
    assert!(Diagnostic::StartTimeout as u32 == 3);
    assert!(Diagnostic::IoFailure as u32 == 4);
    assert!(Diagnostic::DisconnectFailure as u32 == 5);
    assert!(Diagnostic::DisconnectTimeout as u32 == 6);
    assert!(core::mem::size_of::<State>() == 24);
    assert!(core::mem::align_of::<State>() == 4);
    assert!(core::mem::offset_of!(State, phase) == 16);
    assert!(core::mem::offset_of!(State, reserved) == 22);
    assert!(core::mem::size_of::<Services>() == 3 * core::mem::size_of::<usize>());
};
