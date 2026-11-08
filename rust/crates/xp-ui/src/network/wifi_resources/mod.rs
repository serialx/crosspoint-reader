//! Wi-Fi-owned storage. Growth is fallible and kept outside rendering.
extern crate alloc;
mod buffer;
mod row;
mod small_z;
pub mod text;
pub use buffer::Buffer;
use core::alloc::Layout;
pub use row::{sort_rows, Row};
pub use small_z::SmallZ;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Capacity,
    Allocation(usize),
}

/// # Safety
/// Successful allocations are live, disjoint and aligned for their layout.
/// Release accepts that pointer/layout exactly once; callbacks retain no loans.
pub unsafe trait Memory {
    fn allocate(layout: Layout) -> *mut u8;
    /// # Safety
    /// Pointer and layout describe a live allocation from this implementation.
    unsafe fn release(pointer: *mut u8, layout: Layout);
}
pub struct Global;
unsafe impl Memory for Global {
    fn allocate(layout: Layout) -> *mut u8 {
        unsafe { alloc::alloc::alloc(layout) }
    }
    unsafe fn release(pointer: *mut u8, layout: Layout) {
        unsafe { alloc::alloc::dealloc(pointer, layout) };
    }
}

#[cfg(test)]
mod tests;
