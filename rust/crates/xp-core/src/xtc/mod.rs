//! Allocation-free XTC byte interpretation. File I/O and owned strings stay with the caller.

pub const OK: i32 = 0;
pub const INVALID_MAGIC: i32 = 2;
pub const INVALID_VERSION: i32 = 3;
pub const CORRUPTED_HEADER: i32 = 4;
pub const READ_ERROR: i32 = 6;
pub const HEADER_BYTES: usize = 56;
pub const ENTRY_BYTES: usize = 16;
pub const PAGE_BYTES: usize = 22;
pub const CHAPTER_BYTES: usize = 96;

#[repr(C, packed)]
#[derive(Clone, Copy, Default)]
pub struct Header {
    pub magic: u32,
    pub version_major: u8,
    pub version_minor: u8,
    pub page_count: u16,
    pub read_direction: u8,
    pub has_metadata: u8,
    pub has_thumbnails: u8,
    pub has_chapters: u8,
    pub current_page: u32,
    pub metadata_offset: u64,
    pub page_table_offset: u64,
    pub data_offset: u64,
    pub thumb_offset: u64,
    pub chapter_offset: u32,
    pub padding: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Entry {
    pub offset: u64,
    pub size: u32,
    pub width: u16,
    pub height: u16,
}

#[repr(u16)]
#[derive(Clone, Copy, Default)]
pub enum ChapterAction {
    #[default]
    Skip,
    Emit,
    Stop,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Chapter {
    pub name_len: u16,
    pub start: u16,
    pub end: u16,
    pub action: ChapterAction,
}

fn u16_at(bytes: &[u8], at: usize) -> u16 {
    u16::from_le_bytes([bytes[at], bytes[at + 1]])
}
fn u32_at(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]])
}
fn u64_at(bytes: &[u8], at: usize) -> u64 {
    u64::from(u32_at(bytes, at)) | (u64::from(u32_at(bytes, at + 4)) << 32)
}

pub fn bit_depth(bytes: &[u8]) -> u8 {
    if bytes.len() < 4 {
        return 0;
    }
    match u32_at(bytes, 0) {
        0x00435458 => 1,
        0x48435458 => 2,
        _ => 0,
    }
}

pub fn header(bytes: &[u8]) -> Result<Header, i32> {
    if bytes.len() < HEADER_BYTES {
        return Err(READ_ERROR);
    }
    if bit_depth(bytes) == 0 {
        return Err(INVALID_MAGIC);
    }
    if !matches!((bytes[4], bytes[5]), (1, 0) | (0, 1)) {
        return Err(INVALID_VERSION);
    }
    if u16_at(bytes, 6) == 0 {
        return Err(CORRUPTED_HEADER);
    }
    Ok(Header {
        magic: u32_at(bytes, 0),
        version_major: bytes[4],
        version_minor: bytes[5],
        page_count: u16_at(bytes, 6),
        read_direction: bytes[8],
        has_metadata: bytes[9],
        has_thumbnails: bytes[10],
        has_chapters: bytes[11],
        current_page: u32_at(bytes, 12),
        metadata_offset: u64_at(bytes, 16),
        page_table_offset: u64_at(bytes, 24),
        data_offset: u64_at(bytes, 32),
        thumb_offset: u64_at(bytes, 40),
        chapter_offset: u32_at(bytes, 48),
        padding: u32_at(bytes, 52),
    })
}

pub fn table_valid(header: &Header, file_size: u64) -> bool {
    let offset = header.page_table_offset;
    offset >= 48 && offset <= file_size && u64::from(header.page_count) * 16 <= file_size - offset
}

pub fn chapter_flag(flag: u8) -> bool {
    flag == 1
}

pub fn has_chapters(header: &Header) -> bool {
    chapter_flag(header.has_chapters) && header.page_table_offset >= HEADER_BYTES as u64
}

pub fn entry(bytes: &[u8]) -> Result<Entry, i32> {
    if bytes.len() < ENTRY_BYTES {
        return Err(READ_ERROR);
    }
    Ok(Entry {
        offset: u64_at(bytes, 0),
        size: u32_at(bytes, 8),
        width: u16_at(bytes, 12),
        height: u16_at(bytes, 14),
    })
}

