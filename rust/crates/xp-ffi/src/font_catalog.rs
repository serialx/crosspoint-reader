//! SD catalog ownership and sequential native effects.
extern crate alloc;
use super::*;
use core::{marker::PhantomPinned, mem::MaybeUninit, sync::atomic::Ordering};
use xp_core::sdfont::{
    catalog::{Catalog, Directory, File},
    control::{self, Update},
};

extern "C" {
    fn xp_native_sd_directory_open(slot: *mut Slot, path: *const c_char) -> i32;
    fn xp_native_sd_directory_next(
        slot: *mut Slot,
        name: *mut u8,
        directory: *mut bool,
        names: bool,
    ) -> bool;
    pub(crate) fn xp_native_sd_path_exists(path: *const c_char) -> bool;
    #[cfg(any(feature = "app-test", feature = "vector-fonts"))]
    fn xp_native_sd_inspect(path: *const c_char, weight: *mut u16, italic: *mut bool) -> bool;
}
unsafe extern "C" fn required(bytes: usize) -> *mut u8 {
    xp_native_sd_allocate(bytes, 2)
}
pub(crate) static CATALOG_MEMORY: Allocator = Allocator {
    styles: required,
    resident: allocate_internal,
    free_styles: free_internal,
    free_resident: xp_native_sd_free,
};
#[repr(u32)]
enum Event {
    Snap = 1,
    Ready,
    LoadedBoot,
    FailedBoot,
    MissingBoot,
    Dirty,
    Disappeared,
    Reload,
    Loaded,
    Failed,
    Missing,
    NoCoverage,
    MissingUi,
    SkipVariant,
    VariantLoaded,
}
unsafe fn log(event: Event, name: &CStr, a: u32, b: u32, dirty: bool) {
    xp_native_sd_diagnostic(
        name.as_ptr(),
        0x500 + event as u32,
        [a, b, u32::from(dirty), 0].as_ptr(),
        0,
        0,
        false,
        ptr::null(),
    );
}
#[repr(C)]
pub(crate) struct State {
    catalog: core::cell::UnsafeCell<Catalog<'static>>,
    dirty: AtomicBool,
}
impl State {
    pub(crate) const unsafe fn new() -> Self {
        Self {
            catalog: core::cell::UnsafeCell::new(Catalog::new(&CATALOG_MEMORY)),
            dirty: AtomicBool::new(false),
        }
    }
}
struct Slots {
    slots: MaybeUninit<[Slot; 2]>,
    initialized: bool,
    _pin: PhantomPinned,
}
impl Drop for Slots {
    fn drop(&mut self) {
        if self.initialized {
            unsafe {
                xp_native_sd_handle_destroy(self.slots.as_mut_ptr().cast::<Slot>().add(1));
                xp_native_sd_handle_destroy(self.slots.as_mut_ptr().cast());
            }
        }
    }
}
struct Dirs<'a>(Pin<&'a mut Slots>);
impl Dirs<'_> {
    unsafe fn slot(&mut self, root: bool) -> *mut Slot {
        let holder = self.0.as_mut().get_unchecked_mut();
        let first = holder.slots.as_mut_ptr().cast::<Slot>();
        if !holder.initialized {
            xp_native_sd_handle_init(first);
            xp_native_sd_handle_init(first.add(1));
            holder.initialized = true;
        }
        first.add(usize::from(!root))
    }
}
// SAFETY: slots are pinned before construction; entry names fit the 128-byte loan.
unsafe impl Directory for Dirs<'_> {
    fn open(&mut self, path: &CStr, root: bool) -> i32 {
        unsafe { xp_native_sd_directory_open(self.slot(root), path.as_ptr()) }
    }
    fn next(&mut self, root: bool, name: &mut [u8; 128], directory: &mut bool) -> bool {
        unsafe { xp_native_sd_directory_next(self.slot(root), name.as_mut_ptr(), directory, root) }
    }
    fn close(&mut self, root: bool) {
        unsafe {
            let slot = self.slot(root);
            xp_native_sd_handle_destroy(slot);
            xp_native_sd_handle_init(slot);
        }
    }
    fn inspect(&mut self, path: &CStr) -> Option<(u16, bool)> {
        #[cfg(any(feature = "app-test", feature = "vector-fonts"))]
        {
            let (mut weight, mut italic) = (0, false);
            unsafe {
                xp_native_sd_inspect(path.as_ptr(), &mut weight, &mut italic)
                    .then_some((weight, italic))
            }
        }
        #[cfg(not(any(feature = "app-test", feature = "vector-fonts")))]
        {
            let _ = path;
            None
        }
    }
    fn log(&mut self, event: u32, a: &CStr, b: Option<&CStr>, n: usize, total: usize) {
        unsafe {
            xp_native_sd_diagnostic(
                a.as_ptr(),
                0x400 + event,
                [n as u32, total as u32, 0, 0].as_ptr(),
                0,
                0,
                false,
                b.map_or(ptr::null(), CStr::as_ptr),
            );
        }
    }
}
pub(crate) unsafe fn catalog<'a>(owner: *const Owner) -> &'a mut Catalog<'static> {
    &mut *(*owner).catalog.catalog.get()
}
unsafe fn discover(owner: *const Owner, vectors: bool) {
    (*owner).registry.invalidate();
    let mut slots = pin!(Slots {
        slots: MaybeUninit::uninit(),
        initialized: false,
        _pin: PhantomPinned
    });
    // Required container allocations inherit native new[]'s fatal failure contract.
    catalog(owner)
        .discover(&mut Dirs(slots.as_mut()), vectors)
        .expect("catalog capacity");
}
#[repr(C)]
pub struct Query {
    operation: u32,
    index: usize,
    file: usize,
    name: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
}
#[repr(C)]
pub struct View {
    name: *const c_char,
    length: usize,
    root: *const c_char,
    files: *const File,
    count: usize,
    sizes: [u32; 8],
    id: i32,
    vector: bool,
    loose: bool,
}
pub(crate) unsafe fn loaded_id(owner: *const Owner, name: &[u8]) -> i32 {
    #[cfg(feature = "vector-fonts")]
    {
        let id = crate::font_vector::loaded_id(owner, name);
        if id != 0 {
            return id;
        }
    }
    control(owner).get_bytes(name, 0)
}

