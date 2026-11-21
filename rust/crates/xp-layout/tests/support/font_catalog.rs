use super::font_registry::xp_font_registry_drop;
use super::*;
use core::ffi::{c_char, CStr};
use std::cell::RefCell;
use xp_core::sdfont::io::Slot;

#[derive(Default)]
struct Fixture {
    enabled: bool,
    vector: bool,
    long_name: bool,
    allocations: usize,
    operations: usize,
    fail_alloc: usize,
    fail_io: usize,
}
thread_local! { static FIXTURE: RefCell<Fixture> = RefCell::new(Fixture::default()); }
// Third slot word: nonzero once the slot holds a HAL handle, as native slots do.
const HANDLE_WORD: usize = 2;
unsafe fn has_handle(slot: *mut Slot) -> bool {
    slot.cast::<usize>().add(HANDLE_WORD).read() != 0
}
pub(super) fn reset(fail_alloc: usize, fail_io: usize) {
    FIXTURE.with(|f| {
        *f.borrow_mut() = Fixture {
            enabled: true,
            fail_alloc,
            fail_io,
            ..Fixture::default()
        }
    });
}
pub(super) fn fail_alloc(domain: u8) -> bool {
    FIXTURE.with(|f| {
        let mut f = f.borrow_mut();
        if !f.enabled || domain == 2 {
            return false;
        }
        f.allocations += 1;
        f.allocations == f.fail_alloc
    })
}
pub(super) fn fail_io() -> bool {
    FIXTURE.with(|f| {
        let mut f = f.borrow_mut();
        if !f.enabled {
            return false;
        }
        f.operations += 1;
        f.operations == f.fail_io
    })
}
pub(super) fn largest() -> usize {
    FIXTURE.with(|f| if f.borrow().enabled { 200 * 1024 } else { 0 })
}
pub(super) unsafe fn directory_open(slot: *mut Slot, path: *const c_char) -> i32 {
    // Native assigns a HalFile to the slot for every open attempt, including a failed one.
    slot.cast::<usize>().add(HANDLE_WORD).write(1);
    slot.cast::<usize>().write(0);
    slot.cast::<usize>().add(1).write(0);
    let enabled = FIXTURE.with(|f| f.borrow().enabled);
    if !enabled {
        return 0;
    }
    let path = CStr::from_ptr(path);
    let node = if path == c"/.fonts" {
        1
    } else if path == c"/.fonts/ShortFont" || path == c"/.fonts/AReallyLongFamilyName" {
        2
    } else {
        return 0;
    };
    slot.cast::<usize>().write(node);
    slot.cast::<usize>().add(1).write(0);
    1
}
pub(super) unsafe fn directory_next(slot: *mut Slot, name: *mut u8, directory: *mut bool) -> bool {
    assert!(
        has_handle(slot),
        "directory_next on a slot with no HAL handle"
    );
    let node = slot.cast::<usize>().read();
    let index = slot.cast::<usize>().add(1).read();
    if node == 0 {
        return false;
    }
    let vector = FIXTURE.with(|f| f.borrow().vector);
    let long_name = FIXTURE.with(|f| f.borrow().long_name);
    let names: &[&CStr] = if node == 1 && long_name {
        &[c"AReallyLongFamilyName"]
    } else if node == 1 {
        &[c"ShortFont"]
    } else if vector {
        &[c"ShortFont-Regular.ttf", c"ShortFont-Bold.ttf"]
    } else if long_name {
        &[
            c"AReallyLongFamilyName_14.cpfont",
            c"AReallyLongFamilyName_18.cpfont",
        ]
    } else {
        &[c"ShortFont_14.cpfont", c"ShortFont_18.cpfont"]
    };
    let Some(value) = names.get(index) else {
        return false;
    };
    *directory = node == 1;
    ptr::copy_nonoverlapping(value.as_ptr().cast(), name, value.to_bytes_with_nul().len());
    slot.cast::<usize>().add(1).write(index + 1);
    true
}
// Mirror the native data declarations, so tests use the production ABI front door.
#[repr(C)]
struct Settings {
    family: *mut c_char,
    points: *mut u8,
    ui: [i32; 3],
    vectors: bool,
    vector_loaded: bool,
    vector_family: View,
    vector_index: usize,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Token {
    address: *const c_void,
    owner: *const Owner,
    request: *const c_void,
    epoch: u32,
    step: u32,
    dirty: bool,
    boot: bool,
}
#[repr(C)]
struct Query {
    operation: u32,
    index: usize,
    file: usize,
    name: *const u8,
    length: usize,
    output: *mut u8,
    capacity: usize,
}
#[repr(C)]
struct View {
    name: *const c_char,
    length: usize,
    root: *const c_char,
    files: *const c_void,
    count: usize,
    sizes: [u32; 8],
    id: i32,
    vector: bool,
    loose: bool,
}
fn family(name: &CStr) -> [c_char; 64] {
    let mut out = [0; 64];
    for (to, from) in out.iter_mut().zip(name.to_bytes()) {
        *to = *from as c_char;
    }
    out
}
unsafe fn run(
    owner: *mut Owner,
    family: &mut [c_char; 64],
    points: &mut u8,
    begin: bool,
) -> Vec<(Vec<u8>, u8)> {
    let mut settings = Settings {
        family: family.as_mut_ptr(),
        points,
        ui: [11, 12, 13],
        vectors: false,
        vector_loaded: false,
        vector_family: core::mem::zeroed(),
        vector_index: 0,
    };
    let mut token: Token = core::mem::zeroed();
    let mut action = if begin {
        font_sd::catalog::xp_sd_fonts_begin(
            owner,
            (&mut settings as *mut Settings).cast(),
            (&mut token as *mut Token).cast(),
        )
    } else {
        font_sd::catalog::xp_sd_fonts_ensure(
            owner,
            (&mut settings as *mut Settings).cast(),
            (&mut token as *mut Token).cast(),
        )
    };
    let mut saves = Vec::new();
    for _ in 0..6 {
        match action as u32 {
            0 => return saves,
            1 => saves.push((
                CStr::from_ptr(settings.family).to_bytes().to_vec(),
                *settings.points,
            )),
            other => panic!("unexpected action {other}"),
        }
        action = font_sd::catalog::xp_sd_fonts_ensure(
            owner,
            (&mut settings as *mut Settings).cast(),
            (&mut token as *mut Token).cast(),
        );
    }
    panic!("unbounded continuation")
}
pub(super) unsafe fn new_owner(generation: &AtomicU32) -> Box<core::mem::MaybeUninit<Owner>> {
    let mut storage = Box::<Owner>::new_uninit();
    xp_font_registry_init(storage.as_mut_ptr(), generation, ptr::null_mut());
    storage
}
#[test]
fn begin_keep_reload_and_variants_preserve_the_short_catalog_name() {
    unsafe {
        reset(0, 0);
        let generation = AtomicU32::new(1);
        let mut storage = new_owner(&generation);
        let owner = storage.as_mut_ptr();
        let (mut name, mut points) = (family(c"ShortFont"), 16);
        assert_eq!(
            run(owner, &mut name, &mut points, true),
            vec![(b"ShortFont".to_vec(), 14)]
        );
        let q = Query {
            operation: 1,
            index: 0,
            file: 0,
            name: ptr::null(),
            length: 0,
            output: ptr::null_mut(),
            capacity: 0,
        };
        let mut view = core::mem::MaybeUninit::<View>::uninit();
        assert_eq!(
            font_sd::catalog::xp_sd_catalog_query(
                owner,
                (&q as *const Query).cast(),
                view.as_mut_ptr().cast()
            ),
            1
        );
        let view = view.assume_init();
        assert_eq!(CStr::from_ptr(view.name), c"ShortFont");
        assert_ne!(view.id, 0);
        assert_eq!(view.count, 2);
        assert_eq!(view.sizes[0], (1 << 14) | (1 << 18));
        assert_ne!(font_sd::catalog::xp_sd_font_variant(owner, view.id, 1.4), 0);
        assert_eq!(CStr::from_ptr(view.name), c"ShortFont");
        points = 16;
        assert_eq!(
            run(owner, &mut name, &mut points, false),
            vec![(b"ShortFont".to_vec(), 14)]
        );
        points = 18;
        assert!(run(owner, &mut name, &mut points, false).is_empty());
        assert_eq!(CStr::from_ptr(view.name), c"ShortFont");
        xp_font_registry_drop(owner);
        SD_ALLOCATIONS.with(|live| assert!(live.borrow().is_empty()));
    }
}
#[test]
fn builtin_boot_does_not_snap_until_ensure_and_missing_selection_saves_once() {
    unsafe {
        reset(0, 0);
        let generation = AtomicU32::new(1);
        let mut storage = new_owner(&generation);
        let owner = storage.as_mut_ptr();
        let (mut name, mut points) = (family(c""), 15);
        assert!(run(owner, &mut name, &mut points, true).is_empty());
        assert_eq!(points, 15);
        assert_eq!(
            run(owner, &mut name, &mut points, false),
            vec![(vec![], 14)]
        );
        name = family(c"Missing");
        points = 15;
        assert_eq!(
            run(owner, &mut name, &mut points, false),
            vec![(vec![], 14)]
        );
        xp_font_registry_drop(owner);
        SD_ALLOCATIONS.with(|live| assert!(live.borrow().is_empty()));
    }
}
#[test]
fn stale_copied_foreign_and_completed_tokens_are_rejected() {
    unsafe {
        reset(0, 0);
        let generation = AtomicU32::new(1);
        let other_generation = AtomicU32::new(1);
        let mut storage = new_owner(&generation);
        let mut other = new_owner(&other_generation);
        let owner = storage.as_mut_ptr();
        let (mut name, mut points) = (family(c"Missing"), 15);
        let mut settings = Settings {
            family: name.as_mut_ptr(),
            points: &mut points,
            ui: [11, 12, 13],
            vectors: false,
            vector_loaded: false,
            vector_family: core::mem::zeroed(),
            vector_index: 0,
        };
        let request = (&mut settings as *mut Settings).cast();
        let mut token: Token = core::mem::zeroed();
        let original = (&mut token as *mut Token).cast();
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_begin(owner, request, original) as u32,
            1
        );
        let mut copied = token;
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_ensure(owner, request, (&mut copied as *mut Token).cast())
                as u32,
            4
        );
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_ensure(other.as_mut_ptr(), request, original) as u32,
            4
        );
        generation.fetch_add(1, Ordering::Relaxed);
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_ensure(owner, request, original) as u32,
            4
        );
        original.cast::<Token>().write(core::mem::zeroed());
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_ensure(owner, request, original) as u32,
            0
        );
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_ensure(owner, request, original) as u32,
            4
        );
        xp_font_registry_drop(owner);
        xp_font_registry_drop(other.as_mut_ptr());
        SD_ALLOCATIONS.with(|live| assert!(live.borrow().is_empty()));
    }
}
#[test]
fn every_fallible_face_allocation_and_file_operation_completes_and_releases() {
    unsafe {
        for failure in 0..2 {
            reset(0, 0);
            let generation = AtomicU32::new(1);
            let mut storage = new_owner(&generation);
            let (mut name, mut points) = (family(c"ShortFont"), 16);
            run(storage.as_mut_ptr(), &mut name, &mut points, true);
            let total = FIXTURE.with(|f| {
                let f = f.borrow();
                if failure == 0 {
                    f.allocations
                } else {
                    f.operations
                }
            });
            xp_font_registry_drop(storage.as_mut_ptr());
            for at in 1..=total {
                reset(
                    if failure == 0 { at } else { 0 },
                    if failure == 1 { at } else { 0 },
                );
                let mut storage = new_owner(&generation);
                let (mut name, mut points) = (family(c"ShortFont"), 16);
                let saves = run(storage.as_mut_ptr(), &mut name, &mut points, true);
                assert_eq!(saves.len(), 1, "class {failure} at {at}");
                xp_font_registry_drop(storage.as_mut_ptr());
                SD_ALLOCATIONS
                    .with(|live| assert!(live.borrow().is_empty(), "class {failure} at {at}"));
            }
        }
    }
}

