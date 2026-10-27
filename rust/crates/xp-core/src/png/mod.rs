//! Bounded PNG decoding over caller-owned file I/O.
extern crate alloc;
use crate::{image::Row, inflate::InflateState};
use alloc::{alloc::alloc_zeroed, boxed::Box, vec::Vec};
use core::{
    alloc::Layout,
    mem,
    ops::{Deref, DerefMut},
    ptr,
};

pub mod bmp;

pub const ROW_LIMIT: usize = 16416;
const WINDOW: usize = 32768;

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Diagnostic {
    Signature = 1,
    Ihdr = 2,
    Image = 3,
    Method = 4,
    Interlace = 5,
    Dimensions = 6,
    Color = 7,
    RowSize = 8,
    Scanline = 9,
    Idat = 10,
    Inflate = 11,
    Scaling = 12,
    RowScratch = 13,
    Atkinson1 = 14,
    Atkinson2 = 15,
    Accumulators = 16,
    ScanlineDecode = 17,
    Filter = 18,
    Complete = 19,
    Open = 20,
    DecoderAlloc = 21,
    Pipeline = 22,
    PipelineAlloc = 23,
    Cache = 24,
    DecodeStart = 25,
    DecodeFailed = 26,
    DecodeComplete = 27,
    UnsupportedDepth = 28,
    RowBuffer = 29,
    FrameDimensions = 30,
    GrayWidth = 31,
}

pub trait Source {
    fn diagnostic(&mut self, _event: Diagnostic, _a: u32, _b: u32, _c: u32, _d: u32) {}
    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> usize;
    fn len(&self) -> u32;
    fn progress(&mut self) {}
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Frame,
    Bmp,
}

#[derive(Clone, Copy, Debug)]
pub struct Info {
    pub width: u32,
    pub height: u32,
    pub depth: u8,
    pub color: u8,
    pub pitch: usize,
    pub channels: usize,
}

