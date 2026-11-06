use super::owned::Fixed;
use crate::app::{bookmarks::Record, resources::Text};
use core::{fmt::Write, ptr};

pub(super) const ROWS: usize = 24;
// The TOC sink appends its borrowed title directly to the final subtitle.
pub(super) trait Toc {
    fn subtitle(&mut self, spine: u16, prefix: &[u8], output: &mut Text) -> bool;
}
pub(super) struct Window {
    subtitles: [Text; ROWS],
    first: Option<usize>,
    count: u8,
}
impl Window {
    /// Initialize only the handles inside the single allocated child owner.
    pub(super) unsafe fn initialize(out: *mut Self) {
        for index in 0..ROWS {
            unsafe {
                ptr::addr_of_mut!((*out).subtitles)
                    .cast::<Text>()
                    .add(index)
                    .write(Text::default())
            };
        }
        unsafe {
            ptr::addr_of_mut!((*out).first).write(None);
            ptr::addr_of_mut!((*out).count).write(0);
        }
    }
    pub(super) fn invalidate(&mut self) {
        self.first = None;
        self.count = 0;
    }
    pub(super) fn subtitle(&self, index: usize) -> Option<&Text> {
        let offset = index.checked_sub(self.first?)?;
        self.subtitles[..usize::from(self.count)].get(offset)
    }
    pub(super) fn refresh<N>(
        &mut self,
        records: &[Record<N>],
        start: usize,
        toc: &mut dyn Toc,
    ) -> bool {
        let first = start.min(records.len().saturating_sub(ROWS));
        let count = (records.len() - first).min(ROWS);
        if self.first == Some(first) && usize::from(self.count) == count {
            return true;
        }
        self.invalidate();
        for (index, row) in records[first..first + count].iter().enumerate() {
            let mut prefix = Fixed::<32>::new();
            let percent = (row.percentage.clamp(0.0, 1.0) * 100.0 + 0.5) as i32;
            let _ = write!(prefix, "{percent}% - ");
            if row.page_count > 0 {
                let _ = write!(prefix, "{}/{} - ", u32::from(row.page) + 1, row.page_count);
            }
            if !toc.subtitle(
                row.spine,
                &prefix.bytes[..prefix.len],
                &mut self.subtitles[index],
            ) {
                return false;
            }
        }
        self.count = count as u8;
        self.first = Some(first);
        true
    }
}
