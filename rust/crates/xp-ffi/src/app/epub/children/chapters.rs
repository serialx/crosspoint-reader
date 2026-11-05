use crate::app::resources::Text;
use core::ptr;

pub(crate) const ROWS: usize = 24;

/// Only a completed window is available to scene resources and glyph prewarm.
pub(crate) struct Window {
    labels: [Text; ROWS],
    first: Option<i32>,
    count: u8,
}
impl Window {
    /// `out` is exclusive, aligned, uninitialized owner storage.
    pub(crate) unsafe fn initialize(out: *mut Self) {
        for index in 0..ROWS {
            ptr::addr_of_mut!((*out).labels)
                .cast::<Text>()
                .add(index)
                .write(Text::default());
        }
        ptr::addr_of_mut!((*out).first).write(None);
        ptr::addr_of_mut!((*out).count).write(0);
    }

    pub(crate) fn first(&self) -> Option<i32> {
        self.first
    }

    pub(crate) fn labels(&self) -> &[Text] {
        &self.labels[..if self.first.is_some() {
            usize::from(self.count)
        } else {
            0
        }]
    }

    pub(crate) fn label(&self, index: i32) -> Option<&Text> {
        let offset = usize::try_from(index.checked_sub(self.first?)?).ok()?;
        self.labels().get(offset)
    }

    /// Fill uses the book's normal per-row fallback. False means storage admission failed.
    pub(crate) fn refresh(
        &mut self,
        total: i32,
        start: i32,
        mut fill: impl FnMut(i32, &mut Text) -> bool,
    ) -> bool {
        if total < 0 {
            return false;
        }
        let first = start.min(total - ROWS as i32).max(0);
        let count = (total - first).min(ROWS as i32) as u8;
        if self.first == Some(first) && self.count == count {
            return true;
        }
        self.first = None;
        self.count = 0;
        for index in 0..usize::from(count) {
            if !fill(first + index as i32, &mut self.labels[index]) {
                return false;
            }
        }
        self.count = count;
        self.first = Some(first);
        true
    }
}
