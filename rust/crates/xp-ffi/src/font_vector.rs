//! Vector sources and registered families share the loaded-font owner.
#[path = "font_vector/physical.rs"]
mod physical;
use crate::font_sd::catalog::CATALOG_MEMORY as METADATA;
use crate::{font_registry::Owner, font_sd::*};
use core::{
    cell::UnsafeCell,
    ffi::{c_void, CStr},
    mem::{self, MaybeUninit},
    ptr::{self, NonNull},
    slice,
};
use physical::Native;
use xp_core::{
    font::{
        runtime::{ClearKind, Prewarm},
        vector_owned::{registration::Registered, Memory, Pressure, Source},
    },
    sdfont::{
        catalog::Family,
        catalog_storage::{Array, Name},
        io::Slot,
    },
};
extern "C" {
    fn fiFontMalloc(bytes: usize) -> *mut c_void;
    fn fiFontFree(bytes: *mut c_void);
    fn abort() -> !;
}
#[derive(Clone, Copy)]
struct Heap;
unsafe impl Memory for Heap {
    fn allocate(self, bytes: usize, _: usize) -> NonNull<u8> {
        NonNull::new(unsafe { fiFontMalloc(bytes) }.cast()).unwrap_or_else(|| unsafe { abort() })
    }
    unsafe fn deallocate(self, bytes: NonNull<u8>, _: usize, _: usize) {
        fiFontFree(bytes.as_ptr().cast());
    }
    fn largest_psram(self) -> usize {
        unsafe { heap_caps_get_largest_free_block(1 << 10) }
    }
    fn largest_internal(self) -> usize {
        unsafe { heap_caps_get_largest_free_block((1 << 11) | (1 << 2)) }
    }
    fn free_internal(self) -> usize {
        unsafe { heap_caps_get_free_size((1 << 11) | (1 << 2)) }
    }
}
type Font = Registered<'static, Native, Heap, Evict>;
struct Evict {
    cleared: bool,
    owner: *const Owner,
    active: *const c_void,
}
impl Pressure for Evict {
    fn clear_active_frame(&mut self) -> bool {
        mem::take(&mut self.cleared)
    }
    fn free_default(&mut self) -> usize {
        unsafe { heap_caps_get_free_size(1 << 12) }
    }
    fn render(&mut self) -> usize {
        unsafe {
            // Initial prewarm happens before registration; that owner is not a render sink target.
            self.cleared = crate::font_runtime::scanning(self.owner) == 0
                && handles(state(self.owner)).any(|handle| {
                    handle.face.cast_const() == self.active
                        && (*self.owner).registry.contains(handle.id)
                });
            (*state(self.owner)).active = self.active;
            let freed = crate::font_runtime::release(self.owner, 2);
            (*state(self.owner)).active = ptr::null();
            freed
        }
    }
    fn sd(&mut self) -> usize {
        unsafe { crate::font_runtime::release(self.owner, 3) }
    }
    fn vector(&mut self) -> usize {
        unsafe {
            (*state(self.owner)).active = self.active;
            let freed = crate::font_runtime::release(self.owner, 4);
            (*state(self.owner)).active = ptr::null();
            freed
        }
    }
}
#[repr(C)]
struct SourceSlot {
    files: [UnsafeCell<MaybeUninit<Slot>>; 2],
    bytes: *mut u8,
    length: usize,
    capacity: usize,
    size: u32,
    streamed: bool,
    present: bool,
    active: bool,
}
unsafe fn reset_file(file: *mut Slot) {
    xp_native_sd_handle_destroy(file);
    xp_native_sd_handle_init(file);
}
impl SourceSlot {
    unsafe fn close(&mut self) {
        if self.present && self.streamed {
            xp_native_sd_file_close(self.files[usize::from(self.active)].get().cast());
        }
        fiFontFree(self.bytes.cast());
        self.bytes = ptr::null_mut();
        self.length = 0;
        self.capacity = 0;
        self.size = 0;
        self.present = false;
        self.streamed = false;
    }
    unsafe fn open(&mut self, path: &CStr) -> bool {
        // Keep the previous streamed Impl until another stream replaces it.
        let file = self.files[usize::from(!self.active)].get().cast();
        self.length = 0;
        let mut size = 0;
        let opened = xp_native_sd_file_open(file, path.as_ptr(), &mut size) != 0;
        if !opened || size == 0 {
            if opened {
                xp_native_sd_file_close(file);
            }
            reset_file(file);
            return false;
        }
        let len = size as usize;
        let resident = len <= 6 * 1024 * 1024
            && (Heap.largest_psram() >= len || Heap.largest_internal() >= len + 96 * 1024);
        if !resident {
            reset_file(self.files[usize::from(self.active)].get().cast());
            self.active = !self.active;
        }
        let prefix = len.min(1024 * 1024);
        let count = if resident {
            len
        } else if Heap.largest_psram() > prefix + 256 * 1024 {
            prefix
        } else {
            0
        };
        if count != 0 {
            if count > self.capacity {
                let next = Heap.allocate(count, 1).as_ptr();
                fiFontFree(self.bytes.cast());
                self.bytes = next;
                self.capacity = count;
            }
            if (resident || xp_native_sd_file_seek(file, 0) != 0)
                && xp_native_sd_file_read(file, self.bytes, count) == count as i32
            {
                self.length = count;
            }
            if resident {
                xp_native_sd_file_close(file);
                reset_file(file);
                if self.length != count {
                    return false;
                }
            }
        }
        self.streamed = !resident;
        self.present = true;
        self.size = size;
        if self.streamed {
            xp_native_sd_diagnostic(
                path.as_ptr(),
                0x600,
                &[size / 1024, (self.length / 1024) as u32, 0, 0] as *const u32,
                0,
                0,
                false,
                ptr::null(),
            );
        }
        true
    }
    unsafe fn view(&self) -> Source<'static> {
        if !self.present {
            return Source::Missing;
        }
        let bytes = if self.length == 0 {
            &[]
        } else {
            slice::from_raw_parts(self.bytes, self.length)
        };
        if self.streamed {
            Source::Stream {
                file: NonNull::new_unchecked(self.files[usize::from(self.active)].get().cast()),
                size: self.size,
                prefix: bytes,
            }
        } else {
            Source::Resident(bytes)
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Handle {
    face: *mut c_void,
    id: i32,
    used: u32,
}
impl Handle {
    const EMPTY: Self = Self {
        face: ptr::null_mut(),
        id: 0,
        used: 0,
    };
}
#[repr(C)]
pub(crate) struct State {
    sources: [SourceSlot; 4],
    reader: Handle,
    ui: Array<*mut c_void>,
    ui_ids: Array<i32>,
    variants: Array<Handle>,
    name: Name,
    points: u8,
    clock: u32,
    active: *const c_void,
}
unsafe fn state(owner: *const Owner) -> *mut State {
    (*owner).vector.get()
}
pub(crate) unsafe fn initialize(owner: *mut Owner) {
    let s = state(owner);
    ptr::addr_of_mut!((*s).reader).write(Handle::EMPTY);
    ptr::addr_of_mut!((*s).ui).write(Array::new());
    ptr::addr_of_mut!((*s).ui_ids).write(Array::new());
    ptr::addr_of_mut!((*s).variants).write(Array::new());
    ptr::addr_of_mut!((*s).name).write(Name::new());
    ptr::addr_of_mut!((*s).points).write(0);
    ptr::addr_of_mut!((*s).clock).write(0);
    ptr::addr_of_mut!((*s).active).write(ptr::null());
    for i in 0..4 {
        let source = ptr::addr_of_mut!((*s).sources).cast::<SourceSlot>().add(i);
        source.write(SourceSlot {
            files: core::array::from_fn(|_| UnsafeCell::new(MaybeUninit::uninit())),
            active: false,
            bytes: ptr::null_mut(),
            length: 0,
            capacity: 0,
            size: 0,
            streamed: false,
            present: false,
        });
        for file in &(*source).files {
            xp_native_sd_handle_init(file.get().cast());
        }
    }
}
unsafe fn handles<'a>(s: *const State) -> impl Iterator<Item = Handle> + 'a {
    core::iter::once((*s).reader)
        .chain(
            (*s).ui
                .values()
                .iter()
                .zip((*s).ui_ids.values())
                .map(|(&face, &id)| Handle { face, id, used: 0 }),
        )
        .chain((*s).variants.values().iter().copied())
}
unsafe fn free(face: *mut c_void) {
    if !face.is_null() {
        ptr::drop_in_place(face.cast::<Font>());
        xp_native_sd_free(face.cast(), false);
    }
}
unsafe fn clear_variants(owner: *const Owner) {
    let s = state(owner);
    for h in (*s).variants.values() {
        (*owner.cast_mut()).registry.remove(h.id);
    }
    for h in (*s).variants.values() {
        free(h.face);
    }
    (*s).variants.clear();
}
pub(crate) unsafe fn prepare(owner: *const Owner) {
    assert!((*state(owner)).variants.reserve_exact(5, &METADATA));
}
pub(crate) unsafe fn unload(owner: *const Owner) {
    let s = state(owner);
    clear_variants(owner);
    for &id in (*s).ui_ids.values() {
        (*owner.cast_mut()).registry.remove(id);
    }
    (*s).ui_ids.clear();
    for &face in (*s).ui.values() {
        free(face);
    }
    (*s).ui.clear();
    (*owner.cast_mut()).registry.clear_fallbacks();
    if (*s).reader.id != 0 {
        (*owner.cast_mut()).registry.remove((*s).reader.id);
    }
    free((*s).reader.face);
    (*s).reader = Handle::EMPTY;
    for source in &mut (*s).sources {
        source.close();
    }
    assert!((*s).name.assign(b"", &METADATA));
    (*s).points = 0;
}
pub(crate) unsafe fn destroy(owner: *const Owner) {
    unload(owner);
    let s = state(owner);
    (*s).variants.release(&METADATA);
    (*s).ui_ids.release(&METADATA);
    (*s).ui.release(&METADATA);
    (*s).name.release(&METADATA);
    for source in &(*s).sources {
        for file in &source.files {
            xp_native_sd_handle_destroy(file.get().cast());
        }
    }
}
pub(crate) unsafe fn loaded(owner: *const Owner) -> bool {
    (*state(owner)).reader.id != 0
}
pub(crate) unsafe fn loaded_id(owner: *const Owner, name: &[u8]) -> i32 {
    let s = state(owner);
    if (*s).name.bytes() == name {
        (*s).reader.id
    } else {
        0
    }
}
fn font_id(name: &[u8], points: u8, ui: bool) -> i32 {
    let mut temporary = Name::new();
    let bytes = if ui {
        temporary = Name::copy(name, &METADATA).expect("UI family name");
        let mut text = [0; 35];
        text[..name.len()].copy_from_slice(name);
        text[name.len()..name.len() + 3].copy_from_slice(b"\x01ui");
        assert!(temporary.assign(&text[..name.len() + 3], &METADATA));
        temporary.bytes()
    } else {
        name
    };
    let hash = bytes.iter().fold(2166136261u32, |h, &b| {
        (h ^ u32::from(b)).wrapping_mul(16777619)
    });
    let id = ((hash ^ u32::from(points)).wrapping_mul(16777619) ^ 0x54544600) as i32;
    temporary.release(&METADATA);
    if id == 0 {
        1
    } else {
        id
    }
}
unsafe fn create(
    owner: *const Owner,
    id: impl FnOnce() -> i32,
    points: u8,
    cap: usize,
    max: u16,
    seed: bool,
) -> Option<Handle> {
    let face = xp_native_sd_allocate(mem::size_of::<Font>(), 0).cast::<Font>();
    if face.is_null() {
        return None;
    }
    let sources = core::array::from_fn(|i| (*state(owner)).sources[i].view());
    Font::initialize(
        face,
        sources,
        Heap,
        Evict {
            cleared: false,
            owner,
            active: face.cast(),
        },
    );
    if !Font::load(face, u16::from(points), cap, max) {
        ptr::drop_in_place(face);
        xp_native_sd_free(face.cast(), false);
        return None;
    }
    if seed {
        Font::prewarm(face, Some(b" "), true);
    }
    let id = id();
    if !(*owner.cast_mut())
        .registry
        .insert(Font::entry(face, id, points.into()))
    {
        ptr::drop_in_place(face);
        xp_native_sd_free(face.cast(), false);
        return None;
    }
    Some(Handle {
        face: face.cast(),
        id,
        used: 0,
    })
}
pub(crate) unsafe fn load(
    owner: *const Owner,
    family: &Family,
    points: u8,
    ui: [i32; 3],
    dirty: bool,
) -> bool {
    let s = state(owner);
    let name = family.name().to_bytes();
    let reader = (*s).reader;
    let same = !dirty && !reader.face.is_null() && loaded_id(owner, name) != 0;
    if same && (*s).points == points {
        return true;
    }
    let (cap, maximum) = if Heap.largest_psram() > 0 {
        (1024 * 1024, 4096)
    } else {
        (32 * 1024, 768)
    };
    if same {
        clear_variants(owner);
        (*owner.cast_mut()).registry.remove(reader.id);
        if Font::load(reader.face.cast(), u16::from(points), cap, maximum) {
            Font::prewarm(reader.face.cast(), Some(b" "), true);
            let id = font_id(name, points, false);
            if (*owner.cast_mut()).registry.insert(Font::entry(
                reader.face.cast(),
                id,
                points.into(),
            )) {
                (*s).reader.id = id;
                (*s).points = points;
                return true;
            }
        }
    }
    unload(owner);
    let mut path = [0; 160];
    for file in family.files() {
        let role = if file.style < 4 {
            usize::from(file.style)
        } else {
            0
        };
        if !(*s).sources[role].present && family.path(file, &mut path) {
            (*s).sources[role].open(CStr::from_ptr(path.as_ptr().cast()));
        }
    }
    let reader = if (*s).sources[0].present {
        create(
            owner,
            || font_id(name, points, false),
            points,
            cap,
            maximum,
            true,
        )
    } else {
        None
    };
    let Some(reader) = reader else {
        unload(owner);
        return false;
    };
    (*s).reader = reader;
    assert!((*s).name.assign(name, &METADATA));
    (*s).points = points;
    for (id, size) in ui.into_iter().zip([8, 10, 12]) {
        if Heap.largest_psram() == 0 && Heap.free_internal() < 160 * 1024 {
            continue;
        }
        if let Some(fallback) = create(
            owner,
            || font_id(name, size, true),
            size,
            16 * 1024,
            384,
            false,
        ) {
            (*owner.cast_mut()).registry.set_fallback(id, fallback.id);
            assert!((*s).ui_ids.push(fallback.id, &METADATA).is_ok());
            assert!((*s).ui.push(fallback.face, &METADATA).is_ok());
        }
    }
    true
}
unsafe fn load_variant(owner: *const Owner, id: i32, points: u8) -> bool {
    let s = state(owner);
    if let Some(h) = (*s).variants.values_mut().iter_mut().find(|h| h.id == id) {
        (*s).clock = (*s).clock.wrapping_add(1);
        h.used = (*s).clock;
        return true;
    }
    if (*s).variants.values().len() >= 5 {
        let i = (*s)
            .variants
            .values()
            .iter()
            .enumerate()
            .min_by_key(|(_, h)| h.used)
            .unwrap()
            .0;
        (*owner.cast_mut())
            .registry
            .remove((*s).variants.values()[i].id);
        free((*s).variants.remove(i).face);
    }
    let Some(mut h) = create(owner, || id, points, 64 * 1024, 512, true) else {
        return false;
    };
    (*s).clock = (*s).clock.wrapping_add(1);
    h.used = (*s).clock;
    assert!((*s).variants.push(h, &METADATA).is_ok());
    true
}
pub(crate) unsafe fn owns(owner: *const Owner, id: i32) -> bool {
    let reader = (*state(owner)).reader.id;
    reader != 0
        && (id == reader
            || (6..=72).any(|size| xp_core::sdfont::control::variant_id(reader, size) == id))
}
pub(crate) unsafe fn ensure(owner: *const Owner, id: i32) -> bool {
    let reader = (*state(owner)).reader.id;
    reader != 0
        && (6..=72)
            .find(|&size| xp_core::sdfont::control::variant_id(reader, size) == id)
            .is_some_and(|size| load_variant(owner, id, size))
}
pub(crate) unsafe fn variant(owner: *const Owner, id: i32, scale: f32) -> i32 {
    let s = state(owner);
    if (*s).reader.id != id || id == 0 {
        return 0;
    }
    let points = ((*s).points as f32 * scale + 0.5) as i32;
    let points = points.clamp(6, 72) as u8;
    if points == (*s).points {
        return id;
    }
    let variant = xp_core::sdfont::control::variant_id(id, points);
    if load_variant(owner, variant, points) {
        variant
    } else {
        0
    }
}
pub(crate) unsafe fn clear(owner: *const Owner, id: i32, kind: ClearKind) {
    let s = state(owner);
    if let Some(handle) = handles(s).find(|h| h.id == id && !h.face.is_null()) {
        if handle.face != (*s).active.cast_mut() {
            Font::clear(handle.face.cast(), kind == ClearKind::Resident);
        }
    }
}
pub(crate) unsafe fn prewarm(owner: *const Owner, id: i32, request: Prewarm<'_>) -> i32 {
    if let Some(handle) = handles(state(owner)).find(|h| h.id == id && !h.face.is_null()) {
        Font::prewarm(
            handle.face.cast(),
            Some(request.text.to_bytes()),
            !request.accumulate,
        );
    }
    0
}
