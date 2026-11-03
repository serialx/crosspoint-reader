extern crate alloc;

use super::XpEpubLeaves;
use crate::app::{abi, resources::Buffer};
use core::{alloc::Layout, ptr::NonNull, slice};
use xp_core::inflate::InflateState;

const WINDOW: usize = 32768;

struct ScratchLease {
    bytes: NonNull<u8>,
    leaves: &'static XpEpubLeaves,
}

impl ScratchLease {
    fn claim(leaves: &'static XpEpubLeaves, size: usize) -> Option<Self> {
        let size = u32::try_from(size).ok()?;
        let bytes = NonNull::new(unsafe { (leaves.scratch_claim)(size) })?;
        Some(Self { bytes, leaves })
    }
}

impl Drop for ScratchLease {
    fn drop(&mut self) {
        unsafe { (self.leaves.scratch_release)(self.bytes.as_ptr()) };
    }
}

// Decoder and history storage never pass through a task-stack temporary.
pub(super) struct InflateStorage {
    state: NonNull<InflateState>,
    window: Option<NonNull<u8>>,
    lease: Option<ScratchLease>,
    heap_state: Option<Buffer>,
    heap_window: Option<Buffer>,
}

impl InflateStorage {
    pub(super) fn new(
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
        streaming: bool,
    ) -> Option<Self> {
        let state_layout = Layout::new::<InflateState>();
        let aligned = state_layout.pad_to_align().size();
        let needed = aligned + if streaming { WINDOW } else { 0 };
        let lease = ScratchLease::claim(leaves, needed);
        let heap_state = if lease.is_none() {
            let Some(state) = Buffer::allocate(resources, abi::MEMORY_DEFAULT, state_layout.size())
            else {
                unsafe {
                    (leaves.inflate_oom_log)(
                        abi::Text::from_bytes(c"state".to_bytes()),
                        state_layout.size() as u32,
                    )
                };
                return None;
            };
            Some(state)
        } else {
            None
        };
        let state = if let Some(lease) = &lease {
            // The renderer lends pointer-aligned framebuffer storage.
            assert_eq!(lease.bytes.as_ptr() as usize % state_layout.align(), 0);
            lease.bytes.cast()
        } else {
            NonNull::new(heap_state.as_ref()?.as_ptr().cast())?
        };
        let mut storage = Self {
            state,
            window: None,
            lease,
            heap_state,
            heap_window: None,
        };
        if streaming {
            storage.window = if let Some(lease) = &storage.lease {
                NonNull::new(unsafe { lease.bytes.as_ptr().add(aligned) })
            } else {
                let Some(window) = Buffer::allocate(resources, abi::MEMORY_DEFAULT, WINDOW) else {
                    unsafe {
                        (leaves.inflate_oom_log)(
                            abi::Text::from_bytes(c"window".to_bytes()),
                            WINDOW as u32,
                        )
                    };
                    return None;
                };
                storage.heap_window = Some(window);
                NonNull::new(storage.heap_window.as_ref()?.as_ptr())
            };
            let window = storage.window?;
            unsafe { window.as_ptr().write_bytes(0, WINDOW) };
        }
        unsafe { InflateState::init_in_place(storage.state.as_ptr(), false) };
        Some(storage)
    }

    pub(super) fn step(
        &mut self,
        input: &[u8],
        more: bool,
        position: usize,
    ) -> (i32, usize, usize) {
        let window = self.window.expect("streaming inflate storage");
        unsafe {
            self.state.as_mut().step(
                input,
                more,
                slice::from_raw_parts_mut(window.as_ptr(), WINDOW),
                position,
                true,
            )
        }
    }

    pub(super) fn history(&self) -> &[u8] {
        self.window.map_or(&[], |window| unsafe {
            slice::from_raw_parts(window.as_ptr(), WINDOW)
        })
    }
}

#[cfg(feature = "app-test")]
#[path = "../test_support/book_scratch.rs"]
mod tests;

