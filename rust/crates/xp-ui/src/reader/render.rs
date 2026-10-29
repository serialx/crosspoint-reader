//! Reader page orchestration over synchronous renderer and allocator leaves.
use crate::scene::Refresh;
use core::ffi::c_void;

#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Query {
    Now,
    HasImages,
    ImagesNeedDecode,
    TakeForced,
    Counter,
    Frequency,
    TextAa,
    IsA4,
    IsX3,
    IsUc8279,
    AbsoluteSupported,
    OverlayCaps,
    AbsoluteCaps,
    StripBoard,
    Elements,
    WidthBytes,
    Height,
    FreeHeap,
    MaxBlock,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Kind {
    ImageReset,
    PrewarmBegin,
    Page,
    Status,
    PrewarmEnd,
    Highlights,
    Images,
    Placeholders,
    Clear,
    Display,
    DisplayAsync,
    GrayBase,
    AbsoluteBase,
    Precondition,
    SetCounter,
    PrepareStrips,
    ImageWindow,
    RecordBegin,
    RecordEnd,
    SetSource,
    ClearPlan,
    ClearWindow,
    ReleaseStrips,
    Allocate,
    BorrowScratch,
    Mode,
    StripBegin,
    StripEnd,
    Wait,
    WritePlane,
    GrayDisplay,
    Cleanup,
    StoreBw,
    RestoreBw,
    CopyLsb,
    CopyMsb,
    Log,
    Mark,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Buffer {
    Lsb,
    Msb,
    Scratch,
}
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Log {
    StripOom,
    AbsoluteFailed,
    AbsoluteQuality,
    ScratchOom,
    StoreFailed,
    TimingAsync,
    TimingTiled,
    TimingGray,
    TimingBw,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Op {
    pub kind: u8,
    pub buffer: u8,
    pub mode: u8,
    pub reserved: u8,
    pub y: u16,
    pub rows: u16,
    pub offset: u32,
    pub value: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Services {
    pub context: *mut c_void,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u8) -> u32>,
    pub execute: Option<unsafe extern "C" fn(*mut c_void, *const Op) -> u32>,
}
struct Engine {
    context: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    execute: unsafe extern "C" fn(*mut c_void, *const Op) -> u32,
    next_mark: u32,
}
impl Engine {
    fn query(&self, q: Query) -> u32 {
        unsafe { (self.read)(self.context, q as u8) }
    }
    fn op(&self, op: Op) -> u32 {
        unsafe { (self.execute)(self.context, &op) }
    }
    fn command(&self, kind: Kind, value: u32) -> u32 {
        self.op(Op {
            kind: kind as u8,
            value,
            ..Op::default()
        })
    }
    fn simple(&self, kind: Kind) -> u32 {
        self.command(kind, 0)
    }
    fn display(&self, kind: Kind, mode: Refresh) -> u32 {
        self.op(Op {
            kind: kind as u8,
            mode: mode as u8,
            ..Op::default()
        })
    }
    fn mode(&self, mode: u8) {
        self.op(Op {
            kind: Kind::Mode as u8,
            mode,
            ..Op::default()
        });
    }
    fn mark(&mut self) {
        self.command(Kind::Mark, self.next_mark);
        self.next_mark += 1;
    }
    fn log(&self, message: Log) {
        self.command(Kind::Log, message as u32);
    }
    fn advance_counter(&self) {
        let counter = self.query(Query::Counter) as i32;
        let next = if counter <= 1 {
            self.query(Query::Frequency)
        } else {
            (counter - 1) as u32
        };
        self.command(Kind::SetCounter, next);
    }
    fn cycle(&self, asynchronous: bool) {
        let mode = if (self.query(Query::Counter) as i32) <= 1 {
            Refresh::Half
        } else {
            Refresh::Fast
        };
        self.display(
            if asynchronous {
                Kind::DisplayAsync
            } else {
                Kind::Display
            },
            mode,
        );
        self.advance_counter();
    }
    fn combined_cycle(&self) {
        if self.query(Query::OverlayCaps) & 2 == 0 {
            self.cycle(false);
            return;
        }
        let mode = if (self.query(Query::Counter) as i32) <= 1 {
            Refresh::Half
        } else {
            Refresh::Fast
        };
        self.display(Kind::GrayBase, mode);
        self.advance_counter();
    }
    fn gray_pass(&self, absolute: bool, text: bool) {
        if self.query(Query::IsA4) != 0 {
            self.simple(Kind::Highlights);
        }
        self.simple(if absolute || text {
            Kind::Page
        } else {
            Kind::Images
        });
        if absolute || self.query(Query::IsA4) != 0 {
            self.simple(Kind::Status);
        }
    }
    fn fits(&self, bytes: u32) -> bool {
        self.query(Query::FreeHeap) >= bytes.saturating_add(60000)
            && self.query(Query::MaxBlock) >= bytes.saturating_add(16 * 1024)
    }
    fn allocate(&self, buffer: Buffer, bytes: u32) -> bool {
        self.op(Op {
            kind: Kind::Allocate as u8,
            buffer: buffer as u8,
            value: bytes,
            ..Op::default()
        }) != 0
    }
    fn write(&self, buffer: Buffer, lsb: bool, y: u16, rows: u16) {
        self.op(Op {
            kind: Kind::WritePlane as u8,
            buffer: buffer as u8,
            mode: if lsb { 1 } else { 2 },
            y,
            rows,
            ..Op::default()
        });
    }
    fn plane(
        &self,
        buffer: Buffer,
        lsb: bool,
        width: u32,
        height: u16,
        absolute: bool,
        text: bool,
    ) {
        self.mode(if lsb { 1 } else { 2 });
        let mut y = 0u16;
        while y < height {
            let rows = (height - y).min(80);
            self.op(Op {
                kind: Kind::StripBegin as u8,
                buffer: buffer as u8,
                y,
                rows,
                offset: if matches!(buffer, Buffer::Scratch) {
                    0
                } else {
                    u32::from(y).wrapping_mul(width)
                },
                ..Op::default()
            });
            self.command(Kind::Clear, 0);
            self.gray_pass(absolute, text);
            self.simple(Kind::StripEnd);
            if matches!(buffer, Buffer::Scratch) {
                self.write(buffer, lsb, y, rows);
            }
            y += rows;
        }
    }
    fn run(mut self, strip_enabled: bool) {
        self.mark();
        self.simple(Kind::ImageReset);
        self.simple(Kind::PrewarmBegin);
        self.simple(Kind::Page);
        self.simple(Kind::Status);
        self.simple(Kind::PrewarmEnd);
        self.mark();
        let images = self.query(Query::HasImages) != 0;
        let decode = images && self.query(Query::ImagesNeedDecode) != 0;
        let manual = self.query(Query::TakeForced) != 0;
        let clean = manual || (self.query(Query::Counter) as i32) <= 1;
        let text = self.query(Query::TextAa) != 0;
        let any_gray = text || (images && self.query(Query::IsA4) == 0);
        let absolute = images
            && self.query(Query::IsX3) == 0
            && self.query(Query::IsUc8279) != 0
            && self.query(Query::AbsoluteSupported) != 0;
        let caps = self.query(if absolute {
            Query::AbsoluteCaps
        } else {
            Query::OverlayCaps
        });
        let tiled = any_gray && caps & 1 != 0;
        let combined = tiled && !images && caps & 2 != 0;
        let overlap = tiled && caps & 4 != 0 && !images;
        if decode {
            self.simple(Kind::Placeholders);
            self.simple(Kind::Status);
            self.display(Kind::Display, Refresh::Fast);
            self.command(Kind::Clear, 255);
        }
        let mut active = false;
        if strip_enabled {
            if self.query(Query::StripBoard) != 0 && self.query(Query::Elements) <= 512 {
                let scratch_bytes = self.query(Query::WidthBytes).wrapping_mul(80);
                if self.command(Kind::PrepareStrips, u32::from(images)) != 0 {
                    self.simple(Kind::ImageWindow);
                    self.simple(Kind::RecordBegin);
                    self.simple(Kind::Page);
                    if self.simple(Kind::RecordEnd) != 0 {
                        active = self.command(Kind::SetSource, scratch_bytes) != 0;
                    }
                } else {
                    self.log(Log::StripOom);
                }
            }
            if !active {
                self.simple(Kind::ClearPlan);
                self.simple(Kind::ClearWindow);
                self.simple(Kind::ReleaseStrips);
            }
        }
        if !active || images {
            self.simple(Kind::Highlights);
            self.simple(Kind::Page);
        }
        self.simple(Kind::Status);
        self.mark();
        if absolute {
            if self.display(
                Kind::AbsoluteBase,
                if clean { Refresh::Half } else { Refresh::Fast },
            ) == 0
            {
                self.log(Log::AbsoluteFailed);
                self.cycle(false);
                return;
            }
            self.log(Log::AbsoluteQuality);
            self.command(Kind::SetCounter, 1);
        } else if images {
            self.display(
                Kind::Display,
                if clean { Refresh::Half } else { Refresh::Fast },
            );
            self.command(Kind::SetCounter, 1);
        } else if combined {
            self.combined_cycle();
        } else if any_gray {
            if (self.query(Query::Counter) as i32) <= 1 {
                self.display(Kind::Display, Refresh::Half);
                self.simple(Kind::Precondition);
                self.command(Kind::SetCounter, self.query(Query::Frequency));
            } else if overlap {
                self.cycle(true);
            } else {
                self.display(Kind::GrayBase, Refresh::Fast);
                self.command(Kind::SetCounter, self.query(Query::Counter).wrapping_sub(1));
            }
        } else {
            self.cycle(false);
        }
        self.mark();
        if tiled {
            let height = self.query(Query::Height) as u16;
            let width = self.query(Query::WidthBytes);
            let bytes = width.wrapping_mul(u32::from(height));
            let lsb = !active && overlap && self.fits(bytes) && self.allocate(Buffer::Lsb, bytes);
            let msb = lsb && self.fits(bytes) && self.allocate(Buffer::Msb, bytes);
            if lsb {
                self.plane(Buffer::Lsb, true, width, height, absolute, text);
                if msb {
                    self.plane(Buffer::Msb, false, width, height, absolute, text);
                }
                self.mark();
                self.simple(Kind::Wait);
                self.mark();
                self.write(Buffer::Lsb, true, 0, height);
                if msb {
                    self.write(Buffer::Msb, false, 0, height);
                } else {
                    self.plane(Buffer::Lsb, false, width, height, absolute, text);
                    self.write(Buffer::Lsb, false, 0, height);
                }
                self.mark();
                self.mode(0);
                self.simple(Kind::GrayDisplay);
                self.mark();
                self.simple(Kind::Cleanup);
                self.mark();
                self.log(Log::TimingAsync);
            } else {
                let scratch = if active {
                    self.simple(Kind::BorrowScratch) != 0
                } else {
                    self.allocate(Buffer::Scratch, width.wrapping_mul(80))
                };
                self.simple(Kind::Wait);
                if !scratch {
                    self.log(Log::ScratchOom);
                    if overlap || combined {
                        self.simple(Kind::Cleanup);
                    }
                } else {
                    self.plane(Buffer::Scratch, true, width, height, absolute, text);
                    self.mark();
                    self.plane(Buffer::Scratch, false, width, height, absolute, text);
                    self.mark();
                    self.mode(0);
                    self.simple(Kind::GrayDisplay);
                    self.mark();
                    self.simple(Kind::Cleanup);
                    self.mark();
                    self.mark();
                    self.log(Log::TimingTiled);
                }
            }
        } else if any_gray {
            if self.simple(Kind::StoreBw) == 0 {
                self.log(Log::StoreFailed);
                if absolute {
                    self.mode(0);
                }
                return;
            }
            self.mark();
            self.command(Kind::Clear, if absolute { 255 } else { 0 });
            self.mode(1);
            self.gray_pass(absolute, text);
            self.simple(Kind::CopyLsb);
            self.mark();
            self.command(Kind::Clear, if absolute { 255 } else { 0 });
            self.mode(2);
            self.gray_pass(absolute, text);
            self.simple(Kind::CopyMsb);
            self.mark();
            self.simple(Kind::GrayDisplay);
            self.mark();
            self.mode(0);
            self.simple(Kind::RestoreBw);
            self.mark();
            self.mark();
            self.log(Log::TimingGray);
        } else {
            self.mark();
            self.log(Log::TimingBw);
        }
    }
}
/// # Safety
/// Callbacks/context follow xp_ui_reader.h, remain valid synchronously and do not retain operation pointers.
pub unsafe fn render(services: Services, strip_enabled: bool) {
    let (Some(read), Some(execute)) = (services.read, services.execute) else {
        return;
    };
    Engine {
        context: services.context,
        read,
        execute,
        next_mark: 0,
    }
    .run(strip_enabled);
}
const _: () = {
    use core::mem::{align_of, offset_of, size_of};
    assert!(size_of::<Op>() == 16 && align_of::<Op>() == 4);
    assert!(
        offset_of!(Op, kind) == 0
            && offset_of!(Op, buffer) == 1
            && offset_of!(Op, mode) == 2
            && offset_of!(Op, reserved) == 3
    );
    assert!(
        offset_of!(Op, y) == 4
            && offset_of!(Op, rows) == 6
            && offset_of!(Op, offset) == 8
            && offset_of!(Op, value) == 12
    );
    assert!(
        size_of::<Services>() == 3 * size_of::<usize>()
            && align_of::<Services>() == align_of::<usize>()
    );
    assert!(
        offset_of!(Services, context) == 0
            && offset_of!(Services, read) == size_of::<usize>()
            && offset_of!(Services, execute) == 2 * size_of::<usize>()
    );
};
