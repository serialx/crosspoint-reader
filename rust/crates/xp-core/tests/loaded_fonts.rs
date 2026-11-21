use core::{cell::UnsafeCell, mem::MaybeUninit, ptr, sync::atomic::AtomicU32};
use xp_core::font::loaded::{ensure, Entry, Registry};
use xp_core::font::raw::FontData;

fn registry(generation: &AtomicU32) -> Registry {
    let mut out = MaybeUninit::uninit();
    unsafe {
        Registry::initialize(out.as_mut_ptr(), generation);
        out.assume_init()
    }
}

fn entry(id: i32, regular: *const *const FontData) -> Entry {
    Entry {
        id,
        slots: [regular, ptr::null(), ptr::null(), ptr::null()],
        fallback: 0,
        points: 0,
    }
}

#[test]
fn duplicate_registration_preserves_original_and_iteration_is_sorted() {
    let generation = AtomicU32::new(1);
    let mut registry = registry(&generation);
    let first = ptr::null();
    let second = ptr::null();
    assert!(registry.insert(entry(9, &first)));
    assert_ne!(generation.load(core::sync::atomic::Ordering::Relaxed), 1);
    assert!(registry.insert(entry(-2, &second)));
    assert!(!registry.insert(entry(9, &second)));
    assert_eq!(registry.at(0).unwrap().id, -2);
    assert_eq!(registry.find(9).unwrap().slots[0], &first);
    registry.set_fallback(9, -2);
    assert_eq!(registry.find(9).unwrap().fallback, -2);
    registry.remove(-2);
    assert!(registry.find(-2).is_none());
    registry.clear_fallbacks();
    assert_eq!(registry.find(9).unwrap().fallback, 0);
}

#[test]
fn full_registry_rejects_new_entry_without_losing_existing_fonts() {
    let generation = AtomicU32::new(1);
    let mut registry = registry(&generation);
    let data = ptr::null();
    for id in 1..=16 {
        assert!(registry.insert(entry(id, &data)));
    }
    assert!(!registry.insert(entry(17, &data)));
    assert_eq!(registry.len(), 16);
    for id in 1..=16 {
        assert!(registry.find(id).is_some());
    }
    registry.remove(8);
    assert!(registry.insert(entry(17, &data)));
}

#[test]
fn style_fallback_keeps_stable_slot_instead_of_caching_font_data() {
    let first = MaybeUninit::<FontData>::zeroed();
    let second = MaybeUninit::<FontData>::zeroed();
    let slot = UnsafeCell::new(first.as_ptr());
    let mut value = entry(1, slot.get().cast_const());
    let bold = second.as_ptr();
    value.slots[1] = &bold;
    assert_eq!(value.slot(3), &bold);
    assert_eq!(value.slot(2), slot.get().cast_const());
    unsafe {
        slot.get().write(second.as_ptr());
    }
    assert_eq!(unsafe { value.slot(2).read() }, second.as_ptr());
    assert_eq!(unsafe { slot.get().read() }, second.as_ptr());
}

#[test]
fn missing_font_loader_can_reenter_registry_and_must_register_to_succeed() {
    let generation = AtomicU32::new(1);
    let mut registry = registry(&generation);
    let data = ptr::null();
    let raw = &raw mut registry;
    unsafe {
        assert!(!ensure(raw, 7, |_| true));
        assert!(ensure(raw, 7, |id| (*raw).insert(entry(id, &data))));
        assert!(ensure(raw, 7, |_| panic!("loaded font must not reload")));
        (*raw).remove(7);
        assert!(!ensure(raw, 7, |_| false));
    }
}