pub(super) struct Inflater {
    storage: InflateStorage,
    input: *const u8,
    available: usize,
    fill: xp_core::book_io::zip_read::FillFn,
    fill_context: *mut core::ffi::c_void,
    exhausted: bool,
    finished: bool,
    window_position: usize,
    pending_start: usize,
    pending_length: usize,
    one_shot_start: *mut u8,
}

impl Inflater {
    pub(super) fn new(
        resources: &'static abi::Resources,
        leaves: &'static XpEpubLeaves,
        streaming: bool,
        fill: xp_core::book_io::zip_read::FillFn,
        fill_context: *mut core::ffi::c_void,
    ) -> Option<Self> {
        Some(Self {
            storage: InflateStorage::new(resources, leaves, streaming)?,
            input: core::ptr::null(),
            available: 0,
            fill,
            fill_context,
            exhausted: false,
            finished: false,
            window_position: 0,
            pending_start: 0,
            pending_length: 0,
            one_shot_start: core::ptr::null_mut(),
        })
    }

    // The output is writable for max bytes. Non-streaming calls use contiguous,
    // forward slices of one allocation, retained until the inflater is dropped.
    pub(super) unsafe fn read_at_most(
        &mut self,
        dest: *mut u8,
        max: usize,
        produced: &mut usize,
    ) -> i32 {
        *produced = 0;
        let streaming = self.storage.window.is_some();
        if !streaming {
            if self.one_shot_start.is_null() {
                self.one_shot_start = dest;
            }
            if max != 0 {
                unsafe { dest.write_bytes(0, max) };
            }
        }
        loop {
            if self.pending_length != 0 {
                let count = (max - *produced).min(self.pending_length);
                unsafe {
                    core::ptr::copy_nonoverlapping(
                        self.storage.history().as_ptr().add(self.pending_start),
                        dest.add(*produced),
                        count,
                    )
                };
                self.pending_start += count;
                self.pending_length -= count;
                *produced += count;
            }
            if *produced == max {
                return if self.finished && self.pending_length == 0 {
                    1
                } else {
                    0
                };
            }
            if self.finished {
                return 1;
            }
            if self.available == 0 && !self.exhausted {
                self.available = unsafe { (self.fill)(self.fill_context, &mut self.input) };
                if self.available == 0 {
                    self.exhausted = true;
                }
            }
            let input = if self.available == 0 {
                &[]
            } else {
                unsafe { slice::from_raw_parts(self.input, self.available) }
            };
            let (status, consumed, output) = if streaming {
                self.storage
                    .step(input, !self.exhausted, self.window_position)
            } else {
                let position =
                    unsafe { dest.add(*produced).offset_from(self.one_shot_start) } as usize;
                let capacity = position + max - *produced;
                unsafe {
                    self.storage.state.as_mut().step(
                        input,
                        !self.exhausted,
                        slice::from_raw_parts_mut(self.one_shot_start, capacity),
                        position,
                        false,
                    )
                }
            };
            if streaming {
                self.pending_start = self.window_position;
                self.pending_length = output;
                self.window_position += output;
                if self.window_position == WINDOW {
                    self.window_position = 0;
                }
            } else {
                *produced += output;
            }
            if consumed != 0 {
                self.input = unsafe { self.input.add(consumed) };
            }
            self.available -= consumed;
            if status == 1 {
                self.finished = true;
                continue;
            }
            if status < 0 || (status == 0 && self.exhausted && self.available == 0) {
                return 2;
            }
            if *produced == max {
                return if self.finished && self.pending_length == 0 {
                    1
                } else {
                    0
                };
            }
        }
    }

    pub(super) unsafe fn read(&mut self, dest: *mut u8, size: usize) -> bool {
        let mut total = 0;
        while total < size {
            let mut produced = 0;
            let status = unsafe { self.read_at_most(dest.add(total), size - total, &mut produced) };
            total += produced;
            if status == 2 {
                return false;
            }
            if status == 1 {
                return total == size;
            }
            if produced == 0 {
                return false;
            }
        }
        true
    }
}
