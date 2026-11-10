use super::*;
use core::mem::MaybeUninit;
use std::{ffi::CStr, sync::atomic::AtomicU32};

#[test]
fn small_names_and_growth_keep_the_c3_request_sequence() {
    MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    let mut name = catalog_storage::Name::new();
    assert!(name.assign(b"123456789012345", &ALLOCATOR));
    MEMORY.with(|m| assert_eq!(m.borrow().attempts, 0));
    assert!(name.assign(b"1234567890123456", &ALLOCATOR));
    MEMORY.with(|m| assert_eq!(m.borrow().live[0].1.size(), 31));
    assert!(name.assign(&[b'x'; 40], &ALLOCATOR));
    MEMORY.with(|m| assert_eq!(m.borrow().live[0].1.size(), 61));
    assert!(name.assign(b"", &ALLOCATOR));
    assert_eq!(name.cstr(), c"");
    MEMORY.with(|m| assert_eq!(m.borrow().live[0].1.size(), 61));
    name.release(&ALLOCATOR);
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
}

#[test]
fn catalog_every_allocation_failure_and_rediscovery_releases_owners() {
    MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    let result = catalog_fixture::snapshot();
    assert_eq!(
        result.lines().filter(|s| s.starts_with("FAMILY ")).count(),
        9
    );
    let attempts = MEMORY.with(|m| {
        let m = m.borrow();
        assert!(m.live.is_empty());
        m.attempts
    });
    for fail_at in 1..=attempts {
        MEMORY.with(|m| {
            *m.borrow_mut() = Memory {
                fail_at,
                ..Memory::default()
            }
        });
        let mut directory = catalog_fixture::Directory::new(9);
        directory.mixed = true;
        directory.sizes = 1;
        {
            let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
            let _ = catalog.discover(&mut directory, true);
        }
        MEMORY.with(|m| assert!(m.borrow().live.is_empty(), "failure {fail_at}"));
    }
    MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    {
        let mut directory = catalog_fixture::Directory::new(129);
        let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
        assert_eq!(catalog.discover(&mut directory, false), Ok(true));
        assert_eq!(catalog.families().len(), 128);
        assert!(catalog
            .families()
            .windows(2)
            .all(|w| w[0].name().to_bytes() < w[1].name().to_bytes()));
        directory.families = 0;
        assert_eq!(catalog.discover(&mut directory, false), Ok(false));
    }
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
}

#[test]
fn vector_selection_retains_only_selected_filename_owners() {
    MEMORY.with(|m| *m.borrow_mut() = Memory::default());
    {
        let mut directory = catalog_fixture::Directory::new(1);
        directory.only_vector = true;
        let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
        assert_eq!(catalog.discover(&mut directory, true), Ok(true));
        let family = &catalog.families()[0];
        assert!(family.vector);
        assert_eq!(family.files().len(), 4);
        assert_eq!(
            family.files().iter().map(|f| f.style).collect::<Vec<_>>(),
            vec![0, 1, 2, 3]
        );
    }
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
}

fn registry(generation: &AtomicU32) -> font::loaded::Registry {
    let mut registry = MaybeUninit::uninit();
    unsafe {
        font::loaded::Registry::initialize(registry.as_mut_ptr(), generation);
        registry.assume_init()
    }
}
fn face<'a>(shared: &'a IntervalRegistry, path: &CStr) -> Option<owner::FaceOwner<'a>> {
    let state = runtime_tests::state(0);
    let mut files = std::pin::pin!(io::Files::new(runtime_tests::Physical(&state)));
    unsafe { owner::FaceOwner::load(path, files.as_mut(), ALLOCATOR, shared) }
}

#[test]
fn loaded_family_keeps_fresh_update_decisions_and_stable_registry_slots() {
    let shared = IntervalRegistry::new();
    let generation = AtomicU32::new(0);
    let mut registry = registry(&generation);
    {
        let mut directory = catalog_fixture::Directory::new(1);
        let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
        catalog.discover(&mut directory, false).unwrap();
        let family = &catalog.families()[0];
        let mut control = unsafe { control::Control::new(&ALLOCATOR) };
        control.begin().unwrap();
        let id = unsafe {
            control.load(
                family,
                15,
                0,
                font::manager::Mode::Replace,
                &mut registry,
                |p| face(&shared, p),
            )
        }
        .unwrap();
        assert_ne!(id, 0);
        assert_eq!(control.points(), 14);
        assert!(registry.contains(id));
        let slot = registry.slot(id, 0);
        assert!(!slot.is_null());
        let data = unsafe { slot.read() };
        assert_eq!(
            control.update(&catalog, c"F0", 15, false),
            control::Update::Keep { points: 14 }
        );
        assert_eq!(
            control.update(&catalog, c"F0", 15, true),
            control::Update::Reload {
                unload: true,
                snap_before: Some(14)
            }
        );
        let extra = unsafe {
            control.load(
                family,
                16,
                900,
                font::manager::Mode::ExtraSize,
                &mut registry,
                |p| face(&shared, p),
            )
        }
        .unwrap();
        assert_eq!(extra, 900);
        assert_eq!(unsafe { slot.read() }, data);
        assert_eq!(
            unsafe {
                control.load(
                    family,
                    16,
                    901,
                    font::manager::Mode::ExtraSize,
                    &mut registry,
                    |_| panic!("reused"),
                )
            },
            Ok(900)
        );
        assert_eq!(
            unsafe {
                control.load(
                    family,
                    18,
                    900,
                    font::manager::Mode::Variant,
                    &mut registry,
                    |p| face(&shared, p),
                )
            },
            Err(control::LoadError::Collision)
        );
        assert_eq!(
            control.update(&catalog, c"", 15, false),
            control::Update::Builtin {
                unload: true,
                points: 14
            }
        );
        directory.families = 0;
        catalog.discover(&mut directory, false).unwrap();
        assert_eq!(
            control.update(&catalog, c"F0", 14, false),
            control::Update::Disappeared
        );
        unsafe { control.unload(&mut registry, None) };
        assert!(registry.is_empty());
        assert_eq!(control.family(), c"");
    }
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
}

