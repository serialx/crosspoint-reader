#![no_std]
//! First-frame GIF decoding. Storage and scheduling remain with the caller.
extern crate alloc;
use alloc::vec::Vec;
pub mod bmp;

pub const MAX_WIDTH: u16 = 3072;

pub trait Read {
    fn read(&mut self, offset: u32, output: &mut [u8]) -> usize;
    fn poll(&mut self) {}
}

#[repr(C)]
#[derive(Default, Clone, Copy, Debug, PartialEq, Eq)]
pub struct Info {
    pub width: u16,
    pub height: u16,
    pub x: u16,
    pub y: u16,
    pub frame_width: u16,
    pub frame_height: u16,
    pub interlaced: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Input,
    Format,
    Memory,
    Output,
}
type Result<T> = core::result::Result<T, Error>;

fn exact(input: &mut impl Read, offset: &mut u32, output: &mut [u8]) -> Result<()> {
    if input.read(*offset, output) != output.len() {
        return Err(Error::Input);
    }
    *offset = offset
        .checked_add(output.len() as u32)
        .ok_or(Error::Input)?;
    Ok(())
}
fn byte(input: &mut impl Read, offset: &mut u32) -> Result<u8> {
    let mut b = [0];
    exact(input, offset, &mut b)?;
    Ok(b[0])
}
fn skip_blocks(input: &mut impl Read, offset: &mut u32) -> Result<()> {
    loop {
        let len = byte(input, offset)?;
        if len == 0 {
            return Ok(());
        }
        *offset = offset.checked_add(len as u32).ok_or(Error::Input)?;
    }
}
fn le16(b: &[u8]) -> u16 {
    u16::from_le_bytes([b[0], b[1]])
}

struct Header {
    data: u32,
    palette: u32,
    colors: usize,
    transparent: Option<u8>,
    code_size: u8,
    supported_extensions: bool,
}
fn header(input: &mut impl Read, info: &mut Info) -> Result<Header> {
    *info = Info::default();
    let mut offset = 0;
    let mut head = [0; 13];
    exact(input, &mut offset, &mut head)?;
    if &head[..6] != b"GIF87a" && &head[..6] != b"GIF89a" {
        return Err(Error::Format);
    }
    info.width = le16(&head[6..]);
    info.height = le16(&head[8..]);
    let mut palette = 13;
    let mut colors = if head[10] & 0x80 != 0 {
        2 << (head[10] & 7)
    } else {
        0
    };
    offset += colors as u32 * 3;
    let mut transparent = None;
    let mut supported_extensions = true;
    loop {
        match byte(input, &mut offset)? {
            0x21 => {
                let label = byte(input, &mut offset)?;
                if ![1, 0xf9, 0xfe, 0xff].contains(&label) {
                    supported_extensions = false;
                }
                let start = offset;
                if label == 0xf9 {
                    let mut control = [0; 5];
                    if input.read(start, &mut control) == 5 && control[0] == 4 {
                        transparent = (control[1] & 1 != 0).then_some(control[4]);
                    } else {
                        supported_extensions = false;
                    }
                }
                skip_blocks(input, &mut offset)?;
            }
            0x2c => {
                let mut desc = [0; 9];
                exact(input, &mut offset, &mut desc)?;
                info.x = le16(&desc);
                info.y = le16(&desc[2..]);
                info.frame_width = le16(&desc[4..]);
                info.frame_height = le16(&desc[6..]);
                info.interlaced = u8::from(desc[8] & 0x40 != 0);
                if desc[8] & 0x80 != 0 {
                    colors = 2 << (desc[8] & 7);
                    palette = offset;
                    offset += colors as u32 * 3;
                }
                let code_size = byte(input, &mut offset)?;
                if !(2..=8).contains(&code_size) {
                    return Err(Error::Format);
                }
                return Ok(Header {
                    data: offset,
                    palette,
                    colors,
                    transparent,
                    code_size,
                    supported_extensions,
                });
            }
            _ => return Err(Error::Format),
        }
    }
}

pub fn inspect(input: &mut impl Read, info: &mut Info) -> bool {
    header(input, info).is_ok()
}

pub(crate) fn zeros<T: Default + Clone>(count: usize) -> Result<Vec<T>> {
    let mut v = Vec::new();
    v.try_reserve_exact(count).map_err(|_| Error::Memory)?;
    v.resize(count, T::default());
    Ok(v)
}

