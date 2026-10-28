//! CLX1 v2 layout. Arithmetic intentionally matches the persisted C++ u32 format.
#[derive(Clone, Copy)]
#[repr(C, packed)]
pub struct Header {
    pub magic: [u8; 4],
    pub version: u8,
    pub fold: u8,
    pub flags: u8,
    pub metadata: u8,
    pub books: u16,
    pub folders: u16,
    pub next_seen: u16,
    pub padding: u16,
    pub folder_start: u32,
    pub folder_len: u32,
    pub record_start: u32,
    pub perm_start: u32,
    pub name_start: u32,
    pub name_len: u32,
    pub size: u32,
    pub reserved: [u8; 20],
}
impl Default for Header {
    fn default() -> Self {
        Self {
            magic: *b"CLX1",
            version: 2,
            fold: 4,
            flags: 0,
            metadata: 0,
            books: 0,
            folders: 0,
            next_seen: 0,
            padding: 0,
            folder_start: 0,
            folder_len: 0,
            record_start: 0,
            perm_start: 0,
            name_start: 0,
            name_len: 0,
            size: 0,
            reserved: [0; 20],
        }
    }
}
#[derive(Clone, Copy)]
#[repr(C, packed)]
pub struct Record {
    pub name_off: u32,
    pub size: u32,
    pub seen: u16,
    pub folder: u16,
    pub name_len: u8,
    pub fold_len: u8,
    pub author_len: u8,
    pub metadata: u8,
    pub fold: [u8; 96],
    pub author: [u8; 12],
    pub mtime: u32,
}
impl Default for Record {
    fn default() -> Self {
        Self {
            name_off: 0,
            size: 0,
            seen: 0,
            folder: 0,
            name_len: 0,
            fold_len: 0,
            author_len: 0,
            metadata: 0,
            fold: [0; 96],
            author: [0; 12],
            mtime: 0,
        }
    }
}
const _: () =
    assert!(core::mem::size_of::<Header>() == 64 && core::mem::size_of::<Record>() == 128);
pub fn align(value: u32) -> u32 {
    value.wrapping_add(511) / 512 * 512
}
pub fn layout(h: &mut Header, folders: u32, names: u32) {
    h.folder_start = 512;
    h.folder_len = folders;
    h.record_start = align(512u32.wrapping_add(folders));
    h.perm_start = align(h.record_start.wrapping_add(u32::from(h.books) * 128));
    h.name_start = align(h.perm_start.wrapping_add(u32::from(h.books) * 4));
    h.name_len = names;
    h.size = h.name_start.wrapping_add(names);
}
pub fn validate(h: &Header, size: u64, stale: bool) -> u8 {
    if h.magic != *b"CLX1" {
        return 1;
    }
    if h.version != 2 {
        return 2;
    }
    if h.books > 4096 {
        return 5;
    }
    if h.metadata > 1 {
        return 6;
    }
    if size != u64::from(h.size) {
        return 4;
    }
    if u64::from(h.folder_len) > size || u64::from(h.name_len) > size {
        return 6;
    }
    let mut e = *h;
    layout(&mut e, h.folder_len, h.name_len);
    if e.folder_start != h.folder_start
        || e.record_start != h.record_start
        || e.perm_start != h.perm_start
        || e.name_start != h.name_start
        || e.size != h.size
    {
        return 6;
    }
    if !stale && h.fold != 4 {
        return 3;
    }
    0
}
pub fn record_offset(h: &Header, n: u16) -> u32 {
    h.record_start.wrapping_add(u32::from(n) * 128)
}
pub fn order_offset(h: &Header, n: u16, arrival: bool) -> u32 {
    h.perm_start
        .wrapping_add((u32::from(n) + if arrival { u32::from(h.books) } else { 0 }) * 2)
}
