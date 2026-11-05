extern crate alloc;
use super::Diagnostic;
use alloc::vec::Vec;

pub trait ByteSink {
    fn image_diagnostic(&mut self, _event: Diagnostic, _a: u32, _b: u32, _c: u32, _d: u32) {}
    fn open(&mut self) -> bool;
    fn write(&mut self, bytes: &[u8]) -> bool;
    fn close(&mut self) -> bool;
    fn abort(&mut self);
}

#[derive(Default)]
pub struct PixelCacheWriter {
    buffer: Vec<u8>,
    bytes_per_row: usize,
    width: i32,
    height: i32,
    rows: i32,
    start: i32,
    flushed: i32,
    pub active: bool,
    opened: bool,
}

impl PixelCacheWriter {
    pub fn begin(
        &mut self,
        sink: &mut impl ByteSink,
        width: i32,
        height: i32,
        max_rows: i32,
    ) -> bool {
        if width <= 0 || height <= 0 || width > 32767 || height > 32767 || max_rows <= 0 {
            return false;
        }
        self.bytes_per_row = (width as usize).div_ceil(4);
        self.width = width;
        self.height = height;
        // Keep a full MCU block plus slack, with a 16-row minimum and 24 KiB band cap.
        self.rows = (max_rows + 2)
            .max(16)
            .min(height)
            .min((24576 / self.bytes_per_row).max(1) as i32);
        if self.rows < max_rows {
            sink.image_diagnostic(
                Diagnostic::CacheBand,
                self.rows as u32,
                max_rows as u32,
                width as u32,
                height as u32,
            );
            return false;
        }
        // The spare row stays zero for gaps and final padding; pixel() cannot write it.
        let bytes = (self.rows as usize + 1) * self.bytes_per_row;
        if self.buffer.try_reserve_exact(bytes).is_err() {
            sink.image_diagnostic(Diagnostic::CacheAllocation, bytes as u32, 0, 0, 0);
            return false;
        }
        self.buffer.resize(bytes, 0);
        if !sink.open() {
            sink.image_diagnostic(Diagnostic::CacheOpen, 0, 0, 0, 0);
            self.buffer = Vec::new();
            return false;
        }
        self.opened = true;
        if !sink.write(&(width as u16).to_le_bytes()) || !sink.write(&(height as u16).to_le_bytes())
        {
            sink.image_diagnostic(Diagnostic::CacheHeader, 0, 0, 0, 0);
            self.abort(sink);
            return false;
        }
        sink.image_diagnostic(
            Diagnostic::CacheStarted,
            width as u32,
            height as u32,
            self.rows as u32,
            0,
        );
        self.active = true;
        true
    }

    pub fn advance(&mut self, sink: &mut impl ByteSink, top: i32) -> bool {
        if !self.active {
            return false;
        }
        if top <= self.start {
            return true;
        }
        // Decoder blocks arrive in raster order: rows below the next top are final.
        let top = top.min(self.height);
        for row in self.start..top {
            let index = (row - self.start).min(self.rows) as usize * self.bytes_per_row;
            if !sink.write(&self.buffer[index..index + self.bytes_per_row]) {
                sink.image_diagnostic(Diagnostic::CacheRow, row as u32, 0, 0, 0);
                self.active = false;
                return false;
            }
        }
        self.flushed = top;
        self.start = top;
        self.buffer[..self.rows as usize * self.bytes_per_row].fill(0);
        true
    }

    #[inline(always)]
    pub fn pixel(&mut self, x: i32, y: i32, value: u8) {
        let row = y - self.start;
        if row < 0 || row >= self.rows || x < 0 || x as usize / 4 >= self.bytes_per_row {
            return;
        }
        let index = row as usize * self.bytes_per_row + x as usize / 4;
        if let Some(byte) = self.buffer.get_mut(index) {
            let shift = 6 - (x & 3) * 2;
            *byte = (*byte & !(3 << shift)) | ((value & 3) << shift);
        }
    }

    pub fn finish(&mut self, sink: &mut impl ByteSink) -> bool {
        if !self.active {
            self.abort(sink);
            return false;
        }
        for row in self.flushed..self.height {
            let local = row - self.start;
            let index = if local >= 0 && local < self.rows {
                local
            } else {
                self.rows
            } as usize
                * self.bytes_per_row;
            if !sink.write(&self.buffer[index..index + self.bytes_per_row]) {
                sink.image_diagnostic(Diagnostic::CacheRow, row as u32, 0, 0, 0);
                self.abort(sink);
                return false;
            }
        }
        if !sink.close() {
            sink.image_diagnostic(Diagnostic::CacheClose, 0, 0, 0, 0);
            self.abort(sink);
            return false;
        }
        sink.image_diagnostic(
            Diagnostic::CacheWritten,
            self.width as u32,
            self.height as u32,
            (4 + self.bytes_per_row * self.height as usize) as u32,
            0,
        );
        self.opened = false;
        self.active = false;
        true
    }

    pub fn abort(&mut self, sink: &mut impl ByteSink) {
        if self.opened {
            sink.abort();
        }
        self.opened = false;
        self.active = false;
    }
}
