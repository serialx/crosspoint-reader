//! Chapter admission and bounded background-build policy.
use super::services::Core;
use super::thresholds::{
    BACKGROUND_BUILD_MIN_FREE_HEAP, BACKGROUND_BUILD_MIN_MAX_ALLOC, BUILD_ROOM_STEP_MS,
    BUILD_ROOM_WAIT_MS,
};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Room = 0,
    Heap = 1,
    Popup = 2,
    Buttons = 3,
    Background = 4,
    Wanted = 5,
    Delay = 6,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    RadioReleased = 0,
    HeapPaused = 1,
    PopupPending = 2,
    Refresh = 3,
    Overlay = 4,
    PendingTurn = 5,
    Spine = 6,
    FailedSpine = 7,
    NextPage = 8,
    BufferStale = 9,
    HasSection = 10,
    Building = 11,
    Complete = 12,
    Page = 13,
    Framebuffer = 14,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    ReleaseStrips = 0,
    RadioStatus = 1,
    RadioBefore = 2,
    Clock = 3,
    Sleep = 4,
    LogRoomDenied = 5,
    ReadFree = 6,
    ReadBlock = 7,
    LogReclaimed = 8,
    PaintIndex = 9,
    TryLock = 10,
    Unlock = 11,
    Button = 12,
    Raw = 13,
    LoanCount = 14,
    BuildSome = 15,
    LogBuildFailed = 16,
    DropSection = 17,
    Reposition = 18,
    Update = 19,
    RadioHeap = 20,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Engine(Core);