fn be32(bytes: &[u8]) -> u32 {
    u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

pub fn info(source: &mut impl Source, mode: Mode) -> Option<Info> {
    let mut header = [0; 33];
    let read = source.read(0, &mut header);
    if mode == Mode::Bmp {
        if read < 8 || &header[..8] != b"\x89PNG\r\n\x1a\n" {
            source.diagnostic(Diagnostic::Signature, 0, 0, 0, 0);
            return None;
        } else if read >= 12 && (read < 16 || &header[12..16] != b"IHDR") {
            source.diagnostic(Diagnostic::Ihdr, 0, 0, 0, 0);
            return None;
        }
    }
    if read != header.len() || &header[..4] != b"\x89PNG" || &header[12..16] != b"IHDR" {
        return None;
    }
    let width = be32(&header[16..20]);
    let height = be32(&header[20..24]);
    let depth = header[24];
    let color = header[25];
    if mode == Mode::Bmp {
        source.diagnostic(
            Diagnostic::Image,
            width,
            height,
            u32::from(depth) | u32::from(color) << 8 | u32::from(header[28]) << 16,
            0,
        );
        if header[26] != 0 || header[27] != 0 {
            source.diagnostic(Diagnostic::Method, 0, 0, 0, 0);
        } else if header[28] != 0 {
            source.diagnostic(Diagnostic::Interlace, 0, 0, 0, 0);
        } else if width == 0 || height == 0 || width > 2048 || height > 3072 {
            source.diagnostic(Diagnostic::Dimensions, width, height, 0, 0);
        } else if !matches!(color, 0 | 2 | 3 | 4 | 6) {
            source.diagnostic(Diagnostic::Color, u32::from(color), 0, 0, 0);
        }
    }
    if header[28] != 0 {
        return None;
    }
    let channels = match color {
        2 => 3,
        4 => 2,
        6 => 4,
        _ => 1,
    };
    let pitch = (u64::from(width) * channels as u64 * u64::from(depth)).div_ceil(8);
    match mode {
        Mode::Frame => {
            if be32(&header[8..12]) != 13
                || crate::crc32(&header[12..29]) != be32(&header[29..33])
                || depth > 8
                || pitch >= ROW_LIMIT as u64
            {
                return None;
            }
        }
        Mode::Bmp => {
            if pitch > 16384
                && header[26] == 0
                && header[27] == 0
                && width <= 2048
                && height <= 3072
                && width > 0
                && height > 0
                && matches!(color, 0 | 2 | 3 | 4 | 6)
            {
                source.diagnostic(Diagnostic::RowSize, pitch as u32, 0, 0, 0);
            }
            if &header[..8] != b"\x89PNG\r\n\x1a\n"
                || header[26] != 0
                || header[27] != 0
                || width == 0
                || height == 0
                || width > 2048
                || height > 3072
                || !matches!(color, 0 | 2 | 3 | 4 | 6)
                || !matches!(depth, 1 | 2 | 4 | 8 | 16)
                || (color == 3 && depth == 16)
                || (matches!(color, 2 | 4 | 6) && depth < 8)
                || pitch > 16384
            {
                return None;
            }
        }
    }
    Some(Info {
        width,
        height,
        depth,
        color,
        pitch: pitch as usize,
        channels,
    })
}

impl Info {
    pub fn frame_supported(self) -> bool {
        self.width > 0
            && self.height > 0
            && self.width <= 32767
            && self.height <= 32767
            && u64::from(self.width) * u64::from(self.height) <= 8388608
            && self.width <= (ROW_LIMIT / 2) as u32
            && (self.pitch + 1) * 2 + 32 <= ROW_LIMIT
            && (self.depth == 8 || (matches!(self.color, 0 | 3) && matches!(self.depth, 1 | 2 | 4)))
            && matches!(self.color, 0 | 2 | 3 | 4 | 6)
    }
}

pub(super) fn zeros<T: Default + Clone>(len: usize) -> Option<Vec<T>> {
    let mut bytes = Vec::new();
    bytes.try_reserve_exact(len).ok()?;
    bytes.resize(len, T::default());
    Some(bytes)
}

struct Workspace {
    inflate: InflateState,
    input: [u8; 2048],
    palette: [u8; 1024],
}
impl Workspace {
    fn new(mode: Mode) -> Option<Box<Self>> {
        // One decode owns this block; initialize in place to avoid an 11 KiB stack temporary.
        let raw = unsafe { alloc_zeroed(Layout::new::<Self>()).cast::<Self>() };
        if raw.is_null() {
            return None;
        }
        unsafe {
            InflateState::init_in_place(ptr::addr_of_mut!((*raw).inflate), mode == Mode::Bmp);
            Some(Box::from_raw(raw))
        }
    }
}

enum Work<'a> {
    Owned(Box<Workspace>),
    Lent(&'a mut Workspace),
}
impl Deref for Work<'_> {
    type Target = Workspace;
    fn deref(&self) -> &Workspace {
        match self {
            Self::Owned(v) => v,
            Self::Lent(v) => v,
        }
    }
}
impl DerefMut for Work<'_> {
    fn deref_mut(&mut self) -> &mut Workspace {
        match self {
            Self::Owned(v) => v,
            Self::Lent(v) => v,
        }
    }
}
enum Window<'a> {
    Owned(Vec<u8>),
    Lent(&'a mut [u8]),
}
impl Deref for Window<'_> {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        match self {
            Self::Owned(v) => v,
            Self::Lent(v) => v,
        }
    }
}
impl DerefMut for Window<'_> {
    fn deref_mut(&mut self) -> &mut [u8] {
        match self {
            Self::Owned(v) => v,
            Self::Lent(v) => v,
        }
    }
}

pub fn scratch_size() -> usize {
    mem::size_of::<Workspace>() + WINDOW
}

pub struct Decoder<'a> {
    pub info: Info,
    mode: Mode,
    workspace: Work<'a>,
    rows: Vec<u8>,
    window: Window<'a>,
    offset: u32,
    remaining: u32,
    input_pos: usize,
    input_end: usize,
    window_pos: usize,
    window_end: usize,
    window_next: usize,
    done: bool,
    row_pos: usize,
    y: u32,
    palette_entries: usize,
    alpha: bool,
    transparent: u32,
    zlib_header: bool,
}

