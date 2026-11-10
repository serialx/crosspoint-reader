//! Dormant loaded-family and variant ownership. No native policy callback is kept.
use super::{
    catalog::{Allocation, Catalog, Family},
    catalog_storage::{Array, Name},
    owner::FaceOwner,
    parser::Allocator,
};
use crate::font::{
    loaded::{Entry, Registry},
    manager::Mode,
    registry,
};
use core::{ffi::CStr, mem};

#[repr(C)]
struct Loaded<'a> {
    face: Option<FaceOwner<'a>>,
    id: i32,
    size: u8,
}
#[repr(C)]
struct Variant {
    id: i32,
    used: u32,
}
pub struct Control<'a> {
    name: Name,
    size: u8,
    loaded: Array<Loaded<'a>>,
    variants: Array<Variant>,
    clock: u32,
    memory: &'a Allocator,
}
#[derive(Debug, PartialEq, Eq)]
pub enum LoadError {
    Path,
    Face,
    Collision,
    Allocation,
    NotInitialized,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Update {
    Vector {
        unload: bool,
    },
    Disappeared,
    Builtin {
        unload: bool,
        points: u8,
    },
    Keep {
        points: u8,
    },
    Reload {
        unload: bool,
        snap_before: Option<u8>,
    },
    Missing {
        unload: bool,
    },
}
impl<'a> Control<'a> {
    /// # Safety
    /// Internal allocator contract matches Catalog; all registry targets must be
    /// unregistered or unusable before this owner is dropped. Access is serialized.
    pub const unsafe fn new(memory: &'a Allocator) -> Self {
        Self {
            name: Name::new(),
            size: 0,
            loaded: Array::new(),
            variants: Array::new(),
            clock: 0,
            memory,
        }
    }
    pub fn begin(&mut self) -> Result<(), Allocation> {
        if self.variants.reserve_exact(5, self.memory) {
            Ok(())
        } else {
            Err(Allocation)
        }
    }
    pub fn family(&self) -> &CStr {
        self.name.cstr()
    }
    pub fn points(&self) -> u8 {
        self.size
    }
    pub fn get(&self, name: &CStr, size: u8) -> i32 {
        if name.to_bytes() != self.name.bytes() {
            return 0;
        }
        self.loaded
            .values()
            .iter()
            .find(|f| size == 0 || f.size == size)
            .map_or(0, |f| f.id)
    }
    /// # Safety
    /// The registry is live/serialized and no view survives removal or destruction.
    pub unsafe fn unload(&mut self, registry: *mut Registry, id: Option<i32>) {
        if let Some(id) = id {
            if let Some(index) = self.loaded.values().iter().position(|f| f.id == id) {
                (*registry).remove(id);
                drop(self.loaded.remove(index));
            }
        } else {
            self.variants.clear();
            (*registry).clear_fallbacks();
            for font in self.loaded.values_mut() {
                (*registry).remove(font.id);
                drop(font.face.take());
            }
            self.loaded.clear();
            let _ = self.name.assign(b"", self.memory);
            self.size = 0;
        }
    }
    /// # Safety
    /// No registry/catalog loan survives loader callbacks; loader cannot reenter
    /// this control or catalog. Registry targets are stable face-owned slots.
    pub unsafe fn load(
        &mut self,
        family: &Family,
        points: u8,
        id: i32,
        mode: Mode,
        registry: *mut Registry,
        load: impl FnOnce(&CStr) -> Option<FaceOwner<'a>>,
    ) -> Result<i32, LoadError> {
        if mode == Mode::Replace && !self.name.bytes().is_empty() {
            self.unload(registry, None);
        }
        let Some(file) = family.nearest(points, mode != Mode::Replace) else {
            return Ok(0);
        };
        if mode == Mode::ExtraSize {
            let existing = self.get(family.name(), points);
            if existing != 0 {
                return Ok(existing);
            }
        }
        let mut path = [0; 160];
        if !family.path(file, &mut path) {
            return Err(LoadError::Path);
        }
        let face = load(CStr::from_ptr(path.as_ptr().cast())).ok_or(LoadError::Face)?;
        let id = if id == 0 {
            registry::font_id(
                face.parsed().content_hash(),
                family.name().to_bytes(),
                file.size,
            )
        } else {
            id
        };
        if (*registry).contains(id) {
            return Err(LoadError::Collision);
        }
        let slots = core::array::from_fn(|style| face.font_slot(style as u8));
        if self
            .loaded
            .push(
                Loaded {
                    face: Some(face),
                    id,
                    size: file.size,
                },
                self.memory,
            )
            .is_err()
        {
            return Err(LoadError::Allocation);
        }
        // Native insertFont also ignores a rejected normal-less registration.
        let _ = (*registry).insert(Entry {
            id,
            slots,
            fallback: 0,
        });
        if mode == Mode::Replace {
            if !self.name.assign(family.name().to_bytes(), self.memory) {
                return Err(LoadError::Allocation);
            }
            self.size = file.size;
        }
        Ok(id)
    }
    pub fn update(&self, catalog: &Catalog<'_>, wanted: &CStr, points: u8, dirty: bool) -> Update {
        let unload = !self.name.bytes().is_empty();
        if wanted.to_bytes().is_empty() {
            return Update::Builtin {
                unload,
                points: snap(&[12, 14, 16, 18], points),
            };
        }
        let Some(family) = catalog.find(wanted) else {
            return if wanted.to_bytes() == self.name.bytes() {
                Update::Disappeared
            } else {
                Update::Missing { unload }
            };
        };
        if family.vector {
            return Update::Vector { unload };
        }
        if wanted.to_bytes() == self.name.bytes() {
            let selected = family.nearest(points, false).map_or(0, |f| f.size);
            if !dirty && selected == self.size {
                return Update::Keep { points: selected };
            }
            return Update::Reload {
                unload,
                snap_before: (selected != 0).then_some(selected),
            };
        }
        Update::Reload {
            unload,
            snap_before: None,
        }
    }
    /// # Safety
    /// Same registry/loader loan contract as load. begin reserves the native five
    /// records after discovery; no extra variant allocation occurs on this path.
    pub unsafe fn variant(
        &mut self,
        family: &Family,
        points: u8,
        id: i32,
        largest: u32,
        registry: *mut Registry,
        load: impl FnOnce(&CStr) -> Option<FaceOwner<'a>>,
    ) -> Result<bool, LoadError> {
        if let Some(v) = self.variants.values_mut().iter_mut().find(|v| v.id == id) {
            self.clock = self.clock.wrapping_add(1);
            v.used = self.clock;
            return Ok(true);
        }
        if self.get(family.name(), points) == id {
            return Ok(true);
        }
        if largest < 48 * 1024 {
            return Ok(false);
        }
        if self.variants.capacity() < 5 {
            return Err(LoadError::NotInitialized);
        }
        if self.variants.values().len() >= 5 {
            let index = self
                .variants
                .values()
                .iter()
                .enumerate()
                .min_by_key(|(_, v)| v.used)
                .map(|(i, _)| i)
                .expect("five variants");
            let oldest = self.variants.remove(index);
            self.unload(registry, Some(oldest.id));
        }
        if self.load(family, points, id, Mode::Variant, registry, load)? == 0 {
            return Ok(false);
        }
        self.clock = self.clock.wrapping_add(1);
        if self
            .variants
            .push(
                Variant {
                    id,
                    used: self.clock,
                },
                self.memory,
            )
            .is_err()
        {
            return Err(LoadError::Allocation);
        }
        Ok(true)
    }
}
impl Drop for Control<'_> {
    fn drop(&mut self) {
        self.variants.release(self.memory);
        self.loaded.release(self.memory);
        self.name.release(self.memory);
    }
}
pub fn variant_id(reader: i32, points: u8) -> i32 {
    let mut hash = 2166136261u32;
    for byte in reader.to_le_bytes() {
        hash = (hash ^ u32::from(byte)).wrapping_mul(16777619);
    }
    hash = (hash ^ u32::from(points)).wrapping_mul(16777619);
    hash = (hash ^ 0x56415200).wrapping_mul(16777619);
    if hash == 0 {
        1
    } else {
        hash as i32
    }
}
pub fn choose_variant(family: &Family, base: u8, scale: f32) -> u8 {
    let target = f32::from(base) * scale;
    let mut chosen = base;
    if scale < 1.0 {
        let mut below = 0;
        let mut smallest = base;
        for file in family.files() {
            if f32::from(file.size) <= target && file.size > below {
                below = file.size;
            }
            smallest = smallest.min(file.size);
        }
        chosen = if below != 0 { below } else { smallest };
    } else {
        for file in family.files() {
            if (f32::from(file.size) - target).abs() < (f32::from(chosen) - target).abs() {
                chosen = file.size;
            }
        }
    }
    chosen
}
fn snap(sizes: &[u8], points: u8) -> u8 {
    sizes
        .iter()
        .copied()
        .min_by_key(|size| size.abs_diff(points))
        .unwrap_or(points)
}
pub const FALLBACK_PROBES: [u32; 14] = [
    0x4E00, 0x3042, 0x30A2, 0xAC00, 0x03B1, 0x0430, 0x05D0, 0x0627, 0x0531, 0x10D0, 0x1200, 0x13A0,
    0x2D30, 0x0259,
];
pub const UI_POINTS: [u8; 3] = [8, 10, 12];
const _: () = {
    assert!(mem::size_of::<Variant>() == 8);
    assert!(!cfg!(target_pointer_width = "32") || mem::size_of::<Loaded<'_>>() == 12);
};

pub fn builtin_variant(ids: &[i32; 4], font: i32, scale: f32) -> i32 {
    let Some(base) = ids.iter().position(|id| *id == font) else {
        return 0;
    };
    let sizes = [12u8, 14, 16, 18];
    let target = f32::from(sizes[base]) * scale;
    let mut pick = if scale < 1.0 { 0 } else { base };
    for (index, size) in sizes.iter().enumerate() {
        if if scale < 1.0 {
            f32::from(*size) <= target
        } else {
            (f32::from(*size) - target).abs() < (f32::from(sizes[pick]) - target).abs()
        } {
            pick = index;
        }
    }
    ids[pick]
}
