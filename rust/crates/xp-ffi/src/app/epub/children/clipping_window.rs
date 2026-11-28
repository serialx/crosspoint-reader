use core::ptr;

pub(super) const ROWS: usize = 8;
pub(super) struct Row {
    pub(super) chapter: [u8; 48],
    pub(super) preview: [u8; 260],
    pub(super) preview_length: usize,
}
pub(super) struct Window {
    rows: [Row; ROWS],
    first: i32,
    count: u8,
}
impl Window {
    /// Initialize the bounded preview storage directly inside its allocated owner.
    pub(super) unsafe fn initialize(out: *mut Self) {
        ptr::addr_of_mut!((*out).rows).write_bytes(0, 1);
        ptr::addr_of_mut!((*out).first).write(-1);
        ptr::addr_of_mut!((*out).count).write(0);
    }
    pub(super) fn invalidate(&mut self) {
        self.first = -1;
        self.count = 0;
    }
    pub(super) fn row(&self, index: i32) -> Option<&Row> {
        if self.first < 0 {
            return None;
        }
        let offset = usize::try_from(index.checked_sub(self.first)?).ok()?;
        self.rows[..usize::from(self.count)].get(offset)
    }
    /// Caller has admitted a storage operation before modifying this window.
    pub(super) fn refresh(&mut self, total: i32, start: i32, mut fill: impl FnMut(i32, &mut Row)) {
        let first = start.min(total - ROWS as i32).max(0);
        if first == self.first {
            return;
        }
        self.count = (total - first).clamp(0, ROWS as i32) as u8;
        for slot in 0..usize::from(self.count) {
            let row = &mut self.rows[slot];
            row.chapter.fill(0);
            row.preview[0] = 0;
            row.preview_length = 0;
            fill(first + slot as i32, row);
        }
        self.first = first;
    }
}
