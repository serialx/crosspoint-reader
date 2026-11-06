use super::{
    io::{File, Io},
    Store,
};

struct Reader<'a, F> {
    file: &'a mut F,
    remaining: usize,
    bytes: [u8; 64],
    position: usize,
    size: usize,
}
impl<F: File> Reader<'_, F> {
    fn next(&mut self) -> Option<u8> {
        if self.position == self.size {
            if self.remaining == 0 {
                return None;
            }
            self.size = self.remaining.min(self.bytes.len());
            if self.file.read(&mut self.bytes[..self.size]) != self.size as i32 {
                return None;
            }
            self.remaining -= self.size;
            self.position = 0;
        }
        let byte = self.bytes[self.position];
        self.position += 1;
        Some(byte)
    }
}

fn read(file: &mut impl File, remaining: usize, out: &mut [u8]) -> Option<usize> {
    if out.len() < 260 {
        return None;
    }
    out[0] = 0;
    let mut reader = Reader {
        file,
        remaining,
        bytes: [0; 64],
        position: 0,
        size: 0,
    };
    let mut length = 0;
    let mut prefix = 0;
    let mut space_pending = false;
    while reader.remaining > 0 || reader.position < reader.size {
        let first = reader.next()?;
        let count = match first {
            0..=0x7f => 1,
            0xc2..=0xdf => 2,
            0xe0..=0xef => 3,
            0xf0..=0xf4 => 4,
            _ => return None,
        };
        let mut point = [first, 0, 0, 0];
        for byte in &mut point[1..count] {
            *byte = reader.next()?;
            if *byte & 0xc0 != 0x80 {
                return None;
            }
        }
        let space = (count == 1 && matches!(first, b' ' | b'\r' | b'\n' | b'\t'))
            || point[..count] == [0xc2, 0xa0]
            || point[..count] == [0xe2, 0x80, 0x83]
            || point[..count] == [0xe2, 0x80, 0xaf];
        if !space || length > 0 {
            if prefix + count > 256 {
                out[length..length + 3].copy_from_slice(&[0xe2, 0x80, 0xa6]);
                length += 3;
                out[length] = 0;
                return Some(length);
            }
            prefix += count;
        }
        if space {
            space_pending = length > 0;
            continue;
        }
        if space_pending {
            out[length] = b' ';
            length += 1;
        }
        out[length..length + count].copy_from_slice(&point[..count]);
        length += count;
        space_pending = false;
    }
    out[length] = 0;
    Some(length)
}

impl<I: Io, const B: usize> Store<I, B> {
    pub fn preview(&self, index: usize, out: &mut [u8], length: &mut usize) -> bool {
        *length = 0;
        if let Some(first) = out.first_mut() {
            *first = 0;
        }
        let Some(row) = self.row(index) else {
            return false;
        };
        if out.is_empty() || self.path.is_empty() {
            return false;
        }
        if row.text_length == 0 {
            return true;
        }
        let Some(mut file) = self.io.open_read(&self.path) else {
            return false;
        };
        if !file.seek(row.text_offset) {
            return false;
        }
        match read(&mut file, row.text_length as usize, out) {
            Some(count) => {
                *length = count;
                true
            }
            None => {
                out[0] = 0;
                false
            }
        }
    }
}
