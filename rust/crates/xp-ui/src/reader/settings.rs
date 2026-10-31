//! Reader orientation, automatic turns and live text settings.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    InitialOrientation = 0,
    Orientation = 1,
    AutoTurn = 2,
    Text = 3,
    LiveText = 4,
    Popup = 5,
    SizeResult = 6,
    SpacingResult = 7,
    AlignmentResult = 8,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    AppliedOrientation = 0,
    Spine = 1,
    CachedSpine = 2,
    CachedCount = 3,
    NextPage = 4,
    LastTurn = 5,
    TurnDuration = 6,
    AutoActive = 7,
    Orientation = 8,
    FontSize = 9,
    Spacing = 10,
    Alignment = 11,
    HasSection = 12,
    PageCount = 13,
    Page = 14,
    RateCount = 15,
    StatusHeight = 16,
    ProgressHeight = 17,
    SizeCount = 18,
    SpacingCount = 19,
    AlignmentCount = 20,
}

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    BaseOrientation = 0,
    Lock = 1,
    Unlock = 2,
    RememberOffset = 3,
    DropSection = 4,
    SaveSettings = 5,
    ApplyOrientation = 6,
    Clock = 7,
    Rate = 8,
    EnsureFonts = 9,
    DiscardOverlay = 10,
    Update = 11,
    PrepareSizes = 12,
    DropSizes = 13,
    ReserveLabels = 14,
    AddLabel = 15,
    SnapSize = 16,
    SizeAt = 17,
    ShowSizes = 18,
    ShowSpacing = 19,
    ShowAlignment = 20,
    PaintPopup = 21,
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
    fn op(&self, cmd: Command) -> u32 {
        self.0.op(cmd as u8)
    }
    fn cmd(&self, cmd: Command, arg: i32) -> u32 {
        self.0.command(cmd as u8, arg, 0)
    }
    fn lock(&self, owner: bool) -> Guard<'_> {
        self.cmd(Command::Lock, i32::from(owner));
        self.0.guard(Command::Unlock as u8)
    }
    fn cache(&self) {
        if self.f(Field::HasSection) != 0 {
            self.op(Command::RememberOffset);
            self.set(Field::CachedSpine, self.f(Field::Spine));
            self.set(Field::CachedCount, self.f(Field::PageCount));
            self.set(Field::NextPage, self.f(Field::Page));
        }
    }
    fn orientation(&self, orientation: u8) {
        let value = u32::from(orientation);
        if self.f(Field::Orientation) == value && self.f(Field::AppliedOrientation) == value {
            return;
        }
        let _lock = self.lock(true);
        self.cache();
        if self.f(Field::Orientation) != value {
            self.set(Field::Orientation, value);
            self.op(Command::SaveSettings);
        }
        self.op(Command::ApplyOrientation);
        self.set(Field::AppliedOrientation, value);
        self.op(Command::DropSection);
    }
    fn auto(&self, choice: u8) {
        if choice == 0 || u32::from(choice) >= self.f(Field::RateCount) {
            self.set(Field::AutoActive, 0);
            return;
        }
        self.set(Field::LastTurn, self.op(Command::Clock));
        let Some(duration) = 60000u32.checked_div(self.cmd(Command::Rate, i32::from(choice)))
        else {
            return;
        };
        self.set(Field::TurnDuration, duration);
        self.set(Field::AutoActive, 1);
        let height = self.f(Field::StatusHeight);
        if height == 0 || height == self.f(Field::ProgressHeight) {
            let _lock = self.lock(false);
            self.cache();
            self.op(Command::DropSection);
        }
    }
    fn text(&self) {
        {
            let _lock = self.lock(false);
            self.cache();
            self.op(Command::DropSection);
        }
        self.op(Command::SaveSettings);
        let _lock = self.lock(false);
        self.op(Command::EnsureFonts);
    }
    fn live(&self) {
        self.text();
        self.op(Command::DiscardOverlay);
        self.op(Command::Update);
    }
    fn popup(&self, row: i32) {
        match row {
            1 => {
                self.op(Command::PrepareSizes);
                let _sizes = self.0.guard(Command::DropSizes as u8);
                let count = self.f(Field::SizeCount);
                if count == 0 || count > i32::MAX as u32 {
                    return;
                }
                self.cmd(Command::ReserveLabels, count as i32);
                for i in 0..count {
                    self.cmd(Command::AddLabel, i as i32);
                }
                let current = self.op(Command::SnapSize);
                let mut index = 0;
                for i in 0..count {
                    if self.cmd(Command::SizeAt, i as i32) == current {
                        index = i;
                    }
                }
                self.cmd(Command::ShowSizes, index as i32);
            }
            2 => {
                let Some(index) = self
                    .f(Field::Spacing)
                    .checked_rem(self.f(Field::SpacingCount))
                else {
                    return;
                };
                self.cmd(Command::ShowSpacing, index as i32);
            }
            3 => {
                let Some(index) = self
                    .f(Field::Alignment)
                    .checked_rem(self.f(Field::AlignmentCount))
                else {
                    return;
                };
                self.cmd(Command::ShowAlignment, index as i32);
            }
            _ => return,
        }
        self.op(Command::PaintPopup);
    }
}
/// # Safety
/// Services obey the synchronous reader settings resource contract.
pub unsafe fn run(services: *const Services, operation: u8, argument: i32) {
    let Some(s) = services.as_ref() else {
        return;
    };
    let (Some(read), Some(write), Some(command)) = (s.read, s.write, s.command) else {
        return;
    };
    if s.context.is_null() {
        return;
    }
    let e = Engine(Core {
        context: s.context,
        read,
        write,
        command,
    });
    match operation {
        0 => {
            e.op(Command::BaseOrientation);
            e.set(Field::AppliedOrientation, e.f(Field::Orientation));
        }
        1 => e.orientation(argument as u8),
        2 => e.auto(argument as u8),
        3 => e.text(),
        4 => e.live(),
        5 => e.popup(argument),
        6 => {
            if argument < 0 || argument as u32 >= e.f(Field::SizeCount) {
                return;
            }
            e.set(Field::FontSize, e.cmd(Command::SizeAt, argument));
            e.live();
        }
        7 => {
            e.set(Field::Spacing, u32::from(argument as u8));
            e.live();
        }
        8 => {
            e.set(Field::Alignment, u32::from(argument as u8));
            e.live();
        }
        _ => {}
    }
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Services>() == 4 * size_of::<usize>());
    assert!(align_of::<Services>() == align_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 3 * size_of::<usize>());
    assert!(Operation::InitialOrientation as u8 == 0);
    assert!(Operation::Orientation as u8 == 1);
    assert!(Operation::AutoTurn as u8 == 2);
    assert!(Operation::Text as u8 == 3);
    assert!(Operation::LiveText as u8 == 4);
    assert!(Operation::Popup as u8 == 5);
    assert!(Operation::SizeResult as u8 == 6);
    assert!(Operation::SpacingResult as u8 == 7);
    assert!(Operation::AlignmentResult as u8 == 8);
    assert!(Field::AppliedOrientation as u8 == 0);
    assert!(Field::Spine as u8 == 1);
    assert!(Field::CachedSpine as u8 == 2);
    assert!(Field::CachedCount as u8 == 3);
    assert!(Field::NextPage as u8 == 4);
    assert!(Field::LastTurn as u8 == 5);
    assert!(Field::TurnDuration as u8 == 6);
    assert!(Field::AutoActive as u8 == 7);
    assert!(Field::Orientation as u8 == 8);
    assert!(Field::FontSize as u8 == 9);
    assert!(Field::Spacing as u8 == 10);
    assert!(Field::Alignment as u8 == 11);
    assert!(Field::HasSection as u8 == 12);
    assert!(Field::PageCount as u8 == 13);
    assert!(Field::Page as u8 == 14);
    assert!(Field::RateCount as u8 == 15);
    assert!(Field::StatusHeight as u8 == 16);
    assert!(Field::ProgressHeight as u8 == 17);
    assert!(Field::SizeCount as u8 == 18);
    assert!(Field::SpacingCount as u8 == 19);
    assert!(Field::AlignmentCount as u8 == 20);
    assert!(Command::BaseOrientation as u8 == 0);
    assert!(Command::Lock as u8 == 1);
    assert!(Command::Unlock as u8 == 2);
    assert!(Command::RememberOffset as u8 == 3);
    assert!(Command::DropSection as u8 == 4);
    assert!(Command::SaveSettings as u8 == 5);
    assert!(Command::ApplyOrientation as u8 == 6);
    assert!(Command::Clock as u8 == 7);
    assert!(Command::Rate as u8 == 8);
    assert!(Command::EnsureFonts as u8 == 9);
    assert!(Command::DiscardOverlay as u8 == 10);
    assert!(Command::Update as u8 == 11);
    assert!(Command::PrepareSizes as u8 == 12);
    assert!(Command::DropSizes as u8 == 13);
    assert!(Command::ReserveLabels as u8 == 14);
    assert!(Command::AddLabel as u8 == 15);
    assert!(Command::SnapSize as u8 == 16);
    assert!(Command::SizeAt as u8 == 17);
    assert!(Command::ShowSizes as u8 == 18);
    assert!(Command::ShowSpacing as u8 == 19);
    assert!(Command::ShowAlignment as u8 == 20);
    assert!(Command::PaintPopup as u8 == 21);
};