#[test]
fn variant_heap_floor_ui_reuse_lru_eviction_and_reload() {
    let shared = IntervalRegistry::new();
    let generation = AtomicU32::new(0);
    let mut registry = registry(&generation);
    {
        let mut directory = catalog_fixture::Directory::new(1);
        directory.sizes = 9;
        let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
        catalog.discover(&mut directory, false).unwrap();
        let family = &catalog.families()[0];
        let mut control = unsafe { control::Control::new(&ALLOCATOR) };
        control.begin().unwrap();
        let reader = unsafe {
            control.load(
                family,
                16,
                0,
                font::manager::Mode::Replace,
                &mut registry,
                |p| face(&shared, p),
            )
        }
        .unwrap();
        let ui = control::variant_id(reader, 14);
        unsafe {
            control.load(
                family,
                14,
                ui,
                font::manager::Mode::ExtraSize,
                &mut registry,
                |p| face(&shared, p),
            )
        }
        .unwrap();
        assert_eq!(
            unsafe { control.variant(family, 14, ui, 0, &mut registry, |_| panic!("UI reload")) },
            Ok(true)
        );
        let first = control::variant_id(reader, 18);
        assert_eq!(
            unsafe {
                control.variant(family, 18, first, 48 * 1024 - 1, &mut registry, |_| {
                    panic!("below floor")
                })
            },
            Ok(false)
        );
        for points in [18, 20, 22, 24, 26, 28] {
            let id = control::variant_id(reader, points);
            assert_eq!(
                unsafe {
                    control.variant(family, points, id, 48 * 1024, &mut registry, |p| {
                        face(&shared, p)
                    })
                },
                Ok(true)
            );
        }
        assert!(!registry.contains(first));
        assert!(registry.contains(ui));
        assert!(registry.contains(reader));
        assert_eq!(
            unsafe {
                control.variant(family, 18, first, 48 * 1024, &mut registry, |p| {
                    face(&shared, p)
                })
            },
            Ok(true)
        );
        assert!(registry.contains(first));
        assert_eq!(control::choose_variant(family, 16, 0.95), 14);
        assert_eq!(control::choose_variant(family, 16, 1.2), 20);
        assert_eq!(control::builtin_variant(&[1, 2, 3, 4], 2, 0.9), 1);
        unsafe { control.unload(&mut registry, None) };
    }
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
}

#[test]
fn skipped_long_paths_keep_the_full_diagnostic_directory() {
    struct LongPath {
        root: bool,
        file: bool,
        logged: Option<String>,
    }
    unsafe impl catalog::Directory for LongPath {
        fn open(&mut self, path: &CStr, root: bool) -> i32 {
            if !root || path == c"/.fonts" {
                1
            } else {
                0
            }
        }
        fn close(&mut self, _: bool) {}
        fn inspect(&mut self, _: &CStr) -> Option<(u16, bool)> {
            None
        }
        fn next(&mut self, root: bool, name: &mut [u8; 128], directory: &mut bool) -> bool {
            let used = if root { &mut self.root } else { &mut self.file };
            if *used {
                return false;
            }
            *used = true;
            name.fill(0);
            *directory = root;
            if root {
                name[..100].fill(b'A');
            } else {
                name[..80].fill(b'B');
                name[80..90].copy_from_slice(b"_14.cpfont");
            }
            true
        }
        fn log(&mut self, event: u32, _: &CStr, b: Option<&CStr>, _: usize, _: usize) {
            if event == 7 {
                self.logged = Some(b.unwrap().to_str().unwrap().to_owned());
            }
        }
    }
    let mut physical = LongPath {
        root: false,
        file: false,
        logged: None,
    };
    {
        let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
        assert_eq!(catalog.discover(&mut physical, false), Ok(false));
    }
    assert_eq!(
        physical.logged,
        Some(format!("/.fonts/{}", "A".repeat(100)))
    );
    MEMORY.with(|m| assert!(m.borrow().live.is_empty()));
}