struct Codes<'a, R> {
    input: &'a mut R,
    buffer: &'a mut [u8],
    offset: u32,
    at: usize,
    end: usize,
    block_left: usize,
    plain: &'a mut [u8],
    plain_end: usize,
    bit: usize,
    ended: bool,
}
impl<R: Read> Codes<'_, R> {
    #[inline(always)]
    fn raw(&mut self) -> Result<u8> {
        if self.at == self.end {
            self.end = self.input.read(self.offset, self.buffer);
            if self.end == 0 || self.end > self.buffer.len() {
                return Err(Error::Input);
            }
            self.offset = self
                .offset
                .checked_add(self.end as u32)
                .ok_or(Error::Input)?;
            self.at = 0;
        }
        let result = self.buffer[self.at];
        self.at += 1;
        Ok(result)
    }
    #[inline(never)]
    fn refill(&mut self) -> Result<()> {
        let consumed = self.bit / 8;
        self.plain.copy_within(consumed..self.plain_end, 0);
        self.plain_end -= consumed;
        self.bit &= 7;
        while self.plain_end < self.plain.len() - core::mem::size_of::<usize>() && !self.ended {
            if self.block_left == 0 {
                self.block_left = match self.raw() {
                    Ok(byte) => byte as usize,
                    Err(_) => {
                        self.ended = true;
                        break;
                    }
                };
                if self.block_left == 0 {
                    self.ended = true;
                    break;
                }
            }
            if self.at == self.end {
                let first = match self.raw() {
                    Ok(byte) => byte,
                    Err(_) => {
                        self.ended = true;
                        break;
                    }
                };
                self.plain[self.plain_end] = first;
                self.plain_end += 1;
                self.block_left -= 1;
            } else {
                let count = self
                    .block_left
                    .min(self.end - self.at)
                    .min(self.plain.len() - core::mem::size_of::<usize>() - self.plain_end);
                self.plain[self.plain_end..self.plain_end + count]
                    .copy_from_slice(&self.buffer[self.at..self.at + count]);
                self.plain_end += count;
                self.at += count;
                self.block_left -= count;
            }
        }
        Ok(())
    }
    #[inline(always)]
    fn next(&mut self, size: u8) -> Result<u16> {
        if self.bit + size as usize > self.plain_end * 8 {
            self.refill()?;
            if self.bit + size as usize > self.plain_end * 8 {
                return Err(Error::Input);
            }
        }
        let offset = self.bit / 8;
        // The dechunk buffer has a native-word guard after its valid bytes.
        // Available bits were checked above; the unused high bits are masked.
        let word =
            unsafe { core::ptr::read_unaligned(self.plain.as_ptr().add(offset).cast::<usize>()) }
                .to_le();
        let code = ((word >> (self.bit & 7)) & ((1 << size) - 1)) as u16;
        self.bit += size as usize;
        Ok(code)
    }
}

/// The row and palette are borrowed only for the synchronous call. No per-row allocation.
pub trait Rows {
    fn row(
        &mut self,
        info: Info,
        y: u16,
        pixels: &[u8],
        gray: &[u8],
        transparent: Option<u8>,
    ) -> bool;
}