#[cfg(test)]
mod tests {
    use super::*;
    struct Fake {
        fields: [u32; 21],
        lock: bool,
        sizes: bool,
        calls: usize,
    }
    impl Fake {
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
        if field == Field::PageCount as u8 || field == Field::Page as u8 {
            assert_ne!(f.fields[Field::HasSection as usize], 0);
        }
        f.fields[field as usize]
    }
    unsafe extern "C" fn write(ctx: *mut c_void, field: u8, value: u32) {
        (&mut *ctx.cast::<Fake>()).fields[field as usize] = value;
    }
    unsafe extern "C" fn command(ctx: *mut c_void, command: u8, a: i32, _: i32) -> u32 {
        let f = &mut *ctx.cast::<Fake>();
        f.calls += 1;
        match command {
            x if x == Command::Lock as u8 => {
                assert!(!f.lock);
                f.lock = true;
            }
            x if x == Command::Unlock as u8 => {
                assert!(f.lock);
                f.lock = false;
            }
            x if x == Command::PrepareSizes as u8 => {
                assert!(!f.sizes);
                f.sizes = true;
            }
            x if x == Command::DropSizes as u8 => {
                assert!(f.sizes);
                f.sizes = false;
            }
            x if x == Command::PaintPopup as u8 => assert!(!f.sizes && !f.lock),
            x if x == Command::ReserveLabels as u8
                || x == Command::AddLabel as u8
                || x == Command::ShowSizes as u8 =>
            {
                assert!(f.sizes)
            }
            x if x == Command::Rate as u8 => return f.fields[Field::TurnDuration as usize],
            x if x == Command::SizeAt as u8 => {
                assert!(a >= 0 && (a as u32) < f.fields[Field::SizeCount as usize]);
                return 14;
            }
            x if x == Command::DropSection as u8 => {
                assert!(f.lock);
                f.fields[Field::HasSection as usize] = 0;
            }
            x if x == Command::RememberOffset as u8 || x == Command::EnsureFonts as u8 => {
                assert!(f.lock)
            }
            _ => {}
        }
        0
    }
    #[test]
    fn every_exit_balances_locks_and_font_vectors() {
        for op in 0..10 {
            for flags in 0..64 {
                for arg in [-1, 0, 1, 2, 3, 4, 5, 255, 256] {
                    let mut f = Fake {
                        fields: [0; 21],
                        lock: false,
                        sizes: false,
                        calls: 0,
                    };
                    f.fields[Field::HasSection as usize] = u32::from(flags & 1 != 0);
                    f.fields[Field::Orientation as usize] = u32::from(flags & 2 != 0);
                    f.fields[Field::AppliedOrientation as usize] = u32::from(flags & 4 != 0);
                    f.fields[Field::StatusHeight as usize] = if flags & 8 != 0 { 20 } else { 0 };
                    f.fields[Field::SizeCount as usize] = if flags & 16 != 0 { 3 } else { 0 };
                    f.fields[Field::RateCount as usize] = 5;
                    f.fields[Field::TurnDuration as usize] = if flags & 32 != 0 { 3 } else { 0 };
                    f.fields[Field::SpacingCount as usize] = 4;
                    f.fields[Field::AlignmentCount as usize] = 5;
                    let s = f.services();
                    unsafe {
                        run(&s, op, arg);
                    }
                    assert!(!f.lock && !f.sizes);
                }
            }
        }
    }
    #[test]
    fn null_and_incomplete_services_do_nothing() {
        unsafe {
            run(core::ptr::null(), 0, 0);
        }
        let mut f = Fake {
            fields: [0; 21],
            lock: false,
            sizes: false,
            calls: 0,
        };
        let mut s = f.services();
        s.command = None;
        unsafe {
            run(&s, 0, 0);
        }
        assert_eq!(f.calls, 0);
    }
}
