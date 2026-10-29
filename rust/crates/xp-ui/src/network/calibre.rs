//! Calibre screen policy; the facade binds existing transport operations.
use crate::model::{Input, BACK};
use core::ffi::c_void;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub last_handle: u32,
    pub received: u32,
    pub total: u32,
    pub completed_at: u32,
    pub processed_at: u32,
    pub phase: u8,
    pub exit_requested: u8,
    pub reserved: [u8; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Upload {
    pub received: u32,
    pub total: u32,
    pub completed_at: u32,
    pub in_progress: u8,
    pub filename_changed: u8,
    pub reserved: [u8; 2],
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Phase {
    Wifi = 0,
    Starting = 1,
    Running = 2,
    Error = 3,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Call {
    Now = 0,
    Paint = 1,
    WifiConnected = 2,
    WifiPresent = 3,
    WifiDisconnect = 4,
    Delay = 5,
    Restart = 6,
    MdnsEnd = 7,
    MdnsBegin = 8,
    CacheRelease = 9,
    ServerBegin = 10,
    ServerRunning = 11,
    HandleClient = 12,
    Watchdog = 13,
    Yield = 14,
    Back = 15,
    DropUpload = 16,
    Finish = 17,
    ConnectedDetails = 18,
    SelectWifi = 19,
    LogGap = 20,
}
pub const CLEAR_ALL: u32 = 1;
pub const COPY_UPLOAD: u32 = 2;
pub const CLEAR_UPLOAD: u32 = 4;
pub const COPY_COMPLETED: u32 = 8;
pub const CLEAR_COMPLETED: u32 = 16;
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub call: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    pub publish: Option<unsafe extern "C" fn(*mut c_void, *const State, u32)>,
    pub upload: Option<unsafe extern "C" fn(*mut c_void, *mut Upload)>,
}
impl Services {
    fn call(&mut self, operation: Call, value: u32) -> u32 {
        self.call
            .map_or(0, |f| unsafe { f(self.context, operation as u32, value) })
    }
    fn publish(&mut self, state: &State, changes: u32) {
        if let Some(f) = self.publish {
            unsafe { f(self.context, state, changes) }
        }
    }
    fn upload(&mut self) -> Upload {
        let mut out = Upload::default();
        if let Some(f) = self.upload {
            unsafe { f(self.context, &mut out) }
        }
        out
    }
}
impl State {
    fn paint(&self, leaves: &mut Services) {
        leaves.publish(self, 0);
        leaves.call(Call::Paint, 0);
    }
    fn start(&mut self, leaves: &mut Services) {
        self.phase = Phase::Starting as u8;
        self.paint(leaves);
        leaves.call(Call::MdnsEnd, 0);
        leaves.call(Call::MdnsBegin, 0);
        leaves.call(Call::CacheRelease, 0);
        leaves.call(Call::ServerBegin, 0);
        self.phase = if leaves.call(Call::ServerRunning, 0) != 0 {
            Phase::Running
        } else {
            Phase::Error
        } as u8;
        self.paint(leaves);
    }
    pub fn run(&mut self, leaves: &mut Services, event: u8, input: Input, value: bool) {
        if self.phase > Phase::Error as u8 {
            return;
        }
        match event {
            0 => {
                leaves.call(Call::Paint, 0);
                *self = Self::default();
                leaves.publish(self, CLEAR_ALL);
                if leaves.call(Call::WifiConnected, 0) != 0 {
                    leaves.call(Call::ConnectedDetails, 0);
                    self.start(leaves);
                } else {
                    leaves.call(Call::SelectWifi, 0);
                }
            }
            1 => {
                leaves.call(Call::MdnsEnd, 0);
                if leaves.call(Call::WifiPresent, 0) != 0 {
                    leaves.call(Call::WifiDisconnect, 0);
                    leaves.call(Call::Delay, 30);
                    leaves.call(Call::Restart, 0);
                }
            }
            2 => self.tick(leaves, input),
            4 => {
                if value {
                    self.start(leaves);
                } else {
                    leaves.call(Call::Finish, 0);
                }
            }
            _ => {}
        }
        leaves.publish(self, 0);
    }
    fn tick(&mut self, leaves: &mut Services, input: Input) {
        self.exit_requested |= u8::from(input.pressed & BACK != 0);
        if leaves.call(Call::ServerRunning, 0) != 0 {
            let gap = leaves.call(Call::Now, 0).wrapping_sub(self.last_handle);
            if self.last_handle > 0 && gap > 100 {
                leaves.call(Call::LogGap, gap);
            }
            leaves.call(Call::Watchdog, 0);
            for i in 0..80 {
                if leaves.call(Call::ServerRunning, 0) == 0 {
                    break;
                }
                leaves.call(Call::HandleClient, 0);
                if i & 7 == 7 {
                    leaves.call(Call::Watchdog, 0);
                }
                if i & 15 == 15 {
                    leaves.call(Call::Yield, 0);
                    if leaves.call(Call::Back, 0) != 0 {
                        self.exit_requested = 1;
                        break;
                    }
                }
            }
            self.last_handle = leaves.call(Call::Now, 0);
            let status = leaves.upload();
            let mut changed = false;
            if status.in_progress != 0 {
                if status.received != self.received
                    || status.total != self.total
                    || status.filename_changed != 0
                {
                    self.received = status.received;
                    self.total = status.total;
                    leaves.publish(self, COPY_UPLOAD);
                    changed = true;
                }
            } else if self.received != 0 || self.total != 0 {
                self.received = 0;
                self.total = 0;
                leaves.publish(self, CLEAR_UPLOAD);
                changed = true;
            }
            if status.completed_at != 0 && status.completed_at != self.processed_at {
                self.completed_at = status.completed_at;
                self.processed_at = status.completed_at;
                leaves.publish(self, COPY_COMPLETED);
                changed = true;
            }
            if self.completed_at > 0
                && leaves.call(Call::Now, 0).wrapping_sub(self.completed_at) >= 6000
            {
                self.completed_at = 0;
                leaves.publish(self, CLEAR_COMPLETED);
                changed = true;
            }
            if changed {
                self.paint(leaves);
            }
            leaves.call(Call::DropUpload, 0);
        }
        if self.exit_requested != 0 {
            leaves.call(Call::Finish, 0);
        }
    }
}
const _: () = {
    assert!(Phase::Wifi as u32 == 0);
    assert!(Phase::Starting as u32 == 1);
    assert!(Phase::Running as u32 == 2);
    assert!(Phase::Error as u32 == 3);
    assert!(Call::Now as u32 == 0);
    assert!(Call::Paint as u32 == 1);
    assert!(Call::WifiConnected as u32 == 2);
    assert!(Call::WifiPresent as u32 == 3);
    assert!(Call::WifiDisconnect as u32 == 4);
    assert!(Call::Delay as u32 == 5);
    assert!(Call::Restart as u32 == 6);
    assert!(Call::MdnsEnd as u32 == 7);
    assert!(Call::MdnsBegin as u32 == 8);
    assert!(Call::CacheRelease as u32 == 9);
    assert!(Call::ServerBegin as u32 == 10);
    assert!(Call::ServerRunning as u32 == 11);
    assert!(Call::HandleClient as u32 == 12);
    assert!(Call::Watchdog as u32 == 13);
    assert!(Call::Yield as u32 == 14);
    assert!(Call::Back as u32 == 15);
    assert!(Call::DropUpload as u32 == 16);
    assert!(Call::Finish as u32 == 17);
    assert!(Call::ConnectedDetails as u32 == 18);
    assert!(Call::SelectWifi as u32 == 19);
    assert!(Call::LogGap as u32 == 20);
    assert!(CLEAR_ALL == 1);
    assert!(COPY_UPLOAD == 2);
    assert!(CLEAR_UPLOAD == 4);
    assert!(COPY_COMPLETED == 8);
    assert!(CLEAR_COMPLETED == 16);
    assert!(core::mem::offset_of!(State, last_handle) == 0);
    assert!(core::mem::offset_of!(State, received) == 4);
    assert!(core::mem::offset_of!(State, total) == 8);
    assert!(core::mem::offset_of!(State, completed_at) == 12);
    assert!(core::mem::offset_of!(State, processed_at) == 16);
    assert!(core::mem::offset_of!(State, phase) == 20);
    assert!(core::mem::offset_of!(State, exit_requested) == 21);
    assert!(core::mem::offset_of!(State, reserved) == 22);
    assert!(core::mem::offset_of!(Upload, received) == 0);
    assert!(core::mem::offset_of!(Upload, total) == 4);
    assert!(core::mem::offset_of!(Upload, completed_at) == 8);
    assert!(core::mem::offset_of!(Upload, in_progress) == 12);
    assert!(core::mem::offset_of!(Upload, filename_changed) == 13);
    assert!(core::mem::offset_of!(Upload, reserved) == 14);
    assert!(core::mem::size_of::<State>() == 24);
    assert!(core::mem::align_of::<State>() == 4);
    assert!(core::mem::offset_of!(State, phase) == 20);
    assert!(core::mem::size_of::<Upload>() == 16);
    assert!(core::mem::align_of::<Upload>() == 4);
    assert!(core::mem::size_of::<Services>() == 4 * core::mem::size_of::<usize>());
};
