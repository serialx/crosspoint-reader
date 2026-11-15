use super::{Error, Global, Memory, SmallZ};

const ENCRYPTED: u8 = 1;
const SAVED: u8 = 2;
const HIDDEN: u8 = 4;
const STATUS_SAVED: u8 = 8;
const STATUS_ENCRYPTED: u8 = 16;
const STRENGTH_SHIFT: u8 = 5;
const STATUS_READY: u8 = 128;

#[repr(C)]
pub struct Row<M: Memory = Global> {
    ssid: SmallZ<M>,
    rssi: i32,
    first_scan: u16,
    flags: u8,
    reserved: u8,
}
impl<M: Memory> Row<M> {
    pub fn scan(
        ssid: &[u8],
        rssi: i32,
        encrypted: bool,
        saved: bool,
        first_scan: u16,
    ) -> Result<Self, Error> {
        let mut row = Self::hidden();
        row.ssid.try_assign(ssid)?;
        row.rssi = rssi;
        row.first_scan = first_scan;
        row.flags = (u8::from(encrypted) * ENCRYPTED) | (u8::from(saved) * SAVED);
        Ok(row)
    }
    pub const fn hidden() -> Self {
        Self {
            ssid: SmallZ::new(),
            rssi: 0,
            first_scan: u16::MAX,
            flags: HIDDEN | ENCRYPTED,
            reserved: 0,
        }
    }
    pub fn ssid(&self) -> &[u8] {
        self.ssid.as_bytes()
    }
    pub fn rssi(&self) -> i32 {
        self.rssi
    }
    pub fn scan_index(&self) -> u16 {
        self.first_scan
    }
    pub fn saved(&self) -> bool {
        self.flags & SAVED != 0
    }
    pub fn encrypted(&self) -> bool {
        self.flags & ENCRYPTED != 0
    }
    pub fn is_hidden(&self) -> bool {
        self.flags & HIDDEN != 0
    }
    pub fn set_saved(&mut self, saved: bool) {
        self.flags = (self.flags & !SAVED) | (u8::from(saved) * SAVED);
    }
    pub fn replace_signal(&mut self, rssi: i32, encrypted: bool) {
        self.rssi = rssi;
        self.flags = (self.flags & !ENCRYPTED) | (u8::from(encrypted) * ENCRYPTED);
    }
    pub fn rebuild_status(&mut self) {
        self.flags = (self.flags & (ENCRYPTED | SAVED | HIDDEN))
            | STATUS_READY
            | (u8::from(self.saved()) * STATUS_SAVED)
            | (u8::from(self.encrypted()) * STATUS_ENCRYPTED)
            | (super::super::wifi::signal_strength(self.rssi) << STRENGTH_SHIFT);
    }
    pub fn status(&self) -> [u8; 9] {
        let mut out = [0; 9];
        if self.is_hidden() || self.flags & STATUS_READY == 0 {
            return out;
        }
        let mut offset = 0;
        if self.flags & STATUS_SAVED != 0 {
            out[..2].copy_from_slice(b"+ ");
            offset += 2;
        }
        if self.flags & STATUS_ENCRYPTED != 0 {
            out[offset..offset + 2].copy_from_slice(b"* ");
            offset += 2;
        }
        let bars = [b"||||", b" |||", b"  ||", b"   |"];
        let strength = (self.flags >> STRENGTH_SHIFT) & 3;
        out[offset..offset + 4].copy_from_slice(bars[usize::from(strength)]);
        out
    }
}

/// Call before appending the hidden row. First scan indices survive replacements.
#[inline(never)]
pub fn sort_rows<M: Memory>(rows: &mut [Row<M>]) {
    let length = rows.len();
    if length < 2 {
        return;
    }
    // Heap root is last in display order; extraction fills the sorted suffix.
    for root in (0..length / 2).rev() {
        sift_down(rows, root, length);
    }
    for end in (1..length).rev() {
        rows.swap(0, end);
        sift_down(rows, 0, end);
    }
}
fn after<M: Memory>(a: &Row<M>, b: &Row<M>) -> bool {
    b.saved()
        .cmp(&a.saved())
        .then_with(|| b.rssi.cmp(&a.rssi))
        .then_with(|| a.first_scan.cmp(&b.first_scan))
        .is_gt()
}
#[inline(never)]
fn sift_down<M: Memory>(rows: &mut [Row<M>], mut root: usize, end: usize) {
    while root < end / 2 {
        let mut child = root * 2 + 1;
        if child + 1 < end && after(&rows[child + 1], &rows[child]) {
            child += 1;
        }
        if !after(&rows[child], &rows[root]) {
            break;
        }
        rows.swap(root, child);
        root = child;
    }
}
#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<Row>() == 28);
