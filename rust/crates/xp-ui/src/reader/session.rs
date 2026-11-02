//! Reading-session accounting shared by owned EPUB and native reader adapters.

#[derive(Clone, Copy, Default)]
pub struct SessionAccounting {
    pub anchor_ms: u32,
    pub anchor_epoch: i64,
    pub start_epoch: i64,
    pub duration: u32,
    pub anchor_progress: u16,
    pub start_progress: u16,
    pub end_progress: u16,
    pub has_anchor: bool,
    pub pending_forward: bool,
}

impl SessionAccounting {
    pub fn note(&mut self, forward: bool, succeeded: bool) {
        self.pending_forward = forward && succeeded;
    }

    pub fn render(&mut self, now: u32, epoch: i64, progress: i32) {
        let progress = progress.clamp(0, 10000) as u16;
        if self.pending_forward && self.has_anchor && self.anchor_epoch > 0 {
            let elapsed = now.wrapping_sub(self.anchor_ms) / 1000;
            if elapsed >= 2 {
                if self.duration == 0 {
                    self.start_epoch = self.anchor_epoch;
                    self.start_progress = self.anchor_progress;
                    self.end_progress = self.anchor_progress;
                }
                self.duration = self.duration.wrapping_add(elapsed.min(1800));
                self.end_progress = self.end_progress.max(progress);
            }
        }
        self.anchor_ms = now;
        self.anchor_epoch = epoch;
        self.anchor_progress = progress;
        self.has_anchor = true;
        self.pending_forward = false;
    }

    pub fn worthy(&self) -> bool {
        self.start_epoch > 0
            && self.duration >= 10
            && self.start_epoch.wrapping_add(i64::from(self.duration)) > self.start_epoch
    }
}