pub fn decode(input: &mut impl Read, sink: &mut impl Rows) -> Result<()> {
    let mut info = Info::default();
    let h = header(input, &mut info)?;
    if !h.supported_extensions
        || info.width == 0
        || info.height == 0
        || info.width > MAX_WIDTH
        || info.height > 32767
        || info.frame_width as u32 + info.x as u32 > info.width as u32
        || info.frame_height as u32 + info.y as u32 > info.height as u32
    {
        return Err(Error::Format);
    }
    // Prefix links, suffix bytes and expansion stack are bounded by the 12-bit GIF dictionary.
    let mut tables = zeros::<u16>(8192)?;
    let (prefix, suffix) = tables.split_at_mut(4096);
    let mut bytes = zeros::<u8>(4104 + 1024 + 1032 + 256 + info.frame_width as usize)?;
    let (stack, bytes) = bytes.split_at_mut(4104);
    let (buffer, bytes) = bytes.split_at_mut(1024);
    let (plain, bytes) = bytes.split_at_mut(1032);
    let (palette, line) = bytes.split_at_mut(256);
    let mut offset = h.palette;
    exact(input, &mut offset, &mut buffer[..h.colors * 3])?;
    for (gray, rgb) in palette[..h.colors]
        .iter_mut()
        .zip(buffer[..h.colors * 3].as_chunks::<3>().0.iter())
    {
        *gray = ((rgb[0] as u32 * 77 + rgb[1] as u32 * 150 + rgb[2] as u32 * 29) >> 8) as u8;
    }
    let mut codes = Codes {
        input,
        buffer,
        offset: h.data,
        at: 0,
        end: 0,
        block_left: 0,
        plain,
        plain_end: 0,
        bit: 0,
        ended: false,
    };
    let clear = 1u16 << h.code_size;
    for (i, b) in suffix[..clear as usize].iter_mut().enumerate() {
        *b = i as u16;
        prefix[i] = 4096;
    }
    let mut size = h.code_size + 1;
    let mut next = clear + 2;
    let mut previous = None;
    let mut first = 0u8;
    let mut x = 0;
    let mut y = 0;
    while y < info.frame_height {
        let code = codes.next(size)?;
        if code == clear {
            size = h.code_size + 1;
            next = clear + 2;
            previous = None;
            continue;
        }
        if code == clear + 1 {
            return Ok(());
        }
        let mut current = code;
        let mut start = 4096;
        if code == next && previous.is_some() {
            start -= 1;
            stack[start] = first;
            current = previous.unwrap_or(0);
        } else if code >= next {
            return Err(Error::Format);
        }
        while current < 4096 {
            // Prefix links only refer to earlier codes (or the 4096 sentinel).
            // Each entry stores one or two tail bytes; at most 4096 bytes expand.
            unsafe {
                let link = *prefix.get_unchecked(current as usize);
                let tail = *suffix.get_unchecked(current as usize);
                start -= 1;
                *stack.get_unchecked_mut(start) = tail as u8;
                if link & 0x8000 != 0 {
                    start -= 1;
                    *stack.get_unchecked_mut(start) = (tail >> 8) as u8;
                }
                current = link & 0x1fff;
            }
        }
        first = stack[start];
        if let Some(old) = previous {
            if next < 4096 {
                let link = prefix[old as usize];
                if link & 0x8000 == 0 {
                    prefix[next as usize] = link | 0x8000;
                    suffix[next as usize] = (suffix[old as usize] << 8) | first as u16;
                } else {
                    prefix[next as usize] = old;
                    suffix[next as usize] = first as u16;
                }
                next += 1;
                if next == 1 << size && size < 12 {
                    size += 1;
                }
            }
        }
        previous = Some(code);
        while start < 4096 && y < info.frame_height {
            let take = (line.len() - x).min(4096 - start);
            const WORD: usize = core::mem::size_of::<usize>();
            // Extra pixels from short word copies are overwritten by later codes.
            // The stack has tail padding; the copy cannot cross a row boundary.
            if take <= WORD && x + WORD <= line.len() {
                line[x..x + WORD].copy_from_slice(&stack[start..start + WORD]);
            } else {
                line[x..x + take].copy_from_slice(&stack[start..start + take]);
            }
            x += take;
            start += take;
            if x == line.len() {
                let mut row_y = y;
                if info.interlaced != 0 {
                    let last = info.frame_height - 1;
                    row_y = if y > last / 2 {
                        y * 2 - (last | 1)
                    } else if y > last / 4 {
                        y * 4 - ((last & !1) | 2)
                    } else if y > last / 8 {
                        y * 8 - ((last & !3) | 4)
                    } else {
                        y * 8
                    };
                }
                if !sink.row(info, row_y, line, palette, h.transparent) {
                    return Err(Error::Output);
                }
                y += 1;
                if y % 32 == 0 {
                    codes.input.poll();
                }
                x = 0;
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Memory<'a>(&'a [u8]);
    impl Read for Memory<'_> {
        fn read(&mut self, offset: u32, output: &mut [u8]) -> usize {
            let Some(input) = self.0.get(offset as usize..) else {
                return 0;
            };
            let count = output.len().min(input.len());
            output[..count].copy_from_slice(&input[..count]);
            count
        }
    }
    #[derive(Default)]
    struct Pixels {
        values: Vec<u8>,
        rows: usize,
    }
    impl Rows for Pixels {
        fn row(&mut self, _: Info, _: u16, pixels: &[u8], _: &[u8], _: Option<u8>) -> bool {
            self.values.extend_from_slice(pixels);
            self.rows += 1;
            true
        }
    }
    fn small() -> Vec<u8> {
        let hex = b"474946383761010001008100000000000000000000000000002c000000000100010000080400010404003b";
        hex.as_chunks::<2>()
            .0
            .iter()
            .map(|p| {
                let digit = |b| match b {
                    b'0'..=b'9' => b - b'0',
                    _ => b - b'a' + 10,
                };
                digit(p[0]) * 16 + digit(p[1])
            })
            .collect()
    }
    #[test]
    fn first_frame_and_header() {
        let bytes = small();
        let mut input = Memory(&bytes);
        let mut info = Info::default();
        assert!(inspect(&mut input, &mut info));
        assert_eq!((info.width, info.height), (1, 1));
        let mut pixels = Pixels::default();
        assert_eq!(decode(&mut input, &mut pixels), Ok(()));
        assert_eq!(pixels.values, [0]);
    }
    #[test]
    fn every_truncation_and_bit_flip_terminates() {
        let original = small();
        for len in 0..original.len() {
            let _ = decode(&mut Memory(&original[..len]), &mut Pixels::default());
        }
        for at in 0..original.len() {
            for bit in 0..8 {
                let mut bytes = original.clone();
                bytes[at] ^= 1 << bit;
                let _ = decode(&mut Memory(&bytes), &mut Pixels::default());
            }
        }
    }
    #[test]
    fn rejects_code_size_before_shifting() {
        for code in 0..=255 {
            let mut bytes = small();
            bytes[35] = code;
            let result = inspect(&mut Memory(&bytes), &mut Info::default());
            assert_eq!(result, (2..=8).contains(&code));
        }
    }
}
