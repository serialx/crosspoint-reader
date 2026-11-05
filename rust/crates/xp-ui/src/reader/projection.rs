//! Reader projections, remote turns and final book state.
use super::services::{Core, Guard};
use core::ffi::c_void;
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Operation {
    Chapter = 0,
    BookPercent = 1,
    Toc = 2,
    Screenshot = 3,
    BasisPoints = 4,
    Position = 5,
    ConfirmThreshold = 6,
    Remote = 7,
    AtEnd = 8,
    ReturnEnd = 9,
    RenderedEnd = 10,
    InputOver = 11,
    Turn = 12,
    Skip = 13,
    ApplyMove = 14,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Field {
    HasBook = 0,
    HasSection = 1,
    Spine = 2,
    NextPage = 3,
    CachedCount = 4,
    Page = 5,
    PageCount = 6,
    Building = 7,
    AutoTurn = 8,
    PendingSyncError = 9,
    PendingPage = 10,
    LastTurn = 11,
    OverlayOpen = 12,
    HasTouch = 13,
    LongPressFunction = 14,
    OutputPage = 15,
    OutputTotal = 16,
    OutputSpine = 17,
    OutputPercent = 18,
    OutputParagraph = 19,
    OutputHasParagraph = 20,
    OutputHasOffset = 21,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Command {
    BookSize = 0,
    SpineCount = 1,
    Estimate = 2,
    TocCurrent = 3,
    TocCached = 4,
    Title = 5,
    Progress = 6,
    Paragraph = 7,
    Offset = 8,
    InitPosition = 9,
    Credentials = 10,
    ToolbarMenu = 11,
    BaseInput = 12,
    StartSelection = 13,
    OpenToolbar = 14,
    OpenMenu = 15,
    SaveErrorPopup = 16,
    Lock = 17,
    Unlock = 18,
    ClearDeferred = 19,
    Acknowledge = 20,
    DropSection = 21,
    Clock = 22,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u8, u32)>,
    pub command: Option<unsafe extern "C" fn(*mut c_void, u8, i32, i32) -> u32>,
}
struct Policy(Core);
// ESP32-C3 ROM __fixsfsi saturates by sign, including signed NaNs.
fn progress_integer(value: f32) -> i32 {
    if value.is_nan() {
        if value.is_sign_negative() {
            i32::MIN
        } else {
            i32::MAX
        }
    } else {
        value as i32
    }
}
impl Policy {
    fn f(&self, f: Field) -> u32 {
        self.0.read(f as u8)
    }
    fn set(&self, f: Field, v: u32) {
        self.0.write(f as u8, v);
    }
    fn c(&self, c: Command, a: i32, b: i32) -> u32 {
        self.0.command(c as u8, a, b)
    }
    fn op(&self, c: Command) -> u32 {
        self.0.op(c as u8)
    }
    fn lock(&self) -> Guard<'_> {
        self.op(Command::Lock);
        self.0.guard(Command::Unlock as u8)
    }
    fn at_end(&self) -> bool {
        self.f(Field::HasBook) != 0
            && self.f(Field::Spine) as i32 >= self.op(Command::SpineCount) as i32
    }
    fn progress(&self, page: i32, total: i32) -> f32 {
        let fraction = page as f32 / total as f32;
        f32::from_bits(self.c(
            Command::Progress,
            self.f(Field::Spine) as i32,
            fraction.to_bits() as i32,
        ))
    }
    fn apply(&self, kind: u8, value: i32, last: bool, record: bool) -> u32 {
        match kind {
            1 => self.set(Field::Page, value as u32),
            2 => {
                let _lock = self.lock();
                self.set(Field::NextPage, 0);
                if last {
                    self.set(Field::PendingPage, u16::MAX as u32);
                }
                self.set(Field::Spine, value as u32);
                self.op(Command::DropSection);
                if record {
                    self.set(Field::LastTurn, self.op(Command::Clock));
                }
                return 1;
            }
            3 => self.set(Field::Spine, value as u32),
            _ => return 0,
        }
        if record {
            self.set(Field::LastTurn, self.op(Command::Clock));
        }
        1
    }
    fn run(&self, operation: u8, a: i32, b: i32, c: u8) -> u32 {
        use Command as C;
        use Field as F;
        use Operation as O;
        match operation {
            x if x == O::Chapter as u8 => {
                let (page, total) = if self.f(F::HasSection) != 0 {
                    (self.f(F::Page), self.op(C::Estimate))
                } else {
                    (self.f(F::NextPage), self.f(F::CachedCount))
                };
                self.set(F::OutputPage, page);
                self.set(F::OutputTotal, total);
            }
            x if x == O::BookPercent as u8 => {
                if self.f(F::HasBook) == 0 || self.op(C::BookSize) == 0 || b <= 0 {
                    return 0;
                }
                return progress_integer(self.progress(a, b).clamp(0.0, 1.0) * 100.0 + 0.5) as u32;
            }
            x if x == O::Toc as u8 => {
                if self.f(F::HasBook) == 0 {
                    return 0;
                }
                let index = if self.f(F::HasSection) != 0 {
                    self.op(C::TocCurrent)
                } else {
                    self.op(C::TocCached)
                };
                return (index as i32).max(0) as u32;
            }
            x if x == O::Screenshot as u8 => {
                if self.f(F::HasBook) != 0 {
                    self.op(C::Title);
                    self.set(F::OutputSpine, self.f(F::Spine));
                }
                if self.f(F::HasSection) != 0 {
                    self.set(F::OutputPage, self.f(F::Page).wrapping_add(1));
                    let total = self.op(C::Estimate) as i32;
                    self.set(F::OutputTotal, total as u32);
                    if self.f(F::HasBook) != 0 && self.op(C::BookSize) > 0 && total > 0 {
                        let percent = progress_integer(
                            self.progress(self.f(F::Page) as i32, total) * 100.0 + 0.5,
                        )
                        .clamp(0, 100);
                        self.set(F::OutputPercent, percent as u32);
                    }
                }
            }
            x if x == O::BasisPoints as u8 => {
                if self.at_end() {
                    return 10000;
                }
                if self.f(F::HasBook) == 0
                    || self.f(F::HasSection) == 0
                    || self.op(C::BookSize) == 0
                {
                    return u32::MAX;
                }
                let total = self.op(C::Estimate) as i32;
                if total <= 0 {
                    return u32::MAX;
                }
                return progress_integer(
                    self.progress(self.f(F::Page) as i32, total) * 10000.0 + 0.5,
                )
                .clamp(0, 10000) as u32;
            }
            x if x == O::Position as u8 => {
                let page = if self.f(F::HasSection) != 0 {
                    self.f(F::Page)
                } else {
                    self.f(F::NextPage)
                } as i32;
                let total = if self.f(F::HasSection) != 0 {
                    self.op(C::Estimate)
                } else {
                    self.f(F::CachedCount)
                };
                let paragraph = if self.f(F::HasSection) != 0
                    && page >= 0
                    && page < (self.f(F::PageCount) as i32)
                {
                    self.c(
                        C::Paragraph,
                        if page > 0 { page - 1 } else { page } as u16 as i32,
                        0,
                    )
                } else {
                    0
                };
                self.c(C::InitPosition, page, total as i32);
                if self.f(F::HasSection) != 0
                    && page >= 0
                    && page < (self.f(F::PageCount) as i32)
                    && self.c(C::Offset, page as u16 as i32, 0) != 0
                {
                    self.set(F::OutputHasOffset, 1);
                }
                if paragraph & 0x10000 != 0 {
                    self.set(F::OutputParagraph, paragraph & 0xffff);
                    self.set(F::OutputHasParagraph, 1);
                }
            }
            x if x == O::ConfirmThreshold as u8 => {
                if self.f(F::HasTouch) != 0 {
                    return 0;
                }
                return match self.f(F::LongPressFunction) {
                    2 | 3 | 5 | 6 => 400,
                    0 if self.op(C::Credentials) != 0 => 1000,
                    _ => 0,
                };
            }
            x if x == O::Remote as u8 => {
                if self.f(F::HasSection) == 0 {
                    return 0;
                }
                self.set(F::AutoTurn, 0);
                self.op(if a == 0 {
                    C::StartSelection
                } else if self.op(C::ToolbarMenu) != 0 {
                    C::OpenToolbar
                } else {
                    C::OpenMenu
                });
                return 1;
            }
            x if x == O::AtEnd as u8 => return self.at_end() as u32,
            x if x == O::ReturnEnd as u8 => {
                if self.f(F::HasBook) != 0 && self.op(C::SpineCount) > 0 {
                    self.set(F::Spine, self.op(C::SpineCount).wrapping_sub(1));
                    self.set(F::NextPage, 0);
                    self.set(F::PendingPage, u16::MAX as u32);
                }
            }
            x if x == O::RenderedEnd as u8 => {
                self.set(F::AutoTurn, 0);
                if self.f(F::PendingSyncError) != 0 {
                    self.set(F::PendingSyncError, 0);
                    self.op(C::SaveErrorPopup);
                }
            }
            x if x == O::InputOver as u8 => {
                return (self.f(F::OverlayOpen) != 0 || self.op(C::BaseInput) != 0) as u32
            }
            x if x == O::Turn as u8 || x == O::Skip as u8 => {
                if self.f(F::HasSection) == 0 {
                    return 0;
                }
                self.op(C::Acknowledge);
                let is_turn = x == O::Turn as u8;
                let movement = if is_turn {
                    {
                        let _lock = self.lock();
                        self.op(C::ClearDeferred);
                    }
                    super::turn(
                        self.f(F::Spine) as i32,
                        self.op(C::SpineCount) as i32,
                        self.f(F::Page) as i32,
                        self.f(F::PageCount) as u16,
                        self.f(F::Building) != 0,
                        a != 0,
                    )
                } else {
                    super::skip(self.f(F::Spine) as i32, self.f(F::Page) as i32, a)
                };
                return self.apply(
                    movement.kind,
                    movement.value,
                    movement.last_page != 0,
                    is_turn,
                );
            }
            x if x == O::ApplyMove as u8 => return self.apply(a as u8, b, c & 1 != 0, c & 2 != 0),
            _ => {}
        }
        0
    }
}
/// # Safety
/// Services and borrowed outputs remain valid for this synchronous call.
pub unsafe fn run(s: *const Services, operation: u8, a: i32, b: i32, c: u8) -> u32 {
    let Some(s) = (unsafe { s.as_ref() }) else {
        return 0;
    };
    let (Some(read), Some(write), Some(command)) = (s.read, s.write, s.command) else {
        return 0;
    };
    if s.context.is_null() {
        return 0;
    }
    Policy(Core {
        context: s.context,
        read,
        write,
        command,
    })
    .run(operation, a, b, c)
}
const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(size_of::<Services>() == 4 * size_of::<usize>());
    assert!(offset_of!(Services, context) == 0);
    assert!(offset_of!(Services, read) == size_of::<usize>());
    assert!(offset_of!(Services, write) == 2 * size_of::<usize>());
    assert!(offset_of!(Services, command) == 3 * size_of::<usize>());
};
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn c3_conversion_edges() {
        for (bits, want) in [
            (0x7fc12345, i32::MAX),
            (0xffc12345, i32::MIN),
            (0x7f800000, i32::MAX),
            (0xff800000, i32::MIN),
            (0x4f000000, i32::MAX),
            (0xcf000000, i32::MIN),
            (0x3fc00000, 1),
            (0xbfc00000, -1),
            (0x80000000, 0),
        ] {
            assert_eq!(progress_integer(f32::from_bits(bits)), want);
        }
    }
}
