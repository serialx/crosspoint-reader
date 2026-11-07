use super::super::{app_abi, EpubSession};
use crate::app::{
    clippings::record::Record,
    clippings_facade::{ClippingService, NativeStore},
    clippings_native::{Native, Physical},
    clippings_service::{Phase, Service},
    sync::ModelCell,
};
use core::{cell::Cell, ffi::c_void};

type CountObserver = unsafe extern "C" fn(u32);
type RowObserver = unsafe extern "C" fn(u32, *const Record);
type InsertObserver =
    unsafe extern "C" fn(*const Record, app_abi::Text, app_abi::XpAppNativeText, u8);
type RemoveObserver = unsafe extern "C" fn(u32, u8);

pub(in crate::app::epub) struct Fixture {
    pub service: ClippingService,
    pub enabled: Cell<bool>,
    pub count_observer: Cell<Option<CountObserver>>,
    pub row_observer: Cell<Option<RowObserver>>,
    pub insert: Cell<Option<InsertObserver>>,
    pub remove: Cell<Option<RemoveObserver>>,
}

impl Fixture {
    pub fn new(runtime: &'static app_abi::Runtime) -> Self {
        Self {
            service: Service::new(ModelCell::new(Phase::Idle, runtime)),
            enabled: Cell::new(false),
            count_observer: Cell::new(None),
            row_observer: Cell::new(None),
            insert: Cell::new(None),
            remove: Cell::new(None),
        }
    }
}

impl Fixture {
    pub fn inserted(
        &self,
        row: &Record,
        chapter: app_abi::Text,
        text: app_abi::XpAppNativeText,
        result: u8,
    ) {
        if let Some(observe) = self.insert.get() {
            unsafe { observe(row, chapter, text, result) };
        }
    }
    pub fn removed(&self, index: u32, result: bool) {
        if let Some(observe) = self.remove.get() {
            unsafe { observe(index, result as u8) };
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_results(
    owner: *mut c_void,
    insert: Option<InsertObserver>,
    remove: Option<RemoveObserver>,
) {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    owner.clipping_fixture.insert.set(insert);
    owner.clipping_fixture.remove.set(remove);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_load(owner: *mut c_void, path: app_abi::Text) -> u8 {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    let Some(path) = (unsafe { path.bytes() }) else {
        return 0;
    };
    owner
        .clipping_store()
        .and_then(|service| service.with(|store| store.load(path, b"", b"", b"epub")))
        .unwrap_or(false) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_insert(
    owner: *mut c_void,
    row: *const Record,
    chapter: app_abi::Text,
    text: app_abi::XpAppNativeText,
) -> u8 {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    owner.insert_clipping(unsafe { *row }, chapter, text) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_remove(owner: *mut c_void, index: u32) -> u8 {
    unsafe { &*owner.cast::<EpubSession>() }.remove_clipping(index) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_snapshot(
    owner: *mut c_void,
    index: u32,
    out: *mut Record,
    valid: *mut u8,
) -> u32 {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    unsafe { *valid = 0 };
    owner
        .clipping_store()
        .and_then(|service| {
            service.read(|store| {
                if let Some(row) = store.row(index as usize) {
                    unsafe {
                        *out = *row;
                        *valid = 1
                    };
                }
                store.count() as u32
            })
        })
        .unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_try_text(owner: *mut c_void, index: u32) -> u8 {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    let Some(operation) = owner.clipping_store().and_then(|store| store.operation()) else {
        return 0;
    };
    operation.read(|store| store.read_into(index as usize, &mut [0; 4]).is_some()) as u8
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_try_unload(owner: *mut c_void) -> u8 {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    let Some(mut operation) = owner.clipping_store().and_then(|store| store.operation()) else {
        return 0;
    };
    let _access = owner.lock_resources();
    operation.update(|store| store.unload());
    1
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_teardown(owner: *mut c_void) {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    let access = owner.lock_resources();
    owner.teardown_book(&access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_reset(owner: *mut c_void) {
    let fixture = &unsafe { &*owner.cast::<EpubSession>() }.clipping_fixture;
    assert!(fixture.service.reset_fixture());
    fixture.enabled.set(false);
    fixture.count_observer.set(None);
    fixture.row_observer.set(None);
    fixture.insert.set(None);
    fixture.remove.set(None);
}

// Metadata-only fixtures never perform storage I/O. Persistence fixtures supply
// their physical table and use the same NativeStore as the application.
macro_rules! unavailable {
    ($name:ident($($arg:ident: $ty:ty),*) -> $result:ty) => {
        unsafe extern "C" fn $name($($arg: $ty),*) -> $result {
            panic!("metadata fixture storage")
        }
    };
}
unavailable!(open(_path: app_abi::Text, _mode: u32) -> *mut c_void);
unavailable!(offset(_file: *mut c_void, _offset: u32) -> u8);
unavailable!(available(_file: *mut c_void) -> i32);
unavailable!(directory(_file: *mut c_void) -> u8);
unavailable!(next(_file: *mut c_void, _failed: *mut u8) -> *mut c_void);
unavailable!(name(_file: *mut c_void, _out: *mut u8, _capacity: u32) -> u32);
unavailable!(epoch() -> i64);
unavailable!(random(_out: *mut u8, _length: u32) -> ());
const NO_STORAGE: Physical = Physical {
    open,
    seek_relative: offset,
    available,
    truncate: offset,
    is_directory: directory,
    next,
    name,
    epoch_seconds: epoch,
    random,
};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_seed_metadata(
    owner: *mut c_void,
    rows: *const Record,
    count: usize,
    observer: Option<CountObserver>,
    row_observer: Option<RowObserver>,
) -> u8 {
    let owner = unsafe { &*owner.cast::<EpubSession>() };
    let rows = if count == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(rows, count) }
    };
    let fixture = &owner.clipping_fixture;
    if !fixture.service.install(|| {
        NativeStore::try_new(Native {
            leaves: owner.leaves,
            resources: owner.resources,
            physical: unsafe { owner.leaves.clipping_physical.as_ref() }.unwrap_or(&NO_STORAGE),
        })
    }) {
        return 0;
    }
    fixture.enabled.set(true);
    fixture.count_observer.set(observer);
    fixture.row_observer.set(row_observer);
    fixture
        .service
        .with(|store| store.seed_metadata(rows))
        .unwrap_or(false) as u8
}

// Observation only: these hooks cannot replace storage or choose its result.
#[no_mangle]
pub unsafe extern "C" fn xp_app_test_clipping_observe(
    loaded: Option<unsafe extern "C" fn(*const app_abi::Book, u8)>,
    unloaded: Option<unsafe extern "C" fn()>,
) {
    let app = crate::app::app().expect("initialized App");
    app.clipping_loaded_observer.set(loaded);
    app.clipping_unloaded_observer.set(unloaded);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_observe(
    owner: *mut c_void,
    count: Option<CountObserver>,
    row: Option<RowObserver>,
) {
    let fixture = &unsafe { &*owner.cast::<EpubSession>() }.clipping_fixture;
    assert!(
        !fixture.enabled.get(),
        "App composition must use the App store"
    );
    fixture.count_observer.set(count);
    fixture.row_observer.set(row);
}

#[no_mangle]
pub unsafe extern "C" fn xp_app_test_clipping_capacities(out: *mut usize) {
    let capacities = crate::app::app()
        .expect("initialized App")
        .clippings
        .read(|store| store.retained_string_capacities())
        .expect("installed store");
    unsafe { core::ptr::copy_nonoverlapping(capacities.as_ptr(), out, capacities.len()) };
}
