extern crate alloc;

use super::super::{abi, app_abi, state::Overlay, EpubSession};
use super::{BuildObservation, BuildSeed, BuildingObserver, DeferredOverride};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_create_legacy_section(
    seed: *const BuildSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_section: &'static super::legacy_section::LegacySectionServices,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    // Fixture construction finishes before the owner becomes callable.
    let state = owner.state.get_mut();
    state.ready = true;
    state.current.spine = seed.spine;
    state.current.page = seed.active_page;
    state.next_page = seed.next_page;
    state.failed_build_spine = seed.failed_spine;
    state.base.refresh_counter = seed.refresh;
    state.facts.radio_released = seed.radio_released != 0;
    state.heap_paused = seed.heap_paused != 0;
    state.build_popup = seed.popup != 0;
    state.overlay = match seed.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        _ => panic!("fixture overlay"),
    };
    state.manual_turn = seed.pending_turn;
    state.facts.frame_stale = seed.stale != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = Some(NonNull::new(seed.native).expect("fixture resources"));
    guarded.section = super::legacy_section::SectionSlot::legacy(seed.section, legacy_section);
    owner.building_observer = Some(BuildingObserver {
        context: seed.context,
        read: seed.building_read,
    });
    owner.deferred_override = Some(DeferredOverride {
        context: seed.context,
        apply: seed.reposition,
    });
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_destroy(owner: *mut c_void) {
    let owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    // The native resource root is the fixture itself, not a transferred allocation.
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_observe(
    owner: *mut c_void,
    out: *mut BuildObservation,
) {
    let owner = &*owner.cast::<EpubSession>();
    *out = owner.model(|state| BuildObservation {
        next_page: state.next_page,
        failed_spine: state.failed_build_spine,
        refresh: state.base.refresh_counter,
        radio_released: u8::from(state.facts.radio_released),
        heap_paused: u8::from(state.heap_paused),
        popup: u8::from(state.build_popup),
        stale: u8::from(state.facts.frame_stale),
    });
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_room(owner: *mut c_void) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    u8::from(owner.make_build_room(&access))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_heap(owner: *mut c_void) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    u8::from(owner.build_heap_gate(&access))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_popup(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.show_build_popup(&access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_buttons(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).buttons_pressed())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_background(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).background_build();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_wanted(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).build_wanted())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_build_delay(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).skip_loop_delay())
}
