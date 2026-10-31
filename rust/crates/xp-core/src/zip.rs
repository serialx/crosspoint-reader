//! Allocation-free ZIP32 directory parsing. I/O and retained names belong to the caller.

const CENTRAL_BYTES: u32 = 46;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ReadAt {
    pub offset: u32,
    pub length: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Details {
    pub central_offset: u32,
    pub entries: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Entry {
    pub name: ReadAt,
    pub next_offset: u32,
    pub crc32: u32,
    pub compressed_size: u32,
    pub uncompressed_size: u32,
    pub local_offset: u32,
    pub method: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Walker {
    current: u32,
    start: u32,
    central: u32,
    wrapped: u32,
}

pub enum Step {
    Entry(Entry),
    Read(ReadAt),
    Done,
}

fn le16(bytes: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([bytes[at], bytes[at + 1]])
}

fn le32(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]])
}

pub fn eocd_request(size: u32) -> Option<ReadAt> {
    if size < 22 {
        return None;
    }
    let length = size.min(1024);
    Some(ReadAt {
        offset: size - length,
        length,
    })
}

pub fn eocd(bytes: &[u8]) -> Option<Details> {
    if !(22..=1024).contains(&bytes.len()) {
        return None;
    }
    for at in (0..=bytes.len() - 22).rev() {
        if le32(bytes, at) == 0x0605_4b50 {
            return Some(Details {
                central_offset: le32(bytes, at + 16),
                entries: le16(bytes, at + 10),
            });
        }
    }
    None
}

impl Walker {
    pub fn new(central: u32, start: u32) -> Self {
        Self {
            current: start,
            start,
            central,
            wrapped: 0,
        }
    }

    pub fn request(&self) -> Option<ReadAt> {
        if self.wrapped != 0 && self.current >= self.start {
            None
        } else {
            Some(ReadAt {
                offset: self.current,
                length: CENTRAL_BYTES,
            })
        }
    }

    pub fn advance(&mut self, header: &[u8]) -> Option<Step> {
        self.request()?;
        if header.len() != CENTRAL_BYTES as usize || le32(header, 0) != 0x0201_4b50 {
            if self.wrapped == 0 && self.start != self.central {
                self.current = self.central;
                self.wrapped = 1;
                return Some(self.request().map_or(Step::Done, Step::Read));
            }
            return Some(Step::Done);
        }
        let name_offset = self.current.checked_add(CENTRAL_BYTES)?;
        let name_length = u32::from(le16(header, 28));
        let next_offset = name_offset
            .checked_add(name_length)?
            .checked_add(u32::from(le16(header, 30)))?
            .checked_add(u32::from(le16(header, 32)))?;
        let entry = Entry {
            name: ReadAt {
                offset: name_offset,
                length: name_length,
            },
            next_offset,
            crc32: le32(header, 16),
            compressed_size: le32(header, 20),
            uncompressed_size: le32(header, 24),
            local_offset: le32(header, 42),
            method: le16(header, 10),
        };
        self.current = next_offset;
        Some(Step::Entry(entry))
    }
}

pub fn local_request(offset: u32) -> ReadAt {
    ReadAt { offset, length: 30 }
}

pub fn data_offset(offset: u32, header: &[u8]) -> Option<u64> {
    if header.len() != 30 || le32(header, 0) != 0x0403_4b50 {
        return None;
    }
    Some(u64::from(offset) + 30 + u64::from(le16(header, 26)) + u64::from(le16(header, 28)))
}

pub fn name_matches(name: &[u8], target: &[u8]) -> bool {
    name.len() < 256 && name == target
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn eocd_window_and_last_signature() {
        assert_eq!(eocd_request(21), None);
        assert_eq!(
            eocd_request(2000),
            Some(ReadAt {
                offset: 976,
                length: 1024
            })
        );
        let mut bytes = [0; 1024];
        bytes[..4].copy_from_slice(&0x0605_4b50u32.to_le_bytes());
        bytes[10] = 3;
        assert_eq!(eocd(&bytes).unwrap().entries, 3);
        bytes[1002..1006].copy_from_slice(&0x0605_4b50u32.to_le_bytes());
        bytes[1012] = 7;
        assert_eq!(eocd(&bytes).unwrap().entries, 7);
        assert_eq!(eocd(&bytes[..21]), None);
    }

    #[test]
    fn walker_wraps_once_and_checks_overflow() {
        let mut walker = Walker::new(10, 100);
        assert!(matches!(
            walker.advance(&[]),
            Some(Step::Read(ReadAt { offset: 10, .. }))
        ));
        let mut header = [0; 46];
        header[..4].copy_from_slice(&0x0201_4b50u32.to_le_bytes());
        header[28] = 50;
        let Some(Step::Entry(entry)) = walker.advance(&header) else {
            panic!()
        };
        assert_eq!(entry.next_offset, 106);
        assert_eq!(walker.request(), None);
        assert!(Walker::new(u32::MAX - 45, u32::MAX - 45)
            .advance(&header)
            .is_none());
        for length in 0..46 {
            assert!(matches!(
                Walker::new(0, 0).advance(&header[..length]),
                Some(Step::Done)
            ));
        }
    }

    #[test]
    fn local_header_and_raw_names() {
        let mut header = [0; 30];
        header[..4].copy_from_slice(&0x0403_4b50u32.to_le_bytes());
        header[26..30].copy_from_slice(&[255; 4]);
        assert_eq!(
            data_offset(u32::MAX, &header),
            Some(u64::from(u32::MAX) + 131100)
        );
        for length in 0..30 {
            assert_eq!(data_offset(0, &header[..length]), None);
        }
        assert!(name_matches(b"a/../b", b"a/../b"));
        assert!(!name_matches(b"./a", b"a"));
        assert!(name_matches(&[b'x'; 255], &[b'x'; 255]));
        assert!(!name_matches(&[b'x'; 256], &[b'x'; 256]));
    }
}
