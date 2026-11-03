extern crate alloc;

use super::super::{
    abi, app_abi,
    state::{ChildIntent, LoadFailure},
    EpubSession, Text,
};
use super::NavigationHooks;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct RecoverySeed {
    pub native: *mut c_void,
    pub popup: *mut c_void,
    pub path: app_abi::Text,
    pub failure: u8,
    pub context: *mut c_void,
    pub finish: unsafe extern "C" fn(*mut c_void),
    pub push: unsafe extern "C" fn(*mut c_void, *mut c_void),
    pub reopen: unsafe extern "C" fn(*mut c_void, app_abi::Text),
    pub update: unsafe extern "C" fn(*mut c_void),
}

fn failure(value: u8) -> LoadFailure {
    match value {
        0 => LoadFailure::None,
        1 => LoadFailure::Expired,
        2 => LoadFailure::Unverified,
        3 => LoadFailure::Protected,
        _ => panic!("invalid fixture failure"),
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_create(
    seed: *const RecoverySeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    let path = seed.path.bytes().expect("fixture path");
    let Some(mut owner) = EpubSession::try_new(path, false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.state.get_mut().ready = true;
    owner.state.get_mut().failure = failure(seed.failure);
    owner.guarded.get_mut().native = Some(NonNull::new(seed.native).expect("fixture root"));
    owner.guarded.get_mut().failure_popup = Some(NonNull::new(seed.popup).expect("fixture popup"));
    owner.navigation_hooks = Some(NavigationHooks {
        context: seed.context,
        finish: seed.finish,
        push: seed.push,
        reopen: seed.reopen,
        update: seed.update,
    });
    Box::into_raw(owner).cast()
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    assert!(owner.guarded.get_mut().section.is_none());
    // Native root, popup and child lifetimes belong to this host fixture.
    owner.guarded.get_mut().native = None;
    owner.guarded.get_mut().failure_popup = None;
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_seed_failure(owner: *mut c_void, value: u8) {
    (&*owner.cast::<EpubSession>()).model(|state| state.failure = failure(value));
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_seed_path(
    owner: *mut c_void,
    path: app_abi::Text,
) -> u8 {
    let Some(bytes) = path.bytes() else {
        return 0;
    };
    let Ok(path) = Text::copy(bytes) else {
        return 0;
    };
    // Fixture-only reseed between operations; all retained callbacks are dormant.
    (&mut *owner.cast::<EpubSession>()).path = path.into();
    1
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_handle(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).handle_load_failure())
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_begin(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).begin_loan_time_sync();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_wifi_result(owner: *mut c_void, cancelled: u8) {
    let owner = &*owner.cast::<EpubSession>();
    assert!(owner.model(|state| matches!(state.child, ChildIntent::Wifi)));
    owner.loan_wifi_result(cancelled != 0);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_recovery_render(owner: *mut c_void) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    // The retained native render wrapper supplies the caller-held lock.
    let access = owner.borrowed_resources();
    u8::from(owner.render_load_failure(&access))
}