/// # Safety
/// Query/output are borrowed for this serialized call. Names/files remain borrowed
/// until rediscovery/drop; no catalog mutation is allowed across a borrowed view.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_catalog_query(
    owner: *const Owner,
    request: *const Query,
    out: *mut View,
) -> usize {
    let q = &*request;
    if q.operation == 6 {
        return loaded_id(owner, bytes(q.name, q.length)) as u32 as usize;
    }
    if q.operation == 4 || q.operation == 5 {
        return xp_core::font::registry::root(
            q.name.cast(),
            c"/.fonts".as_ptr(),
            c"/fonts".as_ptr(),
            xp_native_sd_path_exists,
            q.operation == 5,
        ) as usize;
    }
    if q.operation == 7 {
        let (sizes, reserve) = selectable(owner, bytes(q.name, q.length));
        (*out).sizes = sizes;
        return reserve;
    }
    let catalog = catalog(owner);
    if q.operation == 0 {
        return catalog.families().len();
    }
    let index = if q.operation == 2 {
        let name = bytes(q.name, q.length);
        let Some(i) = catalog
            .families()
            .iter()
            .position(|f| f.name().to_bytes() == name)
        else {
            return 0;
        };
        i
    } else {
        q.index
    };
    let Some(family) = catalog.families().get(index) else {
        return 0;
    };
    if q.operation == 3 {
        if q.capacity == 0 {
            return 0;
        }
        return family.files().get(q.file).is_some_and(|file| {
            family.path(file, core::slice::from_raw_parts_mut(q.output, q.capacity))
        }) as usize;
    }
    if !matches!(q.operation, 1 | 2) {
        return 0;
    }
    let borrowed = family.borrowed();
    let mut sizes = [0; 8];
    for file in family.files() {
        sizes[usize::from(file.size / 32)] |= 1 << (file.size % 32);
    }
    out.write(View {
        name: borrowed.name,
        length: borrowed.name_len,
        root: borrowed.root,
        files: family.files().as_ptr(),
        count: family.files().len(),
        sizes,
        id: control(owner).get(family.name(), 0),
        vector: family.vector,
        loose: family.loose,
    });
    index + 1
}
/// # Safety
/// Owner is initialized. Marking is concurrent-safe; refreshing is serialized.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_catalog_dirty(
    owner: *const Owner,
    operation: u8,
    vectors: bool,
) -> bool {
    if operation == 0 {
        (*owner).catalog.dirty.store(true, Ordering::Release);
        return false;
    }
    if operation > 2 {
        return false;
    }
    let dirty = take_dirty(owner) || operation == 2;
    if dirty {
        discover(owner, vectors);
    }
    dirty
}
unsafe fn take_dirty(owner: *const Owner) -> bool {
    let dirty = &(*owner).catalog.dirty;
    #[cfg(target_has_atomic = "8")]
    {
        dirty.swap(false, Ordering::Acquire)
    }
    #[cfg(not(target_has_atomic = "8"))]
    {
        __atomic_exchange_1(dirty.as_ptr().cast(), 0, 2) != 0
    }
}
unsafe fn load(
    owner: *const Owner,
    family: &xp_core::sdfont::catalog::Family,
    points: u8,
    id: i32,
    mode: Mode,
) -> i32 {
    control(owner)
        .load(
            family,
            points,
            id,
            mode,
            ptr::addr_of_mut!((*owner.cast_mut()).registry),
            |path| load_face(owner, path),
        )
        .unwrap_or(0)
}
pub(crate) unsafe fn ensure_id(owner: *const Owner, id: i32) -> bool {
    #[cfg(feature = "vector-fonts")]
    if crate::font_vector::ensure(owner, id) {
        return true;
    }
    let Some(family) = catalog(owner).find(control(owner).family()) else {
        return false;
    };
    let reader = control(owner).get(family.name(), 0);
    let Some(points) = family
        .files()
        .iter()
        .find(|f| control::variant_id(reader, f.size) == id)
        .map(|f| f.size)
    else {
        return false;
    };
    let mut loaded = false;
    let result = control(owner)
        .variant_with(
            family,
            points,
            id,
            || {
                let largest = heap(true);
                if largest < 48 * 1024 {
                    log(
                        Event::SkipVariant,
                        c"",
                        u32::from(points),
                        heap(true),
                        false,
                    );
                }
                largest
            },
            ptr::addr_of_mut!((*owner.cast_mut()).registry),
            |path| {
                let face = load_face(owner, path);
                loaded = face.is_some();
                face
            },
        )
        .unwrap_or(false);
    if loaded && result {
        log(
            Event::VariantLoaded,
            family.name(),
            u32::from(points),
            heap(false),
            false,
        );
    }
    result
}
/// # Safety
/// Owner is initialized/serialized; no source/catalog loan crosses variant loading.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_font_variant(owner: *const Owner, id: i32, scale: f32) -> i32 {
    #[cfg(feature = "vector-fonts")]
    if crate::font_vector::owns(owner, id) {
        return crate::font_vector::variant(owner, id, scale);
    }
    let Some(family) = catalog(owner).find(control(owner).family()) else {
        return 0;
    };
    if control(owner).get(family.name(), 0) != id {
        return 0;
    }
    let base = control(owner).points();
    let points = control::choose_variant(family, base, scale);
    if points == base {
        return id;
    }
    let variant = control::variant_id(id, points);
    if ensure_id(owner, variant) {
        variant
    } else {
        0
    }
}
unsafe fn owns_id(owner: *const Owner, id: i32) -> bool {
    #[cfg(feature = "vector-fonts")]
    if crate::font_vector::owns(owner, id) {
        return true;
    }
    if control(owner).face(id).is_some() {
        return true;
    }
    let Some(family) = catalog(owner).find(control(owner).family()) else {
        return false;
    };
    let reader = control(owner).get(family.name(), 0);
    family
        .files()
        .iter()
        .any(|f| control::variant_id(reader, f.size) == id)
}
pub(crate) unsafe fn variant(owner: *const Owner, id: i32, scale: f32) -> Option<i32> {
    if (scale - 1.0).abs() < 0.05 {
        return Some(id);
    }
    owns_id(owner, id).then(|| xp_sd_font_variant(owner, id, scale))
}
pub(crate) unsafe fn ready(owner: *const Owner, id: i32, fallback: i32) -> Option<i32> {
    (id == fallback || owns_id(owner, id)).then(|| {
        if id == fallback || Owner::ensure(owner, id) {
            id
        } else {
            fallback
        }
    })
}
#[repr(C)]
pub struct Settings {
    family: *mut c_char,
    points: *mut u8,
    ui: [i32; 3],
    vectors: bool,
    #[cfg(any(test, feature = "app-test"))]
    vector_loaded: bool,
    #[cfg(any(test, feature = "app-test"))]
    vector_family: core::cell::UnsafeCell<View>,
    #[cfg(any(test, feature = "app-test"))]
    vector_index: core::cell::UnsafeCell<usize>,
}
#[repr(C)]
pub struct Continuation {
    address: *const Continuation,
    owner: *const Owner,
    request: *const Settings,
    pub epoch: u32,
    step: u32,
    dirty: bool,
    boot: bool,
}
#[repr(u32)]
pub enum Action {
    Complete,
    PersistSettings,
    #[cfg(any(test, feature = "app-test"))]
    UnloadVector = 2,
    #[cfg(all(not(feature = "vector-fonts"), any(test, feature = "app-test")))]
    LoadVector = 3,
    Rejected = 4,
    #[cfg(any(test, feature = "app-test"))]
    PrepareVector = 5,
    #[cfg(not(feature = "app-test"))]
    BorrowSettings = 6,
}
const PLAN: u32 = 1;
const LOAD: u32 = 2;
const FALLBACK: u32 = 3;
const DONE: u32 = 4;
const VECTOR: u32 = 5;
unsafe fn emit(owner: *const Owner, token: &mut Continuation, next: u32, action: Action) -> Action {
    (*owner).registry.invalidate();
    token.epoch = (*(*owner).registry.generation).load(Ordering::Relaxed);
    token.step = next;
    action
}
unsafe fn snap(s: &Settings, points: u8) -> bool {
    if points == 0 || *s.points == points {
        return false;
    }
    log(
        Event::Snap,
        c"",
        u32::from(*s.points),
        u32::from(points),
        false,
    );
    *s.points = points;
    true
}
/// # Safety
/// Settings/token are exclusive stable caller storage across the bounded sequence.
/// Native persists settings only after return; all font mutations stay in Rust.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_fonts_begin(
    owner: *const Owner,
    settings: *mut Settings,
    token: *mut Continuation,
) -> Action {
    discover(owner, (*settings).vectors);
    control(owner)
        .begin_with(&CATALOG_MEMORY)
        .expect("variant capacity");
    token.write(Continuation {
        address: token,
        owner,
        request: settings,
        epoch: 0,
        step: PLAN,
        dirty: false,
        boot: true,
    });
    #[cfg(feature = "vector-fonts")]
    if (*settings).vectors {
        crate::font_vector::prepare(owner);
    }
    #[cfg(any(test, feature = "app-test"))]
    if (*settings).vectors && !cfg!(feature = "vector-fonts") {
        return emit(owner, &mut *token, PLAN, Action::PrepareVector);
    }
    emit(owner, &mut *token, PLAN, Action::Complete);
    resume(owner, settings, &mut *token)
}
/// # Safety
/// A zero token starts ensure. Otherwise token belongs to this owner/request and
/// no competing mutation occurs; stale/foreign/completed tokens are rejected.
#[no_mangle]
pub unsafe extern "C" fn xp_sd_fonts_ensure(
    owner: *const Owner,
    settings: *mut Settings,
    token: *mut Continuation,
) -> Action {
    let t = &mut *token;
    if t.owner.is_null() && t.step == 0 {
        let dirty = take_dirty(owner);
        if dirty {
            log(Event::Dirty, c"", 0, 0, false);
            discover(owner, (*settings).vectors);
        }
        *t = Continuation {
            address: token,
            owner,
            request: settings,
            epoch: 0,
            step: PLAN,
            dirty,
            boot: false,
        };
        emit(owner, t, PLAN, Action::Complete);
    } else if t.address != token
        || t.owner != owner
        || t.request != settings
        || t.step == 0
        || (t.step > DONE && !(cfg!(feature = "vector-fonts") && t.step == VECTOR))
        || t.epoch != (*(*owner).registry.generation).load(Ordering::Relaxed)
    {
        return Action::Rejected;
    }
    resume(owner, settings, t)
}
unsafe fn resume(owner: *const Owner, settings: *const Settings, t: &mut Continuation) -> Action {
    let s = &*settings;
    let wanted = CStr::from_ptr(s.family);
    loop {
        match t.step {
            PLAN => {
                if t.boot && wanted.to_bytes().is_empty() {
                    t.step = DONE;
                    continue;
                }
                let update = control(owner).update(catalog(owner), wanted, *s.points, t.dirty);
                #[cfg(feature = "vector-fonts")]
                if crate::font_vector::loaded(owner) && !matches!(update, Update::Vector { .. }) {
                    crate::font_vector::unload(owner);
                }
                #[cfg(any(test, feature = "app-test"))]
                if !cfg!(feature = "vector-fonts")
                    && s.vector_loaded
                    && !matches!(update, Update::Vector { .. })
                {
                    return emit(owner, t, PLAN, Action::UnloadVector);
                }
                match update {
                    Update::Vector { unload } => {
                        if unload {
                            control(owner)
                                .unload(ptr::addr_of_mut!((*owner.cast_mut()).registry), None);
                        }
                        #[cfg(feature = "vector-fonts")]
                        {
                            let points = (*s.points).clamp(8, 22);
                            if snap(s, points) {
                                return emit(owner, t, VECTOR, Action::PersistSettings);
                            }
                            t.step = VECTOR;
                        }
                        #[cfg(not(any(feature = "vector-fonts", test, feature = "app-test")))]
                        return Action::Rejected;
                        #[cfg(all(not(feature = "vector-fonts"), any(test, feature = "app-test")))]
                        {
                            let query = Query {
                                operation: 2,
                                index: 0,
                                file: 0,
                                name: wanted.as_ptr().cast(),
                                length: wanted.to_bytes().len(),
                                output: ptr::null_mut(),
                                capacity: 0,
                            };
                            *s.vector_index.get() =
                                xp_sd_catalog_query(owner, &query, s.vector_family.get()) - 1;
                            emit(owner, t, 0, Action::Complete);
                            return Action::LoadVector;
                        }
                    }
                    Update::Builtin { unload, points } => {
                        if unload {
                            control(owner)
                                .unload(ptr::addr_of_mut!((*owner.cast_mut()).registry), None);
                        }
                        if snap(s, points) {
                            return emit(owner, t, DONE, Action::PersistSettings);
                        }
                        t.step = DONE;
                    }
                    Update::Keep { points } => {
                        if snap(s, points) {
                            return emit(owner, t, DONE, Action::PersistSettings);
                        }
                        t.step = DONE;
                    }
                    Update::Reload { snap_before, .. } => {
                        t.step = LOAD;
                        if let Some(points) = snap_before {
                            if snap(s, points) {
                                return emit(owner, t, LOAD, Action::PersistSettings);
                            }
                        }
                    }
                    Update::Disappeared => {
                        log(Event::Disappeared, wanted, 0, 0, false);
                        control(owner)
                            .unload(ptr::addr_of_mut!((*owner.cast_mut()).registry), None);
                        return clear(owner, s, t);
                    }
                    Update::Missing { unload } => {
                        if unload {
                            control(owner)
                                .unload(ptr::addr_of_mut!((*owner.cast_mut()).registry), None);
                        }
                        log(
                            if t.boot {
                                Event::MissingBoot
                            } else {
                                Event::Missing
                            },
                            wanted,
                            0,
                            0,
                            false,
                        );
                        return clear(owner, s, t);
                    }
                }
            }
            #[cfg(feature = "vector-fonts")]
            VECTOR => {
                let Some(family) = catalog(owner).find(wanted) else {
                    return clear(owner, s, t);
                };
                if family.files().is_empty() {
                    return clear(owner, s, t);
                }
                crate::font_vector::load(owner, family, *s.points, s.ui, t.dirty);
                t.step = DONE;
            }
            LOAD => {
                if control(owner).family() == wanted {
                    log(
                        Event::Reload,
                        wanted,
                        u32::from(control(owner).points()),
                        u32::from(*s.points),
                        t.dirty,
                    );
                }
                let Some(family) = catalog(owner).find(wanted) else {
                    return clear(owner, s, t);
                };
                if load(owner, family, *s.points, 0, Mode::Replace) == 0 {
                    log(
                        if t.boot {
                            Event::FailedBoot
                        } else {
                            Event::Failed
                        },
                        wanted,
                        0,
                        0,
                        false,
                    );
                    return clear(owner, s, t);
                }
                t.step = FALLBACK;
                if snap(s, control(owner).points()) {
                    return emit(owner, t, FALLBACK, Action::PersistSettings);
                }
            }
            FALLBACK => {
                let Some(family) = catalog(owner).find(wanted) else {
                    return clear(owner, s, t);
                };
                let id = control(owner).get(wanted, 0);
                let coverage = control::FALLBACK_PROBES.iter().any(|cp| {
                    let slot = (*owner).registry.slot(id, 0);
                    !slot.is_null() && crate::font::xp_font_coverage(slot.read(), *cp)
                });
                if coverage {
                    for (primary, points) in s.ui.iter().zip(control::UI_POINTS) {
                        let fallback = load(
                            owner,
                            family,
                            points,
                            control::variant_id(id, points),
                            Mode::ExtraSize,
                        );
                        if fallback != 0 {
                            (*owner.cast_mut())
                                .registry
                                .set_fallback(*primary, fallback);
                        } else {
                            log(Event::MissingUi, wanted, u32::from(points), 0, false);
                        }
                    }
                } else {
                    log(Event::NoCoverage, wanted, 0, 0, false);
                }
                log(
                    if t.boot {
                        Event::LoadedBoot
                    } else {
                        Event::Loaded
                    },
                    wanted,
                    0,
                    0,
                    false,
                );
                t.step = DONE;
            }
            DONE => {
                if t.boot {
                    log(
                        Event::Ready,
                        c"",
                        catalog(owner).families().len() as u32,
                        0,
                        false,
                    );
                }
                return emit(owner, t, 0, Action::Complete);
            }
            _ => return Action::Rejected,
        }
    }
}
unsafe fn clear(owner: *const Owner, s: &Settings, t: &mut Continuation) -> Action {
    *s.family = 0;
    let points = match control(owner).update(catalog(owner), c"", *s.points, false) {
        Update::Builtin { points, .. } => points,
        _ => unreachable!(),
    };
    *s.points = points;
    emit(owner, t, DONE, Action::PersistSettings)
}