pub fn bitmap_size(width: u16, height: u16, depth: u8) -> u32 {
    if depth == 2 {
        u32::from(width) * u32::from(height).div_ceil(8) * 2
    } else {
        u32::from(width).div_ceil(8) * u32::from(height)
    }
}

pub fn page(bytes: &[u8], depth: u8, streaming: bool) -> Result<u32, i32> {
    if bytes.len() < PAGE_BYTES {
        return Err(READ_ERROR);
    }
    let magic = if depth == 2 { 0x00485458 } else { 0x00475458 };
    if u32_at(bytes, 0) != magic {
        return Err(if streaming { READ_ERROR } else { INVALID_MAGIC });
    }
    let size = bitmap_size(u16_at(bytes, 4), u16_at(bytes, 6), depth);
    if size != u32_at(bytes, 10) {
        return Err(CORRUPTED_HEADER);
    }
    Ok(size)
}

pub fn metadata_offset(author: bool) -> u32 {
    if author {
        0xb8
    } else {
        0x38
    }
}

pub fn text_len(bytes: &[u8]) -> usize {
    bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len())
}

/// The legacy reader treats chapterOffset plus padding as one little-endian u64.
pub fn chapter_span(header: &Header, bytes: &[u8], file_size: u64) -> (u64, u64) {
    if bytes.len() < 8 {
        return (0, 0);
    }
    let offset = u64_at(bytes, 0);
    if offset < HEADER_BYTES as u64
        || offset >= file_size
        || file_size - offset < CHAPTER_BYTES as u64
    {
        return (0, 0);
    }
    let end = if header.page_table_offset > offset && header.page_table_offset <= file_size {
        header.page_table_offset
    } else if header.data_offset > offset && header.data_offset <= file_size {
        header.data_offset
    } else {
        file_size
    };
    (offset, (end - offset) / CHAPTER_BYTES as u64)
}

pub fn chapter(bytes: &[u8], page_count: u16) -> Result<Chapter, i32> {
    if bytes.len() < CHAPTER_BYTES {
        return Err(READ_ERROR);
    }
    let name_len = text_len(&bytes[..80]) as u16;
    let start = u16_at(bytes, 80);
    let end = u16_at(bytes, 82);
    if name_len == 0 && start == 0 && end == 0 {
        return Ok(Chapter {
            action: ChapterAction::Stop,
            ..Chapter::default()
        });
    }
    let start = start.saturating_sub(1);
    let end = end.saturating_sub(1).min(page_count.saturating_sub(1));
    Ok(Chapter {
        name_len,
        start,
        end,
        action: if start < page_count && start <= end {
            ChapterAction::Emit
        } else {
            ChapterAction::Skip
        },
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_short_records_fail_without_panicking() {
        let bytes = [0u8; CHAPTER_BYTES];
        for len in 0..HEADER_BYTES {
            assert_eq!(header(&bytes[..len]).err(), Some(READ_ERROR));
        }
        for len in 0..ENTRY_BYTES {
            assert_eq!(entry(&bytes[..len]).err(), Some(READ_ERROR));
        }
        for len in 0..PAGE_BYTES {
            assert_eq!(page(&bytes[..len], 1, false), Err(READ_ERROR));
        }
        for len in 0..CHAPTER_BYTES {
            assert_eq!(chapter(&bytes[..len], 1).err(), Some(READ_ERROR));
        }
    }
    #[test]
    fn dimensions_fit_u32() {
        assert_eq!(bitmap_size(u16::MAX, u16::MAX, 2), 1_073_725_440);
        assert_eq!(bitmap_size(u16::MAX, u16::MAX, 1), 536_862_720);
    }
    #[test]
    fn chapter_offsets_do_not_wrap() {
        let header = Header::default();
        assert_eq!(
            chapter_span(&header, &u64::MAX.to_le_bytes(), u64::MAX),
            (0, 0)
        );
        assert_eq!(
            chapter_span(&header, &(u64::MAX - 2).to_le_bytes(), u64::MAX),
            (0, 0)
        );
    }
}
