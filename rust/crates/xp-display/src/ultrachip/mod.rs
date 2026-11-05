//! UltraChip controller policy, preserving SDK 08effe64 bus transactions.
use crate::Bus;
#[cfg(feature = "uc8279c-a4")]
pub mod a4;
#[cfg(feature = "uc8279c-a4")]
mod a4_luts;
mod tags;
#[cfg(feature = "uc8279-x3")]
pub mod x3;
#[cfg(any(feature = "uc8279-x3", feature = "uc8179", feature = "uc8279-x4"))]
mod x3_luts;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
pub mod x4;
#[cfg(any(feature = "uc8179", feature = "uc8279-x4"))]
mod x4_luts;

/// Platform allocations retain the SDK's placement and failure policy.
/// # Safety
/// Nonnull allocations are exclusive, valid for the requested size and remain
/// valid until released through this allocator.
pub unsafe trait Memory {
    /// Returns a new, exclusive block of at least `bytes` or null.
    fn allocate(&mut self, bytes: usize, psram_only: bool) -> *mut u8;
    /// # Safety
    /// `ptr` is null or an unreleased block returned by this allocator.
    unsafe fn release(&mut self, ptr: *mut u8);
}

#[cfg(any(feature = "uc8179", feature = "uc8279-x4", feature = "uc8279c-a4"))]
struct Plane(*mut u8);
#[cfg(any(feature = "uc8179", feature = "uc8279-x4", feature = "uc8279c-a4"))]
impl Plane {
    const fn new() -> Self {
        Self(core::ptr::null_mut())
    }
    fn ensure(&mut self, memory: &mut impl Memory, size: usize, psram_only: bool) {
        if self.0.is_null() {
            self.0 = memory.allocate(size, psram_only);
            if !self.0.is_null() {
                // Rust slices require initialized bytes, including partial strips.
                unsafe { self.0.write_bytes(0, size) };
            }
        }
    }
    #[cfg(feature = "uc8279c-a4")]
    fn present(&self) -> bool {
        !self.0.is_null()
    }
    // The owning panel fixes the allocation length for its entire lifetime.
    fn bytes(&self, size: usize) -> Option<&[u8]> {
        if self.0.is_null() {
            None
        } else {
            Some(unsafe { core::slice::from_raw_parts(self.0, size) })
        }
    }
    fn bytes_mut(&mut self, size: usize) -> Option<&mut [u8]> {
        if self.0.is_null() {
            None
        } else {
            Some(unsafe { core::slice::from_raw_parts_mut(self.0, size) })
        }
    }
    fn release(&mut self, memory: &mut impl Memory) {
        unsafe {
            memory.release(self.0);
        }
        self.0 = core::ptr::null_mut();
    }
}

fn fill(bus: &mut impl Bus, cmd: u8, height: u16, stride: u16) {
    let row = [0xff; 128];
    bus.cmd(cmd);
    bus.txn_begin();
    for _ in 0..height {
        let mut remaining = stride as usize;
        while remaining != 0 {
            let n = remaining.min(row.len());
            bus.txn_write(&row[..n]);
            remaining -= n;
        }
    }
    bus.txn_end();
}

#[cfg(feature = "uc8279-x3")]
fn flipped(bus: &mut impl Bus, cmd: u8, data: &[u8], stride: u16, invert: bool) {
    let mut row = [0; 128];
    bus.cmd(cmd);
    bus.txn_begin();
    for src in data.chunks_exact(stride as usize).rev() {
        if invert {
            for chunk in src.chunks(row.len()) {
                for (dst, src) in row.iter_mut().zip(chunk) {
                    *dst = !src;
                }
                bus.txn_write(&row[..chunk.len()]);
            }
        } else {
            bus.txn_write(src);
        }
    }
    bus.txn_end();
}

#[cfg(any(feature = "uc8179", feature = "uc8279-x4", feature = "uc8279-x3"))]
fn wait_start(bus: &mut impl Bus) {
    let start = bus.clock_ms();
    while bus.busy_level() == 1 && bus.clock_ms().wrapping_sub(start) < 50 {
        bus.delay_ms(1);
    }
}