impl<'a> Decoder<'a> {
    pub fn new(info: Info, mode: Mode, source: &mut impl Source) -> Option<Self> {
        Self::with_scratch(info, mode, None, source)
    }

    pub fn with_scratch(
        info: Info,
        mode: Mode,
        scratch: Option<&'a mut [u8]>,
        source: &mut impl Source,
    ) -> Option<Self> {
        if mode == Mode::Frame && !info.frame_supported() {
            let pitch = if matches!(info.color, 0 | 3) && info.depth < 8 {
                info.pitch
            } else {
                info.width as usize * info.channels
            };
            let required = (pitch + 1) * 2 + 32;
            if required > ROW_LIMIT {
                source.diagnostic(
                    Diagnostic::RowBuffer,
                    required as u32,
                    info.width,
                    u32::from(info.color),
                    u32::from(info.depth),
                );
            } else if info.depth != 8
                && !(matches!(info.depth, 1 | 2 | 4) && matches!(info.color, 0 | 3))
            {
                source.diagnostic(
                    Diagnostic::UnsupportedDepth,
                    u32::from(info.depth),
                    u32::from(info.color),
                    0,
                    0,
                );
            } else if info.width > (ROW_LIMIT / 2) as u32 {
                source.diagnostic(Diagnostic::GrayWidth, info.width, 0, 0, 0);
            } else {
                source.diagnostic(Diagnostic::DecodeFailed, 0, 0, 0, 0);
            }
            return None;
        }
        let (workspace, window) = if let Some(scratch) = scratch {
            if scratch.len() < scratch_size()
                || !(scratch.as_ptr() as usize).is_multiple_of(mem::align_of::<Workspace>())
            {
                return None;
            }
            let (work, window) =
                scratch[..scratch_size()].split_at_mut(mem::size_of::<Workspace>());
            work.fill(0);
            window.fill(0);
            let raw = work.as_mut_ptr().cast::<Workspace>();
            // All workspace fields are initialized in place; the loan outlives this decoder.
            unsafe {
                InflateState::init_in_place(ptr::addr_of_mut!((*raw).inflate), mode == Mode::Bmp);
                (Work::Lent(&mut *raw), Window::Lent(window))
            }
        } else {
            // Reserve the largest block before smaller buffers can fragment it.
            let window = Window::Owned(zeros(WINDOW).or_else(|| {
                source.diagnostic(
                    if mode == Mode::Bmp {
                        Diagnostic::Inflate
                    } else {
                        Diagnostic::DecoderAlloc
                    },
                    0,
                    0,
                    0,
                    0,
                );
                None
            })?);
            (
                Work::Owned(Workspace::new(mode).or_else(|| {
                    source.diagnostic(
                        if mode == Mode::Bmp {
                            Diagnostic::Inflate
                        } else {
                            Diagnostic::DecoderAlloc
                        },
                        0,
                        0,
                        0,
                        0,
                    );
                    None
                })?),
                window,
            )
        };
        Some(Self {
            info,
            mode,
            workspace,
            rows: zeros((info.pitch + 1) * 2).or_else(|| {
                source.diagnostic(
                    if mode == Mode::Bmp {
                        Diagnostic::Scanline
                    } else {
                        Diagnostic::DecoderAlloc
                    },
                    info.pitch as u32,
                    0,
                    0,
                    0,
                );
                None
            })?,
            window,
            offset: 33,
            remaining: 0,
            input_pos: 0,
            input_end: 0,
            window_pos: 0,
            window_end: 0,
            window_next: 0,
            done: false,
            row_pos: 0,
            y: 0,
            palette_entries: 0,
            alpha: matches!(info.color, 4 | 6),
            transparent: 0,
            zlib_header: false,
        })
    }