#[test]
fn variant_ids_match_the_frozen_native_system() {
    let reader = 632843429;
    for (points, expected) in [
        (12, -1088137731),
        (14, -1323018537),
        (18, -1121691357),
        (20, -1490794859),
        (22, -1188805777),
        (24, -1557903135),
        (26, -1524349509),
    ] {
        assert_eq!(
            xp_core::sdfont::control::variant_id(reader, points),
            expected
        );
    }
}

#[test]
fn typed_chapter_receiver_keeps_sd_failures_out_of_the_native_fallback() {
    use xp_layout::chapter::assembly::runtime::FontOperation;
    unsafe extern "C" fn fallback(_: *mut c_void, request: *const FontOperation) -> i32 {
        assert_eq!((*request).font, 123);
        314
    }
    unsafe {
        reset(0, 0);
        let generation = AtomicU32::new(1);
        let mut storage = new_owner(&generation);
        let owner = storage.as_mut_ptr();
        let (mut name, mut points) = (family(c"ShortFont"), 14);
        run(owner, &mut name, &mut points, true);
        let reader = font_sd::xp_sd_font_id(owner, name.as_ptr().cast(), 9, 0);
        let extra = xp_core::sdfont::control::variant_id(reader, 18);
        FIXTURE.with(|fixture| {
            let mut f = fixture.borrow_mut();
            f.fail_alloc = f.allocations + 1;
        });
        let request = FontOperation {
            operation: 1,
            font: extra,
            argument: 0,
        };
        assert_eq!(
            font_sd::xp_sd_font_chapter(owner.cast(), &request, fallback),
            0
        );
        FIXTURE.with(|f| f.borrow_mut().fail_alloc = 0);
        let entry = xp_core::font::loaded::Entry {
            id: extra,
            slots: [ptr::null(); 4],
            fallback: 0,
            points: 0,
        };
        assert_eq!(font_registry::xp_font_registry_update(owner, 4, &entry), 1);
        assert_eq!(
            font_sd::xp_sd_font_chapter(owner.cast(), &request, fallback),
            extra
        );
        let variant = FontOperation {
            operation: 0,
            font: reader,
            argument: 1.4f32.to_bits(),
        };
        assert_eq!(
            font_sd::xp_sd_font_chapter(owner.cast(), &variant, fallback),
            extra
        );
        let builtin = FontOperation {
            operation: 0,
            font: 123,
            argument: 1.4f32.to_bits(),
        };
        assert_eq!(
            font_sd::xp_sd_font_chapter(owner.cast(), &builtin, fallback),
            314
        );
        let invalid = FontOperation {
            operation: 2,
            font: 123,
            argument: 0,
        };
        assert_eq!(
            font_sd::xp_sd_font_chapter(owner.cast(), &invalid, fallback),
            0
        );
        xp_font_registry_drop(owner);
        SD_ALLOCATIONS.with(|live| assert!(live.borrow().is_empty()));
    }
}

