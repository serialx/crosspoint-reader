//! Reader page orchestration over synchronous renderer and allocator leaves.
mod command;
pub use command::{Command, Plane};

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
    IsBw = 19,
    IsX3 = 8,
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
pub trait Renderer {
    fn query(&self, query: Query) -> u32;
    fn execute(&self, command: Command) -> u32;
}

pub struct PhysicalRenderer<'a> {
    services: &'a Services,
}
impl<'a> PhysicalRenderer<'a> {
    /// # Safety
    /// Callbacks/context follow xp_ui_reader.h, remain valid for this borrow,
    /// and do not retain operation pointers after synchronous calls return.
    pub unsafe fn new(services: &'a Services) -> Self {
        Self { services }
    }
}
impl Renderer for PhysicalRenderer<'_> {
    fn query(&self, query: Query) -> u32 {
        unsafe {
            self.services.read.expect("native render queries")(self.services.context, query as u8)
        }
    }
    fn execute(&self, command: Command) -> u32 {
        let op = Op::from(command);
        unsafe {
            self.services.execute.expect("native render operations")(self.services.context, &op)
        }
    }
}

struct Engine<'a> {
    renderer: &'a dyn Renderer,
    next_mark: u32,
}
impl Engine<'_> {
    fn query(&self, query: Query) -> u32 {
        self.renderer.query(query)
    }
    #[inline(never)]
    fn command(&self, command: Command) -> u32 {
        self.renderer.execute(command)
    }
    #[inline(never)]
    fn mode(&self, plane: Plane) {
        self.command(Command::Mode(plane));
    }
    fn mark(&mut self) {
        self.command(Command::Mark(self.next_mark));
        self.next_mark += 1;
    }
    fn log(&self, message: Log) {
        self.command(Command::Log(message));
    }
    fn advance_counter(&self) {
        let counter = self.query(Query::Counter) as i32;
        let next = if counter <= 1 {
            self.query(Query::Frequency)
        } else {
            (counter - 1) as u32
        };
        self.command(Command::SetCounter(next));
    }
    fn cycle(&self, asynchronous: bool) {
        let mode = if (self.query(Query::Counter) as i32) <= 1 {
            Refresh::Half
        } else {
            Refresh::Fast
        };
        self.command(if asynchronous {
            Command::DisplayAsync(mode)
        } else {
            Command::Display(mode)
        });
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
        self.command(Command::GrayBase(mode));
        self.advance_counter();
    }
    fn gray_pass(&self, absolute: bool, text: bool) {
        if self.query(Query::IsA4) != 0 {
            self.command(Command::Highlights);
        }
        self.command(if absolute || text {
            Command::Page
        } else {
            Command::Images
        });
        if absolute || self.query(Query::IsA4) != 0 {
            self.command(Command::Status);
        }
    }
    fn fits(&self, bytes: u32) -> bool {
        self.query(Query::FreeHeap) >= bytes.saturating_add(60000)
            && self.query(Query::MaxBlock) >= bytes.saturating_add(16 * 1024)
    }
    fn allocate(&self, buffer: Buffer, bytes: u32) -> bool {
        self.command(Command::Allocate { buffer, bytes }) != 0
    }
    fn write(&self, buffer: Buffer, lsb: bool, y: u16, rows: u16) {
        self.command(Command::WritePlane {
            buffer,
            plane: if lsb { Plane::Lsb } else { Plane::Msb },
            y,
            rows,
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
        self.mode(if lsb { Plane::Lsb } else { Plane::Msb });
        let mut y = 0u16;
        while y < height {
            let rows = (height - y).min(80);
            self.command(Command::StripBegin {
                buffer,
                y,
                rows,
                offset: if matches!(buffer, Buffer::Scratch) {
                    0
                } else {
                    u32::from(y).wrapping_mul(width)
                },
            });
            self.command(Command::Clear(0));
            self.gray_pass(absolute, text);
            self.command(Command::StripEnd);
            if matches!(buffer, Buffer::Scratch) {
                self.write(buffer, lsb, y, rows);
            }
            y += rows;
        }
    }
    // Plane upload temporaries are not live during the initial page/image draw.
    #[inline(never)]
    fn tiled(&mut self, active: bool, overlap: bool, combined: bool, absolute: bool, text: bool) {
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
            self.command(Command::Wait);
            self.mark();
            self.write(Buffer::Lsb, true, 0, height);
            if msb {
                self.write(Buffer::Msb, false, 0, height);
            } else {
                self.plane(Buffer::Lsb, false, width, height, absolute, text);
                self.write(Buffer::Lsb, false, 0, height);
            }
            self.mark();
            self.mode(Plane::Bw);
            self.command(Command::GrayDisplay);
            self.mark();
            self.command(Command::Cleanup);
            self.mark();
            self.log(Log::TimingAsync);
        } else {
            let scratch = if active {
                self.command(Command::BorrowScratch) != 0
            } else {
                self.allocate(Buffer::Scratch, width.wrapping_mul(80))
            };
            self.command(Command::Wait);
            if !scratch {
                self.log(Log::ScratchOom);
                if overlap || combined {
                    self.command(Command::Cleanup);
                }
            } else {
                self.plane(Buffer::Scratch, true, width, height, absolute, text);
                self.mark();
                self.plane(Buffer::Scratch, false, width, height, absolute, text);
                self.mark();
                self.mode(Plane::Bw);
                self.command(Command::GrayDisplay);
                self.mark();
                self.command(Command::Cleanup);
                self.mark();
                self.mark();
                self.log(Log::TimingTiled);
            }
        }
    }
    fn run(mut self, strip_enabled: bool) {
        self.mark();
        self.command(Command::ImageReset);
        self.command(Command::PrewarmBegin);
        self.command(Command::Page);
        self.command(Command::Status);
        self.command(Command::PrewarmEnd);
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
            self.command(Command::Placeholders);
            self.command(Command::Status);
            self.command(Command::Display(Refresh::Fast));
            self.command(Command::Clear(255));
        }
        let mut active = false;
        if strip_enabled {
            if self.query(Query::StripBoard) != 0 && self.query(Query::Elements) <= 512 {
                let scratch_bytes = self.query(Query::WidthBytes).wrapping_mul(80);
                if self.command(Command::PrepareStrips { images }) != 0 {
                    self.command(Command::ImageWindow);
                    self.command(Command::RecordBegin);
                    self.command(Command::Page);
                    if self.command(Command::RecordEnd) != 0 {
                        active = self.command(Command::SetSource { scratch_bytes }) != 0;
                    }
                } else {
                    self.log(Log::StripOom);
                }
            }
            if !active {
                self.command(Command::ClearPlan);
                self.command(Command::ClearWindow);
                self.command(Command::ReleaseStrips);
            }
        }
        if !active || images {
            self.command(Command::Highlights);
            self.command(Command::Page);
        }
        self.command(Command::Status);
        self.mark();
        if absolute {
            if self.command(Command::AbsoluteBase(if clean {
                Refresh::Half
            } else {
                Refresh::Fast
            })) == 0
            {
                self.log(Log::AbsoluteFailed);
                self.cycle(false);
                return;
            }
            self.log(Log::AbsoluteQuality);
            self.command(Command::SetCounter(1));
        } else if images {
            self.command(Command::Display(if clean {
                Refresh::Half
            } else {
                Refresh::Fast
            }));
            self.command(Command::SetCounter(1));
        } else if combined {
            self.combined_cycle();
        } else if any_gray {
            if (self.query(Query::Counter) as i32) <= 1 {
                self.command(Command::Display(Refresh::Half));
                self.command(Command::Precondition);
                self.command(Command::SetCounter(self.query(Query::Frequency)));
            } else if overlap {
                self.cycle(true);
            } else {
                self.command(Command::GrayBase(Refresh::Fast));
                self.command(Command::SetCounter(
                    self.query(Query::Counter).wrapping_sub(1),
                ));
            }
        } else {
            self.cycle(false);
        }
        self.mark();
        if tiled {
            self.tiled(active, overlap, combined, absolute, text);
        } else if any_gray {
            if self.command(Command::StoreBw) == 0 {
                self.log(Log::StoreFailed);
                if absolute {
                    self.mode(Plane::Bw);
                }
                return;
            }
            self.mark();
            self.command(Command::Clear(if absolute { 255 } else { 0 }));
            self.mode(Plane::Lsb);
            self.gray_pass(absolute, text);
            self.command(Command::CopyLsb);
            self.mark();
            self.command(Command::Clear(if absolute { 255 } else { 0 }));
            self.mode(Plane::Msb);
            self.gray_pass(absolute, text);
            self.command(Command::CopyMsb);
            self.mark();
            self.command(Command::GrayDisplay);
            self.mark();
            self.mode(Plane::Bw);
            self.command(Command::RestoreBw);
            self.mark();
            self.mark();
            self.log(Log::TimingGray);
        } else {
            self.mark();
            self.log(Log::TimingBw);
        }
    }
}
pub fn render(renderer: &dyn Renderer, strip_enabled: bool) {
    Engine {
        renderer,
        next_mark: 0,
    }
    .run(strip_enabled);
}
const _: () = {
    assert!(Query::Now as u8 == 0);
    assert!(Query::HasImages as u8 == 1);
    assert!(Query::ImagesNeedDecode as u8 == 2);
    assert!(Query::TakeForced as u8 == 3);
    assert!(Query::Counter as u8 == 4);
    assert!(Query::Frequency as u8 == 5);
    assert!(Query::TextAa as u8 == 6);
    assert!(Query::IsA4 as u8 == 7);
    assert!(Query::IsX3 as u8 == 8);
    assert!(Query::IsUc8279 as u8 == 9);
    assert!(Query::AbsoluteSupported as u8 == 10);
    assert!(Query::OverlayCaps as u8 == 11);
    assert!(Query::AbsoluteCaps as u8 == 12);
    assert!(Query::StripBoard as u8 == 13);
    assert!(Query::Elements as u8 == 14);
    assert!(Query::WidthBytes as u8 == 15);
    assert!(Query::Height as u8 == 16);
    assert!(Query::FreeHeap as u8 == 17);
    assert!(Query::MaxBlock as u8 == 18);
    assert!(Query::IsBw as u8 == 19);
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
