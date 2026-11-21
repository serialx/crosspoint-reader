//! Registered font families. Physical owners retain their stable data slots.
use super::raw::FontData;
use core::{
    ptr,
    sync::atomic::{AtomicU32, Ordering},
};

pub const CAPACITY: usize = 16;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Entry {
    pub id: i32,
    pub slots: [*const *const FontData; 4],
    pub fallback: i32,
    pub points: u16,
}

impl Entry {
    pub fn slot(&self, style: u8) -> *const *const FontData {
        let index = match style & 3 {
            3 if !self.slots[3].is_null() => 3,
            3 | 1 if !self.slots[1].is_null() => 1,
            3 | 2 if !self.slots[2].is_null() => 2,
            _ => 0,
        };
        self.slots[index]
    }
}

#[repr(C)]
pub struct Registry {
    entries: [Entry; CAPACITY],
    count: usize,
    pub generation: *const AtomicU32,
}

impl Registry {
    /// # Safety
    /// `out` is aligned writable owner storage, not yet shared. The generation
    /// outlives the registry; all calls and physical font mutations are serialized.
    pub unsafe fn initialize(out: *mut Self, generation: *const AtomicU32) {
        for i in 0..CAPACITY {
            ptr::addr_of_mut!((*out).entries)
                .cast::<Entry>()
                .add(i)
                .write(Entry {
                    id: 0,
                    slots: [ptr::null(); 4],
                    fallback: 0,
                    points: 0,
                });
        }
        ptr::addr_of_mut!((*out).count).write(0);
        ptr::addr_of_mut!((*out).generation).write(generation);
    }

    pub fn invalidate(&self) {
        // SAFETY: initialize's contract keeps the serialized generation live.
        // C3 has atomic loads/stores but no native atomic read-modify-write.
        unsafe {
            let generation = &*self.generation;
            generation.store(
                generation.load(Ordering::Relaxed).wrapping_add(1),
                Ordering::Relaxed,
            );
        }
    }

    pub fn len(&self) -> usize {
        self.count
    }
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }
    pub fn find(&self, id: i32) -> Option<Entry> {
        self.entry(id).copied()
    }
    fn entry(&self, id: i32) -> Option<&Entry> {
        self.entries[..self.count].iter().find(|e| e.id == id)
    }
    pub fn slot(&self, id: i32, style: u8) -> *const *const FontData {
        self.entry(id)
            .map_or(ptr::null(), |entry| entry.slot(style))
    }
    pub fn contains(&self, id: i32) -> bool {
        self.entry(id).is_some()
    }
    pub fn fallback(&self, id: i32) -> i32 {
        self.entry(id).map_or(0, |entry| entry.fallback)
    }
    pub fn at(&self, index: usize) -> Option<Entry> {
        self.entries[..self.count].get(index).copied()
    }
    pub fn insert(&mut self, value: Entry) -> bool {
        // Duplicate registration invalidates existing loans, as the native map did.
        self.invalidate();
        if self.count == CAPACITY
            || value.id == 0
            || value.slots[0].is_null()
            || self.find(value.id).is_some()
        {
            return false;
        }
        let index = self.entries[..self.count].partition_point(|e| e.id < value.id);
        self.entries.copy_within(index..self.count, index + 1);
        self.entries[index] = value;
        self.count += 1;
        true
    }
    pub fn remove(&mut self, id: i32) {
        self.invalidate();
        if let Some(index) = self.entries[..self.count].iter().position(|e| e.id == id) {
            self.entries.copy_within(index + 1..self.count, index);
            self.count -= 1;
        }
    }
    pub fn set_fallback(&mut self, id: i32, fallback: i32) {
        self.invalidate();
        if let Some(entry) = self.entries[..self.count].iter_mut().find(|e| e.id == id) {
            entry.fallback = fallback;
        }
    }
    pub fn clear_fallbacks(&mut self) {
        self.invalidate();
        for entry in &mut self.entries[..self.count] {
            entry.fallback = 0;
        }
    }
}

/// # Safety
/// `registry` remains live through `load`. No registry reference or font loan is
/// held during `load`, which may register/evict entries through the same pointer.
pub unsafe fn ensure(registry: *const Registry, id: i32, load: impl FnOnce(i32) -> bool) -> bool {
    if unsafe { (*registry).find(id).is_some() } {
        return true;
    }
    if !load(id) {
        return false;
    }
    unsafe { (*registry).find(id).is_some() }
}