// Native consumers copy these records and label the choices; selection stays here.
#[repr(C)]
pub(crate) struct ChoiceView {
    pub owner: *const Owner,
    pub family: *const u8,
    pub length: usize,
    pub names: *const c_void,
    pub count: usize,
    pub stride: usize,
    pub sizes: [u32; 8],
    pub reserve: usize,
}
impl ChoiceView {
    pub const EMPTY: Self = Self {
        owner: ptr::null(),
        family: ptr::null(),
        length: 0,
        names: ptr::null(),
        count: 0,
        stride: 0,
        sizes: [0; 8],
        reserve: 0,
    };
    pub unsafe fn prepare(&mut self) {
        (self.sizes, self.reserve) = selectable(self.owner, bytes(self.family, self.length));
        if !self.owner.is_null() {
            let families = catalog(self.owner).families();
            self.names = families.as_ptr().cast();
            self.count = families.len();
            self.stride = core::mem::size_of::<xp_core::sdfont::catalog::Family>();
        }
    }
}
pub(crate) unsafe fn selectable(owner: *const Owner, name: &[u8]) -> ([u32; 8], usize) {
    let family = if owner.is_null() || name.is_empty() {
        None
    } else {
        catalog(owner)
            .families()
            .iter()
            .find(|f| f.name().to_bytes() == name)
    };
    let mut sizes = [0u32; 8];
    let mut set = |size: u8| sizes[usize::from(size / 32)] |= 1 << (size % 32);
    let reserve = match family {
        Some(f) if f.vector => {
            for size in 8..=22 {
                set(size);
            }
            15
        }
        Some(f) if !f.files().is_empty() => {
            for file in f.files() {
                set(file.size);
            }
            0 // Native bitmap choices preserve incremental vector growth.
        }
        _ => {
            for size in [12, 14, 16, 18] {
                set(size);
            }
            4
        }
    };
    (sizes, reserve)
}
pub(crate) unsafe fn choices(owner: *const Owner, name: &[u8]) -> Option<alloc::vec::Vec<u8>> {
    let (present, _) = selectable(owner, name);
    let count = present.iter().map(|b| b.count_ones() as usize).sum();
    let mut sizes = alloc::vec::Vec::new();
    sizes.try_reserve_exact(count).ok()?;
    for size in 0..256 {
        if present[size / 32] & (1 << (size % 32)) != 0 {
            sizes.push(size as u8);
        }
    }
    Some(sizes)
}

const _: () = {
    assert!(
        core::mem::size_of::<ChoiceView>()
            == if cfg!(target_pointer_width = "32") {
                60
            } else {
                88
            }
    );
    assert!(
        core::mem::size_of::<Continuation>()
            == if cfg!(target_pointer_width = "32") {
                24
            } else {
                40
            }
    );
    assert!(
        core::mem::size_of::<Settings>()
            == if cfg!(target_pointer_width = "32") {
                if cfg!(any(test, feature = "app-test")) {
                    88
                } else {
                    24
                }
            } else if cfg!(any(test, feature = "app-test")) {
                120
            } else {
                32
            }
    );
};