#[test]
fn vector_effect_lends_the_already_selected_family_and_sizes_are_direct() {
    unsafe {
        reset(0, 0);
        FIXTURE.with(|f| f.borrow_mut().vector = true);
        let generation = AtomicU32::new(1);
        let mut storage = new_owner(&generation);
        let owner = storage.as_mut_ptr();
        let (mut name, mut points) = (family(c"ShortFont"), 16);
        let mut settings = Settings {
            family: name.as_mut_ptr(),
            points: &mut points,
            ui: [11, 12, 13],
            vectors: true,
            vector_loaded: false,
            vector_family: core::mem::zeroed(),
            vector_index: 0,
        };
        let request = (&mut settings as *mut Settings).cast();
        let mut token: Token = core::mem::zeroed();
        let pending = (&mut token as *mut Token).cast();
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_begin(owner, request, pending) as u32,
            5
        );
        assert_eq!(
            font_sd::catalog::xp_sd_fonts_ensure(owner, request, pending) as u32,
            3
        );
        assert!(settings.vector_family.vector);
        assert_eq!(settings.vector_index, 0);
        assert_eq!(settings.vector_family.count, 2);
        let name = settings.vector_family.name;
        assert_eq!(CStr::from_ptr(name), c"ShortFont");
        font_registry::xp_font_registry_update(owner, 3, ptr::null());
        assert_eq!(CStr::from_ptr(name), c"ShortFont");
        assert_eq!(
            font_sd::catalog::choices(owner, b"ShortFont"),
            Some((8..=22).collect())
        );
        assert_eq!(
            font_sd::catalog::choices(owner, b"Missing"),
            Some(vec![12, 14, 16, 18])
        );
        assert_eq!(
            font_sd::catalog::choices(ptr::null(), b""),
            Some(vec![12, 14, 16, 18])
        );
        xp_font_registry_drop(owner);
        SD_ALLOCATIONS.with(|live| assert!(live.borrow().is_empty()));
    }
}

