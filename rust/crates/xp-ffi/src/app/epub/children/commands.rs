use crate::app::{abi, resources::Buffer};
use xp_ui::scene::Command;

/// Unpublished scratch. Growth releases the previous block before allocating.
pub(crate) struct Commands {
    storage: Option<Buffer>,
    capacity: u16,
    limit: u16,
}

impl Commands {
    pub(crate) fn new(capacity: u16) -> Self {
        Self {
            storage: None,
            capacity,
            limit: 128,
        }
    }

    pub(crate) fn fixed(capacity: u16) -> Self {
        Self {
            storage: None,
            capacity,
            limit: capacity,
        }
    }

    pub(crate) fn recover(&mut self, resources: &'static abi::Resources) -> bool {
        self.reserve(resources, self.capacity)
    }

    fn reserve(&mut self, resources: &'static abi::Resources, capacity: u16) -> bool {
        if capacity == 0 || capacity > 128 {
            return false;
        }
        if self.storage.is_some() && capacity <= self.capacity {
            return true;
        }
        self.storage = None;
        self.capacity = capacity.max(self.capacity);
        let bytes = usize::from(self.capacity) * core::mem::size_of::<Command>();
        self.storage = Buffer::allocate(resources, abi::MEMORY_DEFAULT, bytes);
        if let Some(storage) = &self.storage {
            // The platform allocator provides pointer alignment; zero is valid for every Command field.
            unsafe { storage.as_ptr().write_bytes(0, bytes) };
            true
        } else {
            false
        }
    }

    pub(crate) fn grow(&mut self, resources: &'static abi::Resources) -> bool {
        xp_ui::runtime::next_capacity(self.capacity, 0, 16, self.limit)
            .is_some_and(|capacity| self.reserve(resources, capacity))
    }

    pub(crate) fn as_mut_slice(&mut self) -> &mut [Command] {
        match &mut self.storage {
            Some(storage) => unsafe {
                core::slice::from_raw_parts_mut(storage.as_ptr().cast(), usize::from(self.capacity))
            },
            None => &mut [],
        }
    }

    pub(crate) fn clear(&mut self) {
        self.storage = None;
        self.capacity = 0;
    }
}
