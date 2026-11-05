//! Web transfer lifecycle and connection-health policy.
use crate::model::{Input, BACK};
use core::{
    ffi::c_void,
    sync::atomic::{AtomicU32, Ordering},
};
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct State {
    pub last_handle: u32,
    pub first_disconnect: u32,
    pub disconnects: i32,
    pub bars: i32,
    pub phase: u8,
    pub mode: u8,
    pub is_ap: u8,
    pub start_in_join: u8,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Phase {
    Modes = 0,
    Wifi = 1,
    ApStarting = 2,
    Running = 3,
    Stopping = 4,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Call {
    Now = 0,
    Paint = 1,
    EnterResources = 2,
    ExitLogStart = 3,
    ExitLogEnd = 4,
    DnsStop = 5,
    MdnsEnd = 6,
    PluginsRefresh = 7,
    WifiPresent = 8,
    WifiDisconnect = 9,
    ApDisconnect = 10,
    Delay = 11,
    Restart = 12,
    RestartJoin = 13,
    LogMode = 14,
    Home = 15,
    SelectModes = 16,
    SelectCalibre = 17,
    WifiSta = 18,
    SelectWifi = 19,
    ApStartLog = 20,
    WifiAp = 21,
    ApBegin = 22,
    ApDetails = 23,
    MdnsRestart = 24,
    DnsStart = 25,
    ApReadyLog = 26,
    ServerBegin = 27,
    ServerRunning = 28,
    ServerDelete = 29,
    ServerReadyLog = 30,
    ServerErrorLog = 31,
    WifiRssi = 32,
    PluginDrain = 33,
    DnsNext = 34,
    WifiStatus = 35,
    InputUpdate = 36,
    ExitInput = 37,
    Watchdog = 38,
    Yield = 39,
    HandleClient = 40,
    ReadCancel = 41,
    LogGap = 42,
    LogWifiLost = 43,
    LogAbandon = 44,
    LogRecovered = 45,
    LogWeak = 46,
    UsbActivity = 47,
    LogWifiResult = 48,
    UsbSupported = 49,
    ApErrorLog = 50,
    ClearConnection = 51,
}
#[repr(C)]
pub struct Services {
    pub context: *mut c_void,
    pub call: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    pub publish: Option<unsafe extern "C" fn(*mut c_void, *const State)>,
}
impl Services {
    fn call(&mut self, op: Call, value: u32) -> u32 {
        self.call
            .map_or(0, |f| unsafe { f(self.context, op as u32, value) })
    }
    fn publish(&mut self, state: &State) {
        if let Some(f) = self.publish {
            unsafe { f(self.context, state) }
        }
    }
}
// Match the original function-local static across activity instances.
static LAST_WIFI_CHECK: AtomicU32 = AtomicU32::new(0);
pub fn cancel(latch: &mut u8, input: Input, home: bool) -> bool {
    *latch |= u8::from((input.pressed | input.held) & BACK != 0 || home);
    *latch != 0
}
impl State {
    fn paint(&self, c: &mut Services) {
        c.publish(self);
        c.call(Call::Paint, 0);
    }
    pub fn run(&mut self, c: &mut Services, event: u8, value: u8) {
        if self.phase > Phase::Stopping as u8 {
            return;
        }
        match event {
            0 => {
                c.call(Call::EnterResources, 0);
                self.phase = Phase::Modes as u8;
                self.mode = 0;
                self.is_ap = 0;
                c.call(Call::ClearConnection, 0);
                self.last_handle = 0;
                self.paint(c);
                if self.start_in_join != 0 {
                    self.mode_selected(c, 0);
                } else {
                    c.call(Call::SelectModes, 1);
                }
            }
            1 => {
                c.call(Call::ExitLogStart, 0);
                self.phase = Phase::Stopping as u8;
                c.publish(self);
                c.call(Call::DnsStop, 0);
                c.call(Call::MdnsEnd, 0);
                c.call(Call::PluginsRefresh, 0);
                if c.call(Call::WifiPresent, 0) != 0 {
                    c.call(
                        if self.is_ap != 0 {
                            Call::ApDisconnect
                        } else {
                            Call::WifiDisconnect
                        },
                        0,
                    );
                    c.call(Call::Delay, 30);
                    c.call(Call::Restart, 0);
                }
                c.call(Call::ExitLogEnd, 0);
            }
            2 => self.tick(c),
            3 => {
                if value == 255 {
                    c.call(Call::Home, 0);
                } else if value <= 3 {
                    self.mode_selected(c, value);
                }
            }
            4 => {
                c.call(Call::LogWifiResult, u32::from(value != 0));
                if value != 0 {
                    self.is_ap = 0;
                    c.publish(self);
                    c.call(Call::MdnsRestart, 0);
                    self.start(c);
                } else {
                    self.phase = Phase::Modes as u8;
                    c.publish(self);
                    c.call(Call::SelectModes, 0);
                }
            }
            5 => {
                self.phase = Phase::Modes as u8;
                c.publish(self);
                c.call(Call::SelectModes, 0);
            }
            _ => {}
        }
        c.publish(self);
    }
    fn mode_selected(&mut self, c: &mut Services, mode: u8) {
        if mode == 0 && self.start_in_join == 0 {
            c.call(Call::RestartJoin, 0);
        }
        c.call(Call::LogMode, u32::from(mode));
        if mode == 3 && c.call(Call::UsbSupported, 0) != 0 {
            c.call(Call::UsbActivity, 0);
            return;
        }
        self.mode = mode;
        self.is_ap = u8::from(mode == 2);
        c.publish(self);
        match mode {
            1 => {
                c.call(Call::SelectCalibre, 0);
            }
            0 => {
                c.call(Call::WifiSta, 0);
                self.phase = Phase::Wifi as u8;
                c.publish(self);
                c.call(Call::SelectWifi, 0);
            }
            _ => {
                self.phase = Phase::ApStarting as u8;
                self.paint(c);
                self.start_ap(c);
            }
        }
    }
    pub fn start_ap(&mut self, c: &mut Services) {
        c.call(Call::ApStartLog, 0);
        c.call(Call::WifiAp, 0);
        c.call(Call::Delay, 100);
        if c.call(Call::ApBegin, 0) == 0 {
            c.call(Call::ApErrorLog, 0);
            c.call(Call::Home, 0);
            return;
        }
        c.call(Call::Delay, 100);
        c.call(Call::ApDetails, 0);
        c.call(Call::MdnsRestart, 0);
        c.call(Call::DnsStart, 0);
        c.call(Call::ApReadyLog, 0);
        self.start(c);
    }
    pub fn start(&mut self, c: &mut Services) {
        c.call(Call::ServerBegin, 0);
        if c.call(Call::ServerRunning, 0) != 0 {
            self.phase = Phase::Running as u8;
            c.publish(self);
            c.call(Call::ServerReadyLog, 0);
            self.bars = if self.is_ap != 0 {
                0
            } else {
                bars_for_rssi(c.call(Call::WifiRssi, 0) as i32, 0)
            };
            c.publish(self);
            if self.is_ap == 0 {
                c.call(Call::PluginDrain, 0);
            }
            self.paint(c);
        } else {
            c.call(Call::ServerErrorLog, 0);
            c.call(Call::ServerDelete, 0);
            c.call(Call::Home, 0);
        }
    }
    fn tick(&mut self, c: &mut Services) {
        if self.phase != Phase::Running as u8 {
            return;
        }
        if self.is_ap != 0 {
            c.call(Call::DnsNext, 0);
        }
        if self.is_ap == 0
            && c.call(Call::ServerRunning, 0) != 0
            && c.call(Call::Now, 0)
                .wrapping_sub(LAST_WIFI_CHECK.load(Ordering::Relaxed))
                > 2000
        {
            LAST_WIFI_CHECK.store(c.call(Call::Now, 0), Ordering::Relaxed);
            let connected = c.call(Call::WifiStatus, 0) != 0;
            let mut repaint = false;
            if !connected {
                if self.disconnects == 0 {
                    self.first_disconnect = c.call(Call::Now, 0);
                    repaint = true;
                }
                self.disconnects = self.disconnects.wrapping_add(1);
                c.publish(self);
                let age = c.call(Call::Now, 0).wrapping_sub(self.first_disconnect);
                c.call(Call::LogWifiLost, age);
                if c.call(Call::Now, 0).wrapping_sub(self.first_disconnect) > 300000 {
                    c.call(Call::LogAbandon, 0);
                    self.phase = Phase::Stopping as u8;
                    c.publish(self);
                    c.call(Call::Home, 0);
                    return;
                }
            } else {
                if self.disconnects > 0 {
                    c.publish(self);
                    let age = c.call(Call::Now, 0).wrapping_sub(self.first_disconnect);
                    c.call(Call::LogRecovered, age);
                    repaint = true;
                }
                self.disconnects = 0;
                self.first_disconnect = 0;
                let rssi = c.call(Call::WifiRssi, 0) as i32;
                if rssi < -75 {
                    c.call(Call::LogWeak, rssi as u32);
                }
                let bars = bars_for_rssi(rssi, self.bars);
                if bars != self.bars {
                    self.bars = bars;
                    repaint = true;
                }
            }
            if repaint {
                self.paint(c);
            }
        }
        if c.call(Call::ServerRunning, 0) != 0 {
            let gap = c.call(Call::Now, 0).wrapping_sub(self.last_handle);
            if self.last_handle > 0 && gap > 100 {
                c.call(Call::LogGap, gap);
            }
            c.call(Call::Watchdog, 0);
            for i in 0..500 {
                if c.call(Call::ServerRunning, 0) == 0 {
                    break;
                }
                c.call(Call::HandleClient, 0);
                if c.call(Call::ReadCancel, 0) != 0 {
                    c.call(Call::Home, 0);
                    return;
                }
                if i & 31 == 31 {
                    c.call(Call::Watchdog, 0);
                }
                if i & 63 == 63 {
                    c.call(Call::Yield, 0);
                    c.call(Call::InputUpdate, 0);
                    if c.call(Call::ExitInput, 0) != 0 {
                        c.call(Call::Home, 0);
                        return;
                    }
                }
            }
            self.last_handle = c.call(Call::Now, 0);
        }
        if c.call(Call::ExitInput, 0) != 0 {
            c.call(Call::Home, 0);
        }
    }
}
pub fn bars_for_rssi(rssi: i32, current: i32) -> i32 {
    const RISE: [i32; 4] = [-85, -75, -65, -55];
    const FALL: [i32; 4] = [-88, -78, -68, -58];
    let mut bars = current.clamp(0, 4) as usize;
    while let Some(threshold) = RISE.get(bars) {
        if rssi < *threshold {
            break;
        }
        bars += 1;
    }
    while bars > 0 {
        let Some(threshold) = FALL.get(bars - 1) else {
            break;
        };
        if rssi >= *threshold {
            break;
        }
        bars -= 1;
    }
    bars as i32
}
const _: () = {
    assert!(core::mem::size_of::<State>() == 20);
    assert!(core::mem::align_of::<State>() == 4);
};

const _: () = {
    assert!(Phase::Modes as u32 == 0);
    assert!(Phase::Wifi as u32 == 1);
    assert!(Phase::ApStarting as u32 == 2);
    assert!(Phase::Running as u32 == 3);
    assert!(Phase::Stopping as u32 == 4);
    assert!(Call::Now as u32 == 0);
    assert!(Call::Paint as u32 == 1);
    assert!(Call::EnterResources as u32 == 2);
    assert!(Call::ExitLogStart as u32 == 3);
    assert!(Call::ExitLogEnd as u32 == 4);
    assert!(Call::DnsStop as u32 == 5);
    assert!(Call::MdnsEnd as u32 == 6);
    assert!(Call::PluginsRefresh as u32 == 7);
    assert!(Call::WifiPresent as u32 == 8);
    assert!(Call::WifiDisconnect as u32 == 9);
    assert!(Call::ApDisconnect as u32 == 10);
    assert!(Call::Delay as u32 == 11);
    assert!(Call::Restart as u32 == 12);
    assert!(Call::RestartJoin as u32 == 13);
    assert!(Call::LogMode as u32 == 14);
    assert!(Call::Home as u32 == 15);
    assert!(Call::SelectModes as u32 == 16);
    assert!(Call::SelectCalibre as u32 == 17);
    assert!(Call::WifiSta as u32 == 18);
    assert!(Call::SelectWifi as u32 == 19);
    assert!(Call::ApStartLog as u32 == 20);
    assert!(Call::WifiAp as u32 == 21);
    assert!(Call::ApBegin as u32 == 22);
    assert!(Call::ApDetails as u32 == 23);
    assert!(Call::MdnsRestart as u32 == 24);
    assert!(Call::DnsStart as u32 == 25);
    assert!(Call::ApReadyLog as u32 == 26);
    assert!(Call::ServerBegin as u32 == 27);
    assert!(Call::ServerRunning as u32 == 28);
    assert!(Call::ServerDelete as u32 == 29);
    assert!(Call::ServerReadyLog as u32 == 30);
    assert!(Call::ServerErrorLog as u32 == 31);
    assert!(Call::WifiRssi as u32 == 32);
    assert!(Call::PluginDrain as u32 == 33);
    assert!(Call::DnsNext as u32 == 34);
    assert!(Call::WifiStatus as u32 == 35);
    assert!(Call::InputUpdate as u32 == 36);
    assert!(Call::ExitInput as u32 == 37);
    assert!(Call::Watchdog as u32 == 38);
    assert!(Call::Yield as u32 == 39);
    assert!(Call::HandleClient as u32 == 40);
    assert!(Call::ReadCancel as u32 == 41);
    assert!(Call::LogGap as u32 == 42);
    assert!(Call::LogWifiLost as u32 == 43);
    assert!(Call::LogAbandon as u32 == 44);
    assert!(Call::LogRecovered as u32 == 45);
    assert!(Call::LogWeak as u32 == 46);
    assert!(Call::UsbActivity as u32 == 47);
    assert!(Call::LogWifiResult as u32 == 48);
    assert!(Call::UsbSupported as u32 == 49);
    assert!(Call::ApErrorLog as u32 == 50);
    assert!(Call::ClearConnection as u32 == 51);
    assert!(core::mem::offset_of!(State, last_handle) == 0);
    assert!(core::mem::offset_of!(State, first_disconnect) == 4);
    assert!(core::mem::offset_of!(State, disconnects) == 8);
    assert!(core::mem::offset_of!(State, bars) == 12);
    assert!(core::mem::offset_of!(State, phase) == 16);
    assert!(core::mem::offset_of!(State, mode) == 17);
    assert!(core::mem::offset_of!(State, is_ap) == 18);
    assert!(core::mem::offset_of!(State, start_in_join) == 19);
};