    fn chunk(&mut self, source: &mut impl Source) -> bool {
        loop {
            let mut header = [0; 8];
            if source.read(self.offset, &mut header) != 8 {
                return false;
            }
            let len = be32(&header[..4]);
            let Some(data) = self.offset.checked_add(8) else {
                return false;
            };
            let Some(next) = data.checked_add(len).and_then(|n| n.checked_add(4)) else {
                return false;
            };
            if len > i32::MAX as u32 || data > source.len() || len > source.len() - data {
                return false;
            }
            self.offset = data;
            match &header[4..] {
                b"IDAT" if len != 0 => {
                    self.remaining = len;
                    return true;
                }
                b"PLTE" => {
                    if len > 768 || data.saturating_add(len) > source.len() {
                        return false;
                    }
                    let n = len as usize;
                    if source.read(data, &mut self.workspace.palette[..n]) != n {
                        return false;
                    }
                    self.workspace.palette[768..].fill(255);
                    self.palette_entries = n / 3;
                }
                b"tRNS" if self.mode == Mode::Frame => {
                    if self.info.color == 3 {
                        if len > 256 {
                            return false;
                        }
                        if source.read(data, &mut self.workspace.palette[768..768 + len as usize])
                            != len as usize
                        {
                            return false;
                        }
                        self.alpha = true;
                    } else if len == 2 || len == 6 {
                        let mut values = [0; 6];
                        if source.read(data, &mut values[..len as usize]) != len as usize {
                            return false;
                        }
                        self.transparent = if len == 2 {
                            values[1] as u32
                        } else {
                            (values[1] as u32) << 16 | (values[3] as u32) << 8 | values[5] as u32
                        };
                        self.alpha = true;
                    }
                }
                b"IEND" if self.mode == Mode::Bmp => return false,
                _ => {}
            }
            if next > source.len() {
                return false;
            }
            self.offset = next;
        }
    }

    fn input(&mut self, source: &mut impl Source) -> bool {
        if self.input_pos < self.input_end {
            return true;
        }
        if self.remaining == 0 && !self.chunk(source) {
            return false;
        }
        let count = (self.remaining as usize).min(self.workspace.input.len());
        let read = source.read(self.offset, &mut self.workspace.input[..count]);
        if read == 0 || read > count {
            return false;
        }
        self.offset += read as u32;
        self.remaining -= read as u32;
        if self.remaining == 0 {
            self.offset = match self.offset.checked_add(4) {
                Some(v) => v,
                None => return false,
            };
        }
        self.input_pos = 0;
        self.input_end = read;
        true
    }

    fn byte(&mut self, source: &mut impl Source) -> Option<u8> {
        if !self.input(source) {
            return None;
        }
        let value = self.workspace.input[self.input_pos];
        self.input_pos += 1;
        Some(value)
    }

    fn refill(&mut self, source: &mut impl Source) -> bool {
        if self.done {
            return false;
        }
        if self.mode == Mode::Bmp {
            self.zlib_header = true;
        }
        if !self.zlib_header {
            let Some(a) = self.byte(source) else {
                return false;
            };
            let Some(b) = self.byte(source) else {
                return false;
            };
            if a & 15 != 8
                || a >> 4 > 7
                || (u16::from(a) * 256 + u16::from(b)) % 31 != 0
                || b & 32 != 0
            {
                return false;
            }
            self.zlib_header = true;
        }
        loop {
            let available = self.input(source);
            let start = self.window_next;
            let workspace = &mut *self.workspace;
            let (status, used, wrote) = workspace.inflate.step(
                &workspace.input[self.input_pos..self.input_end],
                available,
                &mut self.window,
                start,
                true,
            );
            self.input_pos += used;
            if status < 0 {
                return false;
            }
            self.window_pos = start;
            self.window_end = start + wrote;
            self.window_next = self.window_end & (WINDOW - 1);
            self.done = status == 1;
            if wrote > 0 {
                return true;
            }
            if self.done || used == 0 {
                return false;
            }
        }
    }

