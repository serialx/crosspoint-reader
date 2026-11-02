use super::{Buffer, Kind, Log, Op};
use crate::scene::Refresh;

#[derive(Clone, Copy)]
pub enum Plane {
    Bw,
    Lsb,
    Msb,
}
impl Plane {
    fn wire(self) -> u8 {
        match self {
            Self::Bw => 0,
            Self::Lsb => 1,
            Self::Msb => 2,
        }
    }
}

#[derive(Clone, Copy)]
pub enum Command {
    ImageReset,
    PrewarmBegin,
    Page,
    Status,
    PrewarmEnd,
    Highlights,
    Images,
    Placeholders,
    Clear(u8),
    Display(Refresh),
    DisplayAsync(Refresh),
    GrayBase(Refresh),
    AbsoluteBase(Refresh),
    Precondition,
    SetCounter(u32),
    PrepareStrips {
        images: bool,
    },
    ImageWindow,
    RecordBegin,
    RecordEnd,
    SetSource {
        scratch_bytes: u32,
    },
    ClearPlan,
    ClearWindow,
    ReleaseStrips,
    Allocate {
        buffer: Buffer,
        bytes: u32,
    },
    BorrowScratch,
    Mode(Plane),
    StripBegin {
        buffer: Buffer,
        y: u16,
        rows: u16,
        offset: u32,
    },
    StripEnd,
    Wait,
    WritePlane {
        buffer: Buffer,
        plane: Plane,
        y: u16,
        rows: u16,
    },
    GrayDisplay,
    Cleanup,
    StoreBw,
    RestoreBw,
    CopyLsb,
    CopyMsb,
    Log(Log),
    Mark(u32),
}

impl From<Command> for Op {
    fn from(command: Command) -> Self {
        let mut op = Self::default();
        op.kind = match command {
            Command::ImageReset => Kind::ImageReset,
            Command::PrewarmBegin => Kind::PrewarmBegin,
            Command::Page => Kind::Page,
            Command::Status => Kind::Status,
            Command::PrewarmEnd => Kind::PrewarmEnd,
            Command::Highlights => Kind::Highlights,
            Command::Images => Kind::Images,
            Command::Placeholders => Kind::Placeholders,
            Command::Clear(value) => {
                op.value = u32::from(value);
                Kind::Clear
            }
            Command::Display(mode) => {
                op.mode = mode as u8;
                Kind::Display
            }
            Command::DisplayAsync(mode) => {
                op.mode = mode as u8;
                Kind::DisplayAsync
            }
            Command::GrayBase(mode) => {
                op.mode = mode as u8;
                Kind::GrayBase
            }
            Command::AbsoluteBase(mode) => {
                op.mode = mode as u8;
                Kind::AbsoluteBase
            }
            Command::Precondition => Kind::Precondition,
            Command::SetCounter(value) => {
                op.value = value;
                Kind::SetCounter
            }
            Command::PrepareStrips { images } => {
                op.value = u32::from(images);
                Kind::PrepareStrips
            }
            Command::ImageWindow => Kind::ImageWindow,
            Command::RecordBegin => Kind::RecordBegin,
            Command::RecordEnd => Kind::RecordEnd,
            Command::SetSource { scratch_bytes } => {
                op.value = scratch_bytes;
                Kind::SetSource
            }
            Command::ClearPlan => Kind::ClearPlan,
            Command::ClearWindow => Kind::ClearWindow,
            Command::ReleaseStrips => Kind::ReleaseStrips,
            Command::Allocate { buffer, bytes } => {
                op.buffer = buffer as u8;
                op.value = bytes;
                Kind::Allocate
            }
            Command::BorrowScratch => Kind::BorrowScratch,
            Command::Mode(plane) => {
                op.mode = plane.wire();
                Kind::Mode
            }
            Command::StripBegin {
                buffer,
                y,
                rows,
                offset,
            } => {
                op.buffer = buffer as u8;
                op.y = y;
                op.rows = rows;
                op.offset = offset;
                Kind::StripBegin
            }
            Command::StripEnd => Kind::StripEnd,
            Command::Wait => Kind::Wait,
            Command::WritePlane {
                buffer,
                plane,
                y,
                rows,
            } => {
                op.buffer = buffer as u8;
                op.mode = plane.wire();
                op.y = y;
                op.rows = rows;
                Kind::WritePlane
            }
            Command::GrayDisplay => Kind::GrayDisplay,
            Command::Cleanup => Kind::Cleanup,
            Command::StoreBw => Kind::StoreBw,
            Command::RestoreBw => Kind::RestoreBw,
            Command::CopyLsb => Kind::CopyLsb,
            Command::CopyMsb => Kind::CopyMsb,
            Command::Log(message) => {
                op.value = message as u32;
                Kind::Log
            }
            Command::Mark(index) => {
                op.value = index;
                Kind::Mark
            }
        } as u8;
        op
    }
}
