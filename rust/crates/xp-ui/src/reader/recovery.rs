//! Protected-open recovery and retained result policy.
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Load = 0,
    Begin = 1,
    LoadResult = 2,
    WifiResult = 3,
    RetryResult = 4,
    Render = 5,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    ErrorEmpty = 0,
    ErrorExpired = 1,
    ErrorUnverified = 2,
    Cancelled = 3,
    PopupActive = 4,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    ShowFailure = 0,
    Update = 1,
    Finish = 2,
    StartWifi = 3,
    LogOom = 4,
    Connected = 5,
    PaintSync = 6,
    Sync = 7,
    Disconnect = 8,
    Delay = 9,
    ShowRetry = 10,
    SavePath = 11,
    Restart = 12,
    Reopen = 13,
    Clear = 14,
    PaintFailure = 15,
    BaseRender = 16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub query: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Policy {
    context: *mut c_void,
    query: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    command: unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32,
}
impl Policy {
    fn q(&self, query: Query) -> bool {
        unsafe { (self.query)(self.context, query as u8) != 0 }
    }
    fn c(&self, command: Command, a: i32, b: i32) -> u32 {
        unsafe { (self.command)(self.context, command as u8, a, b) }
    }
    fn op(&self, command: Command) -> u32 {
        self.c(command, 0, 0)
    }
    fn begin(&self) {
        if self.op(Command::StartWifi) == 0 {
            self.op(Command::LogOom);
            self.op(Command::Finish);
        }
    }
    fn choose(&self, sync: bool, index: i32) {
        if sync && index == 0 {
            self.begin();
        } else {
            self.op(Command::Finish);
        }
    }
    fn wifi_result(&self) {
        if self.q(Query::Cancelled) || self.op(Command::Connected) == 0 {
            self.op(Command::Finish);
            return;
        }
        self.op(Command::PaintSync);
        let synced = self.c(Command::Sync, 5000, 0) != 0;
        self.c(Command::Disconnect, 0, 0);
        self.c(Command::Delay, 30, 0);
        if !synced {
            self.op(Command::ShowRetry);
            self.op(Command::Update);
            return;
        }
        self.op(Command::SavePath);
        self.op(Command::Restart);
        self.op(Command::Reopen);
    }
    fn load(&self) -> bool {
        if self.q(Query::ErrorEmpty) {
            return false;
        }
        let (message, offer) = if self.q(Query::ErrorExpired) {
            (1, 0)
        } else if self.q(Query::ErrorUnverified) {
            (2, 1)
        } else {
            (0, 0)
        };
        self.c(Command::ShowFailure, message, offer);
        self.op(Command::Update);
        true
    }
}
/// # Safety
/// Services and their context obey the synchronous recovery contract.
pub unsafe fn run(services: *const Services, operation: u8, a: i32, b: i32) -> u8 {
    let Some(services) = services.as_ref() else {
        return 0;
    };
    let (Some(query), Some(command)) = (services.query, services.command) else {
        return 0;
    };
    let p = Policy {
        context: services.context,
        query,
        command,
    };
    match operation {
        x if x == Operation::Load as u8 => return u8::from(p.load()),
        x if x == Operation::Begin as u8 => p.begin(),
        x if x == Operation::LoadResult as u8 => p.choose(b != 0, a),
        x if x == Operation::WifiResult as u8 => p.wifi_result(),
        x if x == Operation::RetryResult as u8 => p.choose(true, a),
        x if x == Operation::Render as u8 && p.q(Query::PopupActive) => {
            p.op(Command::Clear);
            p.op(Command::PaintFailure);
            return 1;
        }
        _ => {}
    }
    0
}
const _: () = {
    assert!(core::mem::size_of::<Services>() == 3 * core::mem::size_of::<usize>());
    assert!(core::mem::align_of::<Services>() == core::mem::align_of::<usize>());
    assert!(Operation::Load as u8 == 0);
    assert!(Operation::Begin as u8 == 1);
    assert!(Operation::LoadResult as u8 == 2);
    assert!(Operation::WifiResult as u8 == 3);
    assert!(Operation::RetryResult as u8 == 4);
    assert!(Operation::Render as u8 == 5);
    assert!(Query::ErrorEmpty as u8 == 0);
    assert!(Query::ErrorExpired as u8 == 1);
    assert!(Query::ErrorUnverified as u8 == 2);
    assert!(Query::Cancelled as u8 == 3);
    assert!(Query::PopupActive as u8 == 4);
    assert!(Command::ShowFailure as u8 == 0);
    assert!(Command::Update as u8 == 1);
    assert!(Command::Finish as u8 == 2);
    assert!(Command::StartWifi as u8 == 3);
    assert!(Command::LogOom as u8 == 4);
    assert!(Command::Connected as u8 == 5);
    assert!(Command::PaintSync as u8 == 6);
    assert!(Command::Sync as u8 == 7);
    assert!(Command::Disconnect as u8 == 8);
    assert!(Command::Delay as u8 == 9);
    assert!(Command::ShowRetry as u8 == 10);
    assert!(Command::SavePath as u8 == 11);
    assert!(Command::Restart as u8 == 12);
    assert!(Command::Reopen as u8 == 13);
    assert!(Command::Clear as u8 == 14);
    assert!(Command::PaintFailure as u8 == 15);
    assert!(Command::BaseRender as u8 == 16);
    assert!(core::mem::offset_of!(Services, context) == 0);
    assert!(core::mem::offset_of!(Services, query) == core::mem::size_of::<usize>());
    assert!(core::mem::offset_of!(Services, command) == 2 * core::mem::size_of::<usize>());
};
#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;
    #[derive(Default)]
    struct Fake {
        bits: u32,
        events: Vec<(u8, i32, i32)>,
    }
    unsafe extern "C" fn query(c: *mut c_void, q: u8) -> u32 {
        let f = &mut *c.cast::<Fake>();
        f.events.push((100 + q, 0, 0));
        (f.bits >> q) & 1
    }
    unsafe extern "C" fn command(c: *mut c_void, q: u8, a: i32, b: i32) -> u32 {
        let f = &mut *c.cast::<Fake>();
        f.events.push((q, a, b));
        match q {
            x if x == Command::StartWifi as u8 => (f.bits >> 5) & 1,
            x if x == Command::Connected as u8 => (f.bits >> 6) & 1,
            x if x == Command::Sync as u8 => (f.bits >> 7) & 1,
            _ => 0,
        }
    }
    #[test]
    fn callbacks_are_lazy_and_keep_sync_cleanup_order() {
        for bits in 0..256 {
            let mut f = Fake {
                bits,
                ..Fake::default()
            };
            let services = Services {
                context: (&mut f as *mut Fake).cast(),
                query: Some(query),
                command: Some(command),
            };
            unsafe { run(&services, Operation::WifiResult as u8, 0, 0) };
            let ids: Vec<_> = f.events.iter().map(|e| e.0).collect();
            if bits & (1 << 3) != 0 {
                assert_eq!(ids, [100 + Query::Cancelled as u8, Command::Finish as u8]);
            } else if bits & (1 << 6) != 0 {
                let sync_at = f
                    .events
                    .iter()
                    .position(|e| e.0 == Command::Sync as u8)
                    .unwrap();
                assert_eq!(
                    &f.events[sync_at..sync_at + 3],
                    &[
                        (Command::Sync as u8, 5000, 0),
                        (Command::Disconnect as u8, 0, 0),
                        (Command::Delay as u8, 30, 0)
                    ]
                );
                assert_eq!(
                    ids.last().copied(),
                    Some(if bits & (1 << 7) != 0 {
                        Command::Reopen as u8
                    } else {
                        Command::Update as u8
                    })
                );
            }
            for operation in 0..6 {
                for a in [-1, 0, 1, 2] {
                    for b in [0, 1] {
                        f.events.clear();
                        unsafe { run(&services, operation, a, b) };
                    }
                }
            }
        }
    }
    #[test]
    fn render_returns_before_native_base_delegation() {
        for active in [false, true] {
            let mut f = Fake {
                bits: u32::from(active) << 4,
                ..Fake::default()
            };
            let services = Services {
                context: (&mut f as *mut Fake).cast(),
                query: Some(query),
                command: Some(command),
            };
            assert_eq!(
                unsafe { run(&services, Operation::Render as u8, 0, 0) },
                u8::from(active)
            );
            let ids: Vec<_> = f.events.iter().map(|e| e.0).collect();
            if active {
                assert_eq!(
                    ids,
                    [
                        100 + Query::PopupActive as u8,
                        Command::Clear as u8,
                        Command::PaintFailure as u8
                    ]
                );
            } else {
                assert_eq!(ids, [100 + Query::PopupActive as u8]);
            }
            assert!(!ids.contains(&(Command::BaseRender as u8)));
        }
    }
    #[test]
    fn missing_services_and_unknown_operation_have_no_effects() {
        let mut f = Fake::default();
        let mut services = Services {
            context: (&mut f as *mut Fake).cast(),
            query: Some(query),
            command: Some(command),
        };
        unsafe {
            assert_eq!(run(core::ptr::null(), 0, 0, 0), 0);
            run(&services, 255, 0, 0);
        }
        services.query = None;
        unsafe {
            run(&services, 0, 0, 0);
        }
        services.query = Some(query);
        services.command = None;
        unsafe {
            run(&services, 0, 0, 0);
        }
        assert!(f.events.is_empty());
    }
}