    pub fn decode(
        &mut self,
        source: &mut impl Source,
        mut output: impl FnMut(Row<'_>, usize) -> bool,
    ) -> bool {
        let row_size = self.info.pitch + 1;
        while self.y < self.info.height {
            if self.window_pos == self.window_end {
                if self.mode == Mode::Frame {
                    source.progress();
                }
                if !self.refill(source) {
                    if self.mode == Mode::Bmp {
                        source.diagnostic(Diagnostic::ScanlineDecode, self.y, 0, 0, 0);
                    }
                    return false;
                }
            }
            let count = (row_size - self.row_pos).min(self.window_end - self.window_pos);
            self.rows[(self.y as usize % 2) * row_size + self.row_pos
                ..(self.y as usize % 2) * row_size + self.row_pos + count]
                .copy_from_slice(&self.window[self.window_pos..self.window_pos + count]);
            self.window_pos += count;
            self.row_pos += count;
            if self.row_pos != row_size {
                continue;
            }
            let (first, second) = self.rows.split_at_mut(row_size);
            let (current, previous) = if self.y.is_multiple_of(2) {
                (first, second)
            } else {
                (second, first)
            };
            let bpp = if self.info.depth < 8 {
                1
            } else {
                self.info.channels * self.info.depth as usize / 8
            };
            if !unfilter(current, previous, bpp, self.mode == Mode::Frame) {
                source.diagnostic(Diagnostic::Filter, u32::from(current[0]), 0, 0, 0);
                source.diagnostic(Diagnostic::ScanlineDecode, self.y, 0, 0, 0);
                return false;
            }
            let row = Row {
                x: 0,
                y: self.y as i32,
                width: self.info.width as i32,
                height: 1,
                stride: self.info.width as i32,
                pixels: &current[1..],
                palette: &self.workspace.palette,
                pixel_type: self.info.color,
                depth: self.info.depth,
                has_alpha: self.alpha,
                transparent: self.transparent,
            };
            if !output(row, self.palette_entries) {
                return false;
            }
            self.row_pos = 0;
            self.y += 1;
        }
        true
    }
}

fn unfilter(current: &mut [u8], previous: &[u8], bpp: usize, permissive: bool) -> bool {
    let filter = current[0];
    let current = &mut current[1..];
    let previous = &previous[1..];
    match filter {
        0 => {}
        1 => {
            for i in bpp..current.len() {
                current[i] = current[i].wrapping_add(current[i - bpp]);
            }
        }
        2 => {
            for (c, p) in current.iter_mut().zip(previous) {
                *c = c.wrapping_add(*p);
            }
        }
        3 => {
            let first = bpp.min(current.len());
            for (c, p) in current[..first].iter_mut().zip(previous) {
                *c = c.wrapping_add(*p / 2);
            }
            for i in first..current.len() {
                current[i] = current[i]
                    .wrapping_add(((current[i - bpp] as u16 + previous[i] as u16) / 2) as u8);
            }
        }
        4 if bpp == 1 => {
            let mut left = 0u8;
            let mut upper_left = 0u8;
            for (c, &up) in current.iter_mut().zip(previous) {
                left = c.wrapping_add(paeth(left, up, upper_left));
                *c = left;
                upper_left = up;
            }
        }
        4 => {
            let first = bpp.min(current.len());
            for (c, p) in current[..first].iter_mut().zip(previous) {
                *c = c.wrapping_add(*p);
            }
            for i in first..current.len() {
                current[i] = current[i].wrapping_add(paeth(
                    current[i - bpp],
                    previous[i],
                    previous[i - bpp],
                ));
            }
        }
        _ => return permissive,
    }
    true
}

#[inline]
fn paeth(a: u8, b: u8, c: u8) -> u8 {
    // Equivalent Paeth thresholds avoid three absolute values on RV32IMC.
    let threshold = 3 * c as i32 - (a as i32 + b as i32);
    let low = a.min(b);
    let high = a.max(b);
    if threshold >= high as i32 {
        low
    } else if threshold <= low as i32 {
        high
    } else {
        c
    }
}

pub fn sleep_header(header: &[u8]) -> bool {
    if header.len() < 29
        || &header[..8] != b"\x89PNG\r\n\x1a\n"
        || be32(&header[8..12]) != 13
        || &header[12..16] != b"IHDR"
    {
        return false;
    }
    let width = be32(&header[16..20]);
    let height = be32(&header[20..24]);
    let depth = header[24];
    let color = header[25];
    width > 0
        && height > 0
        && width <= 2048
        && height <= 3072
        && width * height <= 2048 * 1536
        && (depth == 8 || (matches!(color, 0 | 3) && matches!(depth, 1 | 2 | 4)))
        && matches!(color, 0 | 2 | 3 | 4 | 6)
        && header[26..29] == [0, 0, 0]
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Bytes<'a>(&'a [u8]);
    impl Source for Bytes<'_> {
        fn read(&mut self, offset: u32, out: &mut [u8]) -> usize {
            let data = self.0.get(offset as usize..).unwrap_or(&[]);
            let n = data.len().min(out.len());
            out[..n].copy_from_slice(&data[..n]);
            n
        }
        fn len(&self) -> u32 {
            self.0.len() as u32
        }
    }
    fn fixture(idat: &[u8]) -> Vec<u8> {
        let mut data = b"\x89PNG\r\n\x1a\n".to_vec();
        for (kind, bytes) in [
            (b"IHDR", &[0, 0, 0, 2, 0, 0, 0, 1, 8, 0, 0, 0, 0][..]),
            (b"IDAT", idat),
            (b"IEND", &[][..]),
        ] {
            data.extend_from_slice(&(bytes.len() as u32).to_be_bytes());
            data.extend_from_slice(kind);
            data.extend_from_slice(bytes);
            let crc = crate::crc32(&data[data.len() - bytes.len() - 4..]);
            data.extend_from_slice(&crc.to_be_bytes());
        }
        data
    }
    #[test]
    fn paeth_thresholds_match_all_byte_triples() {
        for a in 0u8..=255 {
            for b in 0u8..=255 {
                for c in 0u8..=255 {
                    let p = a as i32 + b as i32 - c as i32;
                    let pa = (p - a as i32).abs();
                    let pb = (p - b as i32).abs();
                    let pc = (p - c as i32).abs();
                    let expected = if pa <= pb && pa <= pc {
                        a
                    } else if pb <= pc {
                        b
                    } else {
                        c
                    };
                    assert_eq!(paeth(a, b, c), expected);
                }
            }
        }
    }
    #[test]
    fn filters_and_wrapping_addition() {
        for bpp in 1..=8 {
            for filter in 0..=4 {
                let mut current = [255u8; 33];
                current[0] = filter;
                let mut expected = current;
                let previous = [127; 33];
                for i in 1..33 {
                    let a = if i > bpp { expected[i - bpp] as i32 } else { 0 };
                    let b = previous[i] as i32;
                    let c = if i > bpp { previous[i - bpp] as i32 } else { 0 };
                    let p = a + b - c;
                    let pa = (p - a).abs();
                    let pb = (p - b).abs();
                    let pc = (p - c).abs();
                    let predict = match filter {
                        0 => 0,
                        1 => a,
                        2 => b,
                        3 => (a + b) / 2,
                        _ => {
                            if pa <= pb && pa <= pc {
                                a
                            } else if pb <= pc {
                                b
                            } else {
                                c
                            }
                        }
                    };
                    expected[i] = expected[i].wrapping_add(predict as u8);
                }
                assert!(unfilter(&mut current, &previous, bpp, false));
                assert_eq!(current, expected);
            }
        }
    }
    #[test]
    fn invalid_history_rejects_before_rows() {
        let bytes = fixture(&[0x78, 0x9c, 3, 2, 0, 0, 3, 0, 1]);
        let mut source = Bytes(&bytes);
        let info = info(&mut source, Mode::Frame).unwrap();
        let mut decoder = Decoder::new(info, Mode::Frame, &mut source).unwrap();
        let mut rows = 0;
        assert!(!decoder.decode(&mut source, |_, _| {
            rows += 1;
            true
        }));
        assert_eq!(rows, 0);
    }
    #[test]
    fn malformed_streams_terminate() {
        let mut seed = 27u32;
        let mut compressed = [0; 64];
        for _ in 0..5000 {
            for byte in &mut compressed {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                *byte = seed as u8;
            }
            compressed[0] = 0x78;
            compressed[1] = 0x9c;
            let bytes =
                fixture(&compressed[..(seed as usize % 63) + 2.min(64 - seed as usize % 63)]);
            let mut source = Bytes(&bytes);
            let info = info(&mut source, Mode::Frame).unwrap();
            let mut decoder = Decoder::new(info, Mode::Frame, &mut source).unwrap();
            decoder.decode(&mut source, |_, _| true);
        }
    }
}
