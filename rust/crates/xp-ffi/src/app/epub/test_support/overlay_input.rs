use super::{
    super::{abi, app_abi, EpubSession},
    overlay_actions,
};
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app::epub) struct OverlayInputHooks {
    pub context: *mut c_void,
    pub open: unsafe extern "C" fn(*mut c_void, u8),
    pub close: unsafe extern "C" fn(*mut c_void),
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub text: unsafe extern "C" fn(*mut c_void),
    pub live: unsafe extern "C" fn(*mut c_void),
    pub text_popup: unsafe extern "C" fn(*mut c_void, i32),
    pub more: unsafe extern "C" fn(*mut c_void, i32),
    pub popup: unsafe extern "C" fn(*mut c_void),
    pub width: unsafe extern "C" fn(*mut c_void, i32),
    pub height: unsafe extern "C" fn(*mut c_void, i32),
}

#[repr(C)]
pub struct Seed {
    base: overlay_actions::Seed,
    spine: i32,
    next_page: i32,
    more_count: u8,
    hooks: OverlayInputHooks,
    push_child: unsafe extern "C" fn(*mut c_void, *mut c_void),
}

#[repr(C)]
pub struct Observation {
    spine: i32,
    next_page: i32,
    section: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_input_create(
    seed: *const Seed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    let raw = overlay_actions::xp_epub_test_overlay_create(&seed.base, leaves, runtime, resources);
    let Some(owner) = raw.cast::<EpubSession>().as_mut() else {
        return raw;
    };
    let state = owner.state.get_mut();
    state.current.spine = seed.spine;
    state.next_page = seed.next_page;
    assert!(seed.more_count <= 19);
    owner.guarded.get_mut().more_count = seed.more_count;
    owner.overlay_input_hooks = Some(seed.hooks);
    owner
        .navigation_hooks
        .as_mut()
        .expect("overlay fixture navigation")
        .push = seed.push_child;
    raw
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_input(raw: *mut c_void) {
    (&*raw.cast::<EpubSession>()).handle_overlay_input();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_input_result(
    raw: *mut c_void,
    result: *const app_abi::XpAppResultView,
) {
    (&*raw.cast::<EpubSession>()).child_result(result.as_ref());
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_input_observe(
    raw: *mut c_void,
    out: *mut Observation,
) {
    *out = (&*raw.cast::<EpubSession>()).model(|state| Observation {
        spine: state.current.spine,
        next_page: state.next_page,
        section: u8::from(state.facts.section.is_some()),
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_overlay_input_anchor(
    raw: *mut c_void,
    sink: app_abi::TextSink,
    context: *mut c_void,
) {
    sink(
        context,
        (*raw.cast::<EpubSession>()).guarded.fixture().anchor.wire(),
    );
}
