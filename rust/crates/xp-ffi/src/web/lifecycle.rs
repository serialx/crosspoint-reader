//! Session policy over synchronous network and scheduler leaves.
use super::{Output, Span};
use core::ffi::c_void;
use core::sync::atomic::{AtomicU32, Ordering};
#[repr(C)]
pub struct State {
    running: bool,
    ap_mode: bool,
    udp_active: bool,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Op {
    NetworkMode,
    StaConnected,
    ApStations,
    HttpPresent,
    WsPresent,
    WsActive,
    WsFile,
    AllocateHttp,
    WifiAwake,
    Register,
    HttpBegin,
    AllocateWs,
    RestartWs,
    WsBegin,
    UdpBegin,
    UdpStop,
    WsClose,
    WsDetach,
    AbortWs,
    Delay20,
    Delay10,
    HttpStop,
    HttpDelete,
    HttpPump,
    WsPump,
    Clock,
    UdpSize,
}
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Log {
    AlreadyRunning,
    NoNetwork,
    BeforeBegin,
    Network,
    CreateHttp,
    AfterAllocate,
    HttpOom,
    StartWs,
    WsStarted,
    Discovery,
    Started,
    Addresses,
    AfterBegin,
    Suspended,
    RestartOom,
    Resumed,
    AlreadyStopped,
    Stopping,
    BeforeStop,
    WsStopping,
    WsStopped,
    AfterStop,
    Deleted,
    AfterDelete,
    Final,
    NullHttp,
    Active,
}
#[repr(C)]
pub struct Services {
    operation: unsafe extern "C" fn(*mut c_void, Op) -> usize,
    diagnostic: unsafe extern "C" fn(*mut c_void, Log, u32),
    read_packet: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    hostname: unsafe extern "C" fn(*mut c_void) -> Span,
    reply: unsafe extern "C" fn(*mut c_void, Span, Span, Span),
}
struct Session<'a> {
    ctx: *mut c_void,
    services: &'a Services,
    state: *mut State,
}
// Matches the original function-static clock across server stop/start and instances.
static LAST_DEBUG: AtomicU32 = AtomicU32::new(0);
impl Session<'_> {
    fn op(&self, op: Op) -> usize {
        unsafe { (self.services.operation)(self.ctx, op) }
    }
    fn log(&self, event: Log) {
        unsafe { (self.services.diagnostic)(self.ctx, event, 0) }
    }
    fn running(&self) -> bool {
        unsafe { (*self.state).running }
    }
    fn udp(&self) -> bool {
        unsafe { (*self.state).udp_active }
    }
    fn set_running(&self, value: bool) {
        unsafe {
            (*self.state).running = value;
        }
    }
    fn set_udp(&self, value: bool) {
        unsafe {
            (*self.state).udp_active = value;
        }
    }
    fn begin(&self) {
        if self.running() {
            self.log(Log::AlreadyRunning);
            return;
        }
        let mode = self.op(Op::NetworkMode);
        let sta = mode & 1 != 0 && self.op(Op::StaConnected) != 0;
        let ap = mode & 2 != 0 && self.op(Op::ApStations) != 0;
        if !sta && !ap {
            unsafe {
                (self.services.diagnostic)(self.ctx, Log::NoNetwork, mode as u32);
            }
            return;
        }
        unsafe {
            (*self.state).ap_mode = ap;
        }
        self.log(Log::BeforeBegin);
        self.log(Log::Network);
        self.log(Log::CreateHttp);
        self.op(Op::AllocateHttp);
        self.op(Op::WifiAwake);
        self.log(Log::AfterAllocate);
        if self.op(Op::HttpPresent) == 0 {
            self.log(Log::HttpOom);
            return;
        }
        self.op(Op::Register);
        self.op(Op::HttpBegin);
        self.log(Log::StartWs);
        self.op(Op::AllocateWs);
        self.op(Op::WsBegin);
        self.log(Log::WsStarted);
        self.set_udp(self.op(Op::UdpBegin) != 0);
        self.log(Log::Discovery);
        self.set_running(true);
        self.log(Log::Started);
        self.log(Log::Addresses);
        self.log(Log::AfterBegin);
    }
    fn stop(&self) {
        if !self.running() || self.op(Op::HttpPresent) == 0 {
            self.log(Log::AlreadyStopped);
            return;
        }
        self.log(Log::Stopping);
        self.set_running(false);
        self.log(Log::BeforeStop);
        if self.op(Op::WsActive) != 0 && self.op(Op::WsFile) != 0 {
            self.op(Op::AbortWs);
        }
        if self.op(Op::WsPresent) != 0 {
            self.log(Log::WsStopping);
            self.op(Op::WsClose);
            self.op(Op::WsDetach);
            self.log(Log::WsStopped);
        }
        if self.udp() {
            self.op(Op::UdpStop);
            self.set_udp(false);
        }
        self.op(Op::Delay20);
        self.op(Op::HttpStop);
        self.log(Log::AfterStop);
        self.op(Op::Delay10);
        self.op(Op::HttpDelete);
        self.log(Log::Deleted);
        self.log(Log::AfterDelete);
        self.log(Log::Final);
    }
    fn suspend(&self) {
        if self.op(Op::WsPresent) != 0 && self.op(Op::WsActive) == 0 {
            self.op(Op::WsClose);
        }
        if self.udp() {
            self.op(Op::UdpStop);
        }
        self.log(Log::Suspended);
    }
    fn resume(&self) {
        if !self.running() {
            return;
        }
        if self.op(Op::WsPresent) == 0 {
            if self.op(Op::RestartWs) != 0 {
                self.op(Op::WsBegin);
            } else {
                self.log(Log::RestartOom);
            }
        }
        if self.udp() {
            self.set_udp(self.op(Op::UdpBegin) != 0);
        }
        self.log(Log::Resumed);
    }
    fn pump(&self, port: u16) {
        if !self.running() {
            return;
        }
        if self.op(Op::HttpPresent) == 0 {
            self.log(Log::NullHttp);
            return;
        }
        let now = self.op(Op::Clock) as u32;
        if now.wrapping_sub(LAST_DEBUG.load(Ordering::Relaxed)) > 10000 {
            self.log(Log::Active);
            LAST_DEBUG.store(self.op(Op::Clock) as u32, Ordering::Relaxed);
        }
        self.op(Op::HttpPump);
        if self.op(Op::WsPresent) != 0 {
            self.op(Op::WsPump);
        }
        if !self.udp() || self.op(Op::UdpSize) as i32 <= 0 {
            return;
        }
        let mut packet = [0u8; 16];
        let size = unsafe { (self.services.read_packet)(self.ctx, packet.as_mut_ptr(), 15) };
        if size <= 0 || size > 15 {
            return;
        }
        let packet = packet.get(..size as usize).unwrap_or(&[]);
        if packet.split(|&b| b == 0).next().unwrap_or(&[]) != b"hello" {
            return;
        }
        let host = unsafe { (self.services.hostname)(self.ctx).bytes() };
        let host = if host.is_empty() { b"crosspoint" } else { host };
        let mut suffix = [0u8; 7];
        let mut out = Output::new(&mut suffix);
        out.add(b");");
        out.number(port as usize);
        unsafe {
            (self.services.reply)(
                self.ctx,
                Span::new(b"crosspoint (on "),
                Span::new(host),
                Span::new(out.bytes()),
            );
        }
    }
}
/// # Safety
/// State and services are live for this synchronous call; callbacks may dispatch
/// routes. No Rust reference to the lifecycle state is retained across callbacks.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn xp_web_lifecycle(
    ctx: *mut c_void,
    services: *const Services,
    state: *mut State,
    action: u32,
    port: u16,
) {
    let Some(services) = (unsafe { services.as_ref() }) else {
        return;
    };
    if state.is_null() {
        return;
    }
    let session = Session {
        ctx,
        services,
        state,
    };
    match action {
        0 => session.begin(),
        1 => session.stop(),
        2 => session.pump(port),
        3 => session.suspend(),
        4 => session.resume(),
        _ => {}
    }
}