#[test]
fn native_choice_loan_keeps_inline_and_heap_names_and_size_policy_without_requests() {
    #[repr(C)]
    union Text {
        inline: [u8; 16],
        heap: *const u8,
    }
    #[repr(C)]
    struct Name {
        text: Text,
        length: usize,
        capacity: usize,
    }
    unsafe {
        for long_name in [false, true] {
            reset(0, 0);
            FIXTURE.with(|f| f.borrow_mut().long_name = long_name);
            let generation = AtomicU32::new(1);
            let mut storage = new_owner(&generation);
            let owner = storage.as_mut_ptr();
            font_sd::catalog::xp_sd_catalog_dirty(owner, 2, false);
            let expected = if long_name {
                c"AReallyLongFamilyName"
            } else {
                c"ShortFont"
            };
            let live = SD_ALLOCATIONS.with(|a| a.borrow().len());
            let mut choices = font_sd::catalog::ChoiceView::EMPTY;
            choices.owner = owner;
            choices.family = expected.as_ptr().cast();
            choices.length = expected.to_bytes().len();
            choices.prepare();
            assert_eq!(choices.count, 1);
            assert_eq!(choices.reserve, 0);
            assert_eq!(choices.sizes[0], (1 << 14) | (1 << 18));
            let name = &*choices.names.cast::<Name>();
            assert_eq!(name.length, expected.to_bytes().len());
            let pointer = if name.capacity == 15 {
                ptr::addr_of!(name.text.inline).cast()
            } else {
                name.text.heap
            };
            assert_eq!(CStr::from_ptr(pointer.cast()), expected);
            assert_eq!(
                font_sd::xp_sd_font_id(owner, pointer.cast(), name.length, 0),
                0
            );
            assert_eq!(
                font_sd::catalog::choices(owner, expected.to_bytes()),
                Some(vec![14, 18])
            );
            assert_eq!(
                font_sd::catalog::choices(owner, b"missing"),
                Some(vec![12, 14, 16, 18])
            );
            assert_eq!(CStr::from_ptr(pointer.cast()), expected);
            assert_eq!(SD_ALLOCATIONS.with(|a| a.borrow().len()), live);
            xp_font_registry_drop(owner);
            SD_ALLOCATIONS.with(|a| assert!(a.borrow().is_empty()));
        }
        let query = Query {
            operation: 5,
            index: 0,
            file: 0,
            name: ptr::null(),
            length: 0,
            output: ptr::null_mut(),
            capacity: 0,
        };
        let root = font_sd::catalog::xp_sd_catalog_query(
            ptr::null(),
            (&query as *const Query).cast(),
            ptr::null_mut(),
        );
        assert_ne!(root, 0);
        #[cfg(not(miri))]
        assert_eq!(CStr::from_ptr(root as *const c_char), c"/.fonts");
        let mut view = core::mem::MaybeUninit::<View>::zeroed();
        let query = Query {
            operation: 7,
            ..query
        };
        assert_eq!(
            font_sd::catalog::xp_sd_catalog_query(
                ptr::null(),
                (&query as *const Query).cast(),
                view.as_mut_ptr().cast()
            ),
            4
        );
        assert_eq!(
            view.assume_init().sizes[0],
            (1 << 12) | (1 << 14) | (1 << 16) | (1 << 18)
        );
    }
}
