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

fn exact(input: &mut dyn Read, offset: &mut u32, output: &mut [u8]) -> Result<()> {
    if input.read(*offset, output) != output.len() {
        return Err(Error::Input);
    }
    *offset = offset
        .checked_add(output.len() as u32)
        .ok_or(Error::Input)?;
    Ok(())
}
fn byte(input: &mut dyn Read, offset: &mut u32) -> Result<u8> {
    let mut b = [0];
    exact(input, offset, &mut b)?;
    Ok(b[0])
}
fn skip_blocks(input: &mut dyn Read, offset: &mut u32) -> Result<()> {
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
fn header(input: &mut dyn Read, info: &mut Info) -> Result<Header> {
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
                    offset = offset.checked_add(colors as u32 * 3).ok_or(Error::Input)?;
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

pub fn inspect(input: &mut dyn Read, info: &mut Info) -> bool {
    header(input, info).is_ok()
}

pub(crate) fn zeros<T: Default + Clone>(count: usize) -> Result<Vec<T>> {
    let mut v = Vec::new();
    v.try_reserve_exact(count).map_err(|_| Error::Memory)?;
    v.resize(count, T::default());
    Ok(v)
}

struct Codes<'a> {
    input: &'a mut dyn Read,
    buffer: &'a mut [u8],
    offset: u32,
    at: usize,
    end: usize,
    block_left: usize,
    plain: &'a mut [u8],
    plain_end: usize,
    plain_at: usize,
    word: usize,
    available: usize,
    ended: bool,
}
impl Codes<'_> {
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
        self.plain.copy_within(self.plain_at..self.plain_end, 0);
        self.plain_end -= self.plain_at;
        self.plain_at = 0;
        while self.plain_end < self.plain.len() && !self.ended {
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
                    .min(self.plain.len() - self.plain_end);
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
    fn next(&mut self, size: usize, mask: usize) -> Result<usize> {
        if self.available < size {
            const CHUNK: usize = core::mem::size_of::<usize>() / 2;
            if self.plain_end - self.plain_at < CHUNK && !self.ended {
                self.refill()?;
            }
            let remaining = self.plain_end - self.plain_at;
            if remaining >= CHUNK {
                // Half a native word fits alongside at most eleven remaining bits.
                // All loaded bytes are available; either byte alignment is allowed.
                let value = unsafe {
                    let ptr = self.plain.as_ptr().add(self.plain_at);
                    if CHUNK == 4 {
                        core::ptr::read_unaligned(ptr.cast::<u32>()).to_le() as usize
                    } else {
                        core::ptr::read_unaligned(ptr.cast::<u16>()).to_le() as usize
                    }
                };
                self.word |= value << self.available;
                self.available += CHUNK * 8;
                self.plain_at += CHUNK;
            } else {
                while self.plain_at < self.plain_end {
                    self.word |= (self.plain[self.plain_at] as usize) << self.available;
                    self.available += 8;
                    self.plain_at += 1;
                }
            }
            if self.available < size {
                return Err(Error::Input);
            }
        }
        let code = self.word & mask;
        self.word >>= size;
        self.available -= size;
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

// Entries pack a 12-bit length, an 8-bit tail and a 12-bit prefix.
// Direct row expansion avoids copying long strings through the scratch buffer.
#[inline(never)]
fn expand(mut current: usize, table: &[u32], output: &mut [u8]) {
    for pixel in output.iter_mut().rev() {
        // The caller validates the first code; every stored prefix is below 4096.
        let entry = unsafe { *table.get_unchecked(current) };
        *pixel = (entry >> 12) as u8;
        current = (entry & 0xfff) as usize;
    }
}

pub fn decode(input: &mut dyn Read, sink: &mut dyn Rows) -> Result<()> {
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
    let mut table = zeros::<u32>(4096)?;
    let mut bytes = zeros::<u8>(4096 + 1024 + 1024 + 256 + info.frame_width as usize)?;
    let (stack, bytes) = bytes.split_at_mut(4096);
    let (buffer, bytes) = bytes.split_at_mut(1024);
    let (plain, bytes) = bytes.split_at_mut(1024);
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
        plain_at: 0,
        word: 0,
        available: 0,
        ended: false,
    };
    let clear = 1usize << h.code_size;
    for (i, entry) in table[..clear].iter_mut().enumerate() {
        *entry = (1 << 20) | ((i as u32) << 12);
    }
    let mut size = h.code_size as usize + 1;
    let mut limit = clear * 2;
    let mut next = clear + 2;
    let mut previous = None;
    let mut previous_length = 0;
    let mut first = 0u8;
    let mut x = 0;
    let mut y = 0;
    while y < info.frame_height {
        let code = codes.next(size, limit - 1)?;
        if code == clear {
            size = h.code_size as usize + 1;
            limit = clear * 2;
            next = clear + 2;
            previous = None;
            previous_length = 0;
            continue;
        }
        if code == clear + 1 {
            return Ok(());
        }
        let special = code == next && previous.is_some();
        let current = if special {
            previous.unwrap_or(0)
        } else if code < next {
            code
        } else {
            return Err(Error::Format);
        };
        // Dictionary lengths grow by one and cannot exceed 4091 with a 12-bit table.
        let length = (table[current] >> 20) as usize + usize::from(special);
        let mut start = 4096 - length;
        let direct = length <= line.len() - x;
        let output = if direct {
            &mut line[x..x + length]
        } else {
            &mut stack[start..4096]
        };
        if special {
            output[length - 1] = first;
        }
        if current < clear {
            output[0] = current as u8;
        } else {
            expand(
                current,
                &table,
                &mut output[..length - usize::from(special)],
            );
        }
        first = output[0];
        if let Some(old) = previous {
            if next < 4096 {
                table[next] =
                    (((previous_length + 1) as u32) << 20) | ((first as u32) << 12) | old as u32;
                next += 1;
                if next == limit && size < 12 {
                    size += 1;
                    limit *= 2;
                }
            }
        }
        previous = Some(code);
        previous_length = length;
        while start < 4096 && y < info.frame_height {
            let take = (line.len() - x).min(4096 - start);
            if !direct {
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
        positions: Vec<u16>,
    }
    impl Rows for Pixels {
        fn row(&mut self, _: Info, y: u16, pixels: &[u8], _: &[u8], _: Option<u8>) -> bool {
            self.positions.push(y);
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
    fn interlaced_rows_keep_the_four_pass_order() {
        let mut bytes = Vec::from(&b"GIF89a\x02\x00\x08\x00\x81\x00\x00"[..]);
        bytes.extend_from_slice(&[0; 12]);
        bytes.extend_from_slice(b",\x00\x00\x00\x00\x02\x00\x08\x00\x40\x02");
        let mut data = Vec::new();
        let (mut bits, mut count) = (0u32, 0);
        for code in (0..16).flat_map(|i| [4u32, (i / 2) % 4]).chain([5]) {
            bits |= code << count;
            count += 3;
            while count >= 8 {
                data.push(bits as u8);
                bits >>= 8;
                count -= 8;
            }
        }
        if count != 0 {
            data.push(bits as u8);
        }
        bytes.push(data.len() as u8);
        bytes.extend_from_slice(&data);
        bytes.extend_from_slice(&[0, b';']);
        let mut pixels = Pixels::default();
        assert_eq!(decode(&mut Memory(&bytes), &mut pixels), Ok(()));
        assert_eq!(pixels.positions, [0, 4, 2, 6, 1, 3, 5, 7]);
        assert_eq!(
            pixels.values,
            [0, 0, 1, 1, 2, 2, 3, 3, 0, 0, 1, 1, 2, 2, 3, 3]
        );
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
