// Modified miniz_oxide 0.9.1; see ../../README.md and retained license notices.
use super::core::transfer;

pub const WINDOW_SEGMENT_BYTES: usize = 8192;
pub const WINDOW_SEGMENTS: usize = 4;
const WINDOW_BYTES: usize = WINDOW_SEGMENT_BYTES * WINDOW_SEGMENTS;

pub(super) trait Output {
    const FAST: bool;
    fn len(&self) -> usize;
    fn write_byte(&mut self, pos: usize, byte: u8);
    fn region(&mut self, pos: usize, len: usize) -> &mut [u8];
    fn copy_match(&mut self, src: usize, dest: usize, len: usize, mask: usize);
}

impl Output for &mut [u8] {
    const FAST: bool = true;

    #[inline(always)]
    fn len(&self) -> usize {
        (**self).len()
    }

    #[inline(always)]
    fn write_byte(&mut self, pos: usize, byte: u8) {
        self[pos] = byte;
    }

    #[inline(always)]
    fn region(&mut self, pos: usize, len: usize) -> &mut [u8] {
        &mut self[pos..pos + len]
    }

    #[inline(always)]
    fn copy_match(&mut self, src: usize, dest: usize, len: usize, mask: usize) {
        transfer(self, src, dest, len, mask);
    }
}

pub(super) struct Segmented<'a>(pub [&'a mut [u8; WINDOW_SEGMENT_BYTES]; WINDOW_SEGMENTS]);

impl Output for Segmented<'_> {
    const FAST: bool = false;

    #[inline(always)]
    fn len(&self) -> usize {
        WINDOW_BYTES
    }

    #[inline(always)]
    fn write_byte(&mut self, pos: usize, byte: u8) {
        self.0[pos / WINDOW_SEGMENT_BYTES][pos % WINDOW_SEGMENT_BYTES] = byte;
    }

    #[inline(always)]
    fn region(&mut self, pos: usize, len: usize) -> &mut [u8] {
        let offset = pos % WINDOW_SEGMENT_BYTES;
        &mut self.0[pos / WINDOW_SEGMENT_BYTES][offset..offset + len]
    }

    fn copy_match(&mut self, mut src: usize, mut dest: usize, len: usize, mask: usize) {
        for _ in 0..len {
            let byte = self.0[src / WINDOW_SEGMENT_BYTES][src % WINDOW_SEGMENT_BYTES];
            self.0[dest / WINDOW_SEGMENT_BYTES][dest % WINDOW_SEGMENT_BYTES] = byte;
            src = (src + 1) & mask;
            dest += 1;
        }
    }
}

pub(super) struct OutputBuffer<O> {
    pub output: O,
    position: usize,
    max: usize,
}

impl<O: Output> OutputBuffer<O> {
    #[inline]
    pub fn new(output: O, position: usize, max_count: usize) -> Self {
        let max = position.saturating_add(max_count).min(output.len());
        Self {
            output,
            position,
            max,
        }
    }

    #[inline(always)]
    pub const fn position(&self) -> usize {
        self.position
    }

    #[inline(always)]
    pub fn set_position(&mut self, position: usize) {
        self.position = position;
    }

    #[inline(always)]
    pub fn write_byte(&mut self, byte: u8) {
        self.output.write_byte(self.position, byte);
        self.position += 1;
    }

    #[inline]
    pub fn write_slice(&mut self, data: &[u8]) {
        self.output
            .region(self.position, data.len())
            .copy_from_slice(data);
        self.position += data.len();
    }

    #[inline]
    pub const fn bytes_left(&self) -> usize {
        self.max - self.position
    }
}

/// A wrapper for the output slice used when decompressing.
///
/// Using this rather than `Cursor` lets us implement the writing methods directly on
/// the buffer and lets us use a usize rather than u64 for the position which helps with
/// performance on 32-bit systems.
#[derive(Copy, Clone)]
pub struct InputWrapper<'a> {
    slice: &'a [u8],
}

impl<'a> InputWrapper<'a> {
    #[inline(always)]
    pub const fn as_slice(&self) -> &[u8] {
        self.slice
    }

    #[inline(always)]
    pub const fn from_slice(slice: &'a [u8]) -> InputWrapper<'a> {
        InputWrapper { slice }
    }

    #[inline(always)]
    pub fn advance(&mut self, steps: usize) {
        self.slice = &self.slice[steps..];
    }

    #[inline]
    pub fn read_byte(&mut self) -> Option<u8> {
        self.slice.first().map(|n| {
            self.advance(1);
            *n
        })
    }

    #[inline]
    #[cfg(target_pointer_width = "64")]
    pub fn read_u32_le(&mut self) -> u32 {
        let ret = {
            let four_bytes: [u8; 4] = self.slice[..4].try_into().unwrap_or_default();
            u32::from_le_bytes(four_bytes)
        };
        self.advance(4);
        ret
    }

    #[inline(always)]
    pub const fn bytes_left(&self) -> usize {
        self.slice.len()
    }
}