impl Engine {
    fn f(&self, field: Field) -> u32 {
        self.0.read(field as u8)
    }
    fn set(&self, field: Field, value: u32) {
        self.0.write(field as u8, value);
    }
    fn op(&self, command: Command) -> u32 {
        self.0.op(command as u8)
    }
    fn cmd(&self, command: Command, a: u32, b: u32) -> u32 {
        self.0.command(command as u8, a as i32, b as i32)
    }
    fn ready(&self, asked: &mut bool) -> bool {
        if self.f(Field::RadioReleased) == 0 && (!*asked || self.op(Command::RadioStatus) & 4 == 0)
        {
            self.set(Field::RadioReleased, self.op(Command::RadioBefore));
        }
        *asked = true;
        self.op(Command::RadioStatus) & 7 == 0
    }
    fn room(&self) -> bool {
        self.op(Command::ReleaseStrips);
        let mut asked = false;
        if self.ready(&mut asked) {
            return true;
        }
        let started = self.op(Command::Clock);
        loop {
            if self.op(Command::Clock).wrapping_sub(started) >= BUILD_ROOM_WAIT_MS {
                self.cmd(Command::LogRoomDenied, BUILD_ROOM_WAIT_MS, 0);
                return false;
            }
            self.cmd(Command::Sleep, BUILD_ROOM_STEP_MS, 0);
            if self.ready(&mut asked) {
                return true;
            }
        }
    }
    fn heap(&self) -> bool {
        let free = self.op(Command::ReadFree);
        let block = self.op(Command::ReadBlock);
        let mut paused =
            free < BACKGROUND_BUILD_MIN_FREE_HEAP || block < BACKGROUND_BUILD_MIN_MAX_ALLOC;
        self.set(Field::HeapPaused, u32::from(paused));
        if paused && self.op(Command::ReleaseStrips) != 0 {
            let reclaimed_free = self.op(Command::ReadFree);
            let reclaimed_block = self.op(Command::ReadBlock);
            paused = reclaimed_free < BACKGROUND_BUILD_MIN_FREE_HEAP
                || reclaimed_block < BACKGROUND_BUILD_MIN_MAX_ALLOC;
            self.set(Field::HeapPaused, u32::from(paused));
            self.cmd(Command::LogReclaimed, free, block);
        }
        !paused
    }
    fn popup(&self) {
        if self.f(Field::PopupPending) == 0 || self.f(Field::Framebuffer) == 0 {
            return;
        }
        self.op(Command::PaintIndex);
        self.set(Field::Refresh, 1);
        self.set(Field::PopupPending, 0);
    }
    fn buttons(&self) -> bool {
        (0..7).any(|button| self.cmd(Command::Button, button, 0) != 0)
    }
    fn wanted(&self) -> bool {
        self.f(Field::HasSection) != 0
            && self.f(Field::Building) != 0
            && self.op(Command::RadioHeap) == 0
            && self.f(Field::Spine) != self.f(Field::FailedSpine)
    }
    fn background(&self) {
        let owns = self.op(Command::TryLock) != 0;
        let _lock = self.0.guard(Command::Unlock as u8);
        if !owns || self.f(Field::Overlay) != 0 || !self.wanted() {
            return;
        }
        if self.op(Command::Raw) != 0
            || self.buttons()
            || self.f(Field::PendingTurn) != 0
            || !self.heap()
        {
            return;
        }
        let loans_before = self.op(Command::LoanCount);
        let built = self.op(Command::BuildSome) != 0;
        if self.op(Command::LoanCount) != loans_before {
            self.set(Field::BufferStale, 1);
            self.op(Command::Update);
        }
        if !built {
            self.op(Command::LogBuildFailed);
            self.set(Field::FailedSpine, self.f(Field::Spine));
            self.set(Field::NextPage, self.f(Field::Page));
            self.op(Command::DropSection);
            self.op(Command::Update);
        } else if self.f(Field::Complete) != 0 && self.op(Command::Reposition) != 0 {
            self.op(Command::Update);
        }
    }
}
/// # Safety
/// All services satisfy the synchronous native resource contract.
pub unsafe fn run(services: *const Services, operation: u8) -> u8 {
    let Some(s) = services.as_ref() else {
        return 0;
    };
    let (Some(read), Some(write), Some(command)) = (s.read, s.write, s.command) else {
        return 0;
    };
    if s.context.is_null() {
        return 0;
    }
    let e = Engine(Core {
        context: s.context,
        read,
        write,
        command,
    });
    u8::from(match operation {
        0 => e.room(),
        1 => e.heap(),
        2 => {
            e.popup();
            false
        }
        3 => e.buttons(),
        4 => {
            e.background();
            false
        }
        5 => e.wanted(),
        6 => e.f(Field::Overlay) == 0 && e.f(Field::HeapPaused) == 0 && e.wanted(),
        _ => false,
    })
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 4 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 3 * size_of::<usize>());
    assert!(Operation::Room as u8 == 0);
    assert!(Operation::Heap as u8 == 1);
    assert!(Operation::Popup as u8 == 2);
    assert!(Operation::Buttons as u8 == 3);
    assert!(Operation::Background as u8 == 4);
    assert!(Operation::Wanted as u8 == 5);
    assert!(Operation::Delay as u8 == 6);
    assert!(Field::RadioReleased as u8 == 0);
    assert!(Field::HeapPaused as u8 == 1);
    assert!(Field::PopupPending as u8 == 2);
    assert!(Field::Refresh as u8 == 3);
    assert!(Field::Overlay as u8 == 4);
    assert!(Field::PendingTurn as u8 == 5);
    assert!(Field::Spine as u8 == 6);
    assert!(Field::FailedSpine as u8 == 7);
    assert!(Field::NextPage as u8 == 8);
    assert!(Field::BufferStale as u8 == 9);
    assert!(Field::HasSection as u8 == 10);
    assert!(Field::Building as u8 == 11);
    assert!(Field::Complete as u8 == 12);
    assert!(Field::Page as u8 == 13);
    assert!(Field::Framebuffer as u8 == 14);
    assert!(Command::ReleaseStrips as u8 == 0);
    assert!(Command::RadioStatus as u8 == 1);
    assert!(Command::RadioBefore as u8 == 2);
    assert!(Command::Clock as u8 == 3);
    assert!(Command::Sleep as u8 == 4);
    assert!(Command::LogRoomDenied as u8 == 5);
    assert!(Command::ReadFree as u8 == 6);
    assert!(Command::ReadBlock as u8 == 7);
    assert!(Command::LogReclaimed as u8 == 8);
    assert!(Command::PaintIndex as u8 == 9);
    assert!(Command::TryLock as u8 == 10);
    assert!(Command::Unlock as u8 == 11);
    assert!(Command::Button as u8 == 12);
    assert!(Command::Raw as u8 == 13);
    assert!(Command::LoanCount as u8 == 14);
    assert!(Command::BuildSome as u8 == 15);
    assert!(Command::LogBuildFailed as u8 == 16);
    assert!(Command::DropSection as u8 == 17);
    assert!(Command::Reposition as u8 == 18);
    assert!(Command::Update as u8 == 19);
    assert!(Command::RadioHeap as u8 == 20);
};

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use std::vec::Vec;
    struct Fake {
        fields: [u32; 15],
        commands: [u32; 21],
        events: Vec<(u8, u32, u32)>,
        lock: bool,
        clock: u32,
        radio_cost: u32,
    }
    impl Fake {
        fn new() -> Self {
            Self {
                fields: [0; 15],
                commands: [0; 21],
                events: Vec::new(),
                lock: false,
                clock: 0,
                radio_cost: 0,
            }
        }
        fn services(&mut self) -> Services {
            Services {
                context: core::ptr::from_mut(self).cast(),
                read: Some(read),
                write: Some(write),
                command: Some(command),
            }
        }
    }
    unsafe extern "C" fn read(ctx: *mut c_void, field: u8) -> u32 {
        let f = &mut *ctx.cast::<Fake>();
        if field == Field::Building as u8
            || field == Field::Complete as u8
            || field == Field::Page as u8
        {
            assert_ne!(f.fields[Field::HasSection as usize], 0);
        }
        f.fields[field as usize]
    }
    unsafe extern "C" fn write(ctx: *mut c_void, field: u8, value: u32) {
        let f = &mut *ctx.cast::<Fake>();
        f.fields[field as usize] = value;
    }
    unsafe extern "C" fn command(ctx: *mut c_void, command: u8, a: i32, b: i32) -> u32 {
        let f = &mut *ctx.cast::<Fake>();
        f.events.push((command, a as u32, b as u32));
        match command {
            x if x == Command::TryLock as u8 => {
                assert!(!f.lock);
                f.lock = true;
            }
            x if x == Command::Unlock as u8 => {
                assert!(f.lock);
                f.lock = false;
            }
            x if x == Command::Clock as u8 => return f.clock,
            x if x == Command::Sleep as u8 => f.clock = f.clock.wrapping_add(a as u32),
            x if x == Command::RadioBefore as u8 => f.clock = f.clock.wrapping_add(f.radio_cost),
            x if x == Command::DropSection as u8 => f.fields[Field::HasSection as usize] = 0,
            x if x == Command::BuildSome as u8 => {
                assert!(f.lock);
                assert_ne!(f.fields[Field::HasSection as usize], 0);
                f.commands[Command::LoanCount as usize] += 1;
            }
            _ => {}
        }
        f.commands[command as usize]
    }
    #[test]
    fn slow_radio_timeout_preserves_delay_count_and_clock_rollover() {
        for start in [0, u32::MAX - 125] {
            let mut f = Fake::new();
            f.clock = start;
            f.radio_cost = 3000;
            f.commands[Command::RadioStatus as usize] = 1;
            let s = f.services();
            assert_eq!(unsafe { run(&s, Operation::Room as u8) }, 0);
            let calls = |cmd: Command| f.events.iter().filter(|e| e.0 == cmd as u8).count();
            assert_eq!(calls(Command::RadioBefore), 3);
            assert_eq!(calls(Command::RadioStatus), 5);
            assert_eq!(calls(Command::Clock), 4);
            assert_eq!(calls(Command::Sleep), 2);
            assert_eq!(calls(Command::LogRoomDenied), 1);
            assert!(f
                .events
                .contains(&(Command::LogRoomDenied as u8, BUILD_ROOM_WAIT_MS, 0)));
            assert!(f
                .events
                .iter()
                .filter(|e| e.0 == Command::Sleep as u8)
                .all(|e| e.1 == 50 && e.2 == 0));
            assert_eq!(f.clock, start.wrapping_add(9100));
        }
    }
    #[test]
    fn all_background_exits_release_the_constructed_lock() {
        for mask in 0..8192 {
            let mut f = Fake::new();
            for (bit, field) in [
                Field::Overlay,
                Field::PendingTurn,
                Field::HasSection,
                Field::Building,
                Field::Complete,
            ]
            .into_iter()
            .enumerate()
            {
                f.fields[field as usize] = u32::from(mask & (1 << bit) != 0);
            }
            f.fields[Field::Spine as usize] = 3;
            f.fields[Field::FailedSpine as usize] = if mask & 32 != 0 { 3 } else { u32::MAX };
            for (bit, cmd) in [
                Command::TryLock,
                Command::RadioHeap,
                Command::Raw,
                Command::Button,
                Command::BuildSome,
                Command::Reposition,
            ]
            .into_iter()
            .enumerate()
            {
                f.commands[cmd as usize] = u32::from(mask & (1 << (bit + 6)) != 0);
            }
            f.commands[Command::ReadFree as usize] = if mask & 4096 != 0 { 32767 } else { 32768 };
            f.commands[Command::ReadBlock as usize] = 16384;
            let s = f.services();
            unsafe {
                run(&s, Operation::Background as u8);
            }
            assert!(!f.lock);
            assert_eq!(f.events.first().unwrap().0, Command::TryLock as u8);
            assert_eq!(f.events.last().unwrap().0, Command::Unlock as u8);
        }
    }
    #[test]
    fn invalid_services_and_operations_have_no_effects() {
        assert_eq!(unsafe { run(core::ptr::null(), 0) }, 0);
        let mut f = Fake::new();
        let mut s = f.services();
        assert_eq!(unsafe { run(&s, 255) }, 0);
        s.command = None;
        assert_eq!(unsafe { run(&s, 0) }, 0);
        assert!(f.events.is_empty());
    }
}
