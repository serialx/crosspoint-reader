extern crate alloc;
use super::super::{abi, app_abi, EpubSession};
use super::{BaseHooks, NavigationHooks};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::base::REFRESH_MANUAL;

#[repr(C)]
pub struct Hooks {
    pub context: *mut c_void,
    pub enter: unsafe extern "C" fn(*mut c_void),
    pub exit: unsafe extern "C" fn(*mut c_void),
    pub orientation: unsafe extern "C" fn(*mut c_void),
    pub load: unsafe extern "C" fn(*mut c_void) -> u8,
    pub failure: unsafe extern "C" fn(*mut c_void) -> u8,
    pub at_end: unsafe extern "C" fn(*mut c_void) -> u8,
    pub return_end: unsafe extern "C" fn(*mut c_void),
    pub end_rendered: unsafe extern "C" fn(*mut c_void),
    pub back: unsafe extern "C" fn(*mut c_void) -> u8,
    pub format: unsafe extern "C" fn(*mut c_void) -> u8,
    pub touch: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub buttons: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::input::Turn,
    pub turn: unsafe extern "C" fn(*mut c_void, u8) -> u8,
    pub skip: unsafe extern "C" fn(*mut c_void, i32) -> u8,
    pub snapshot: unsafe extern "C" fn(*mut c_void) -> xp_ui::reader::Session,
    pub worthy: unsafe extern "C" fn(*mut c_void) -> u8,
    pub reset: unsafe extern "C" fn(*mut c_void),
    pub complete: unsafe extern "C" fn(*mut c_void),
    pub mark: unsafe extern "C" fn(*mut c_void),
    pub note: unsafe extern "C" fn(*mut c_void, u8, u8),
    pub force: unsafe extern "C" fn(*mut c_void),
    pub percent: unsafe extern "C" fn(*mut c_void) -> i32,
    pub home: unsafe extern "C" fn(*mut c_void),
    pub navigate: unsafe extern "C" fn(*mut c_void, app_abi::Text),
}
#[repr(C)]
pub struct Seed {
    pub native: *mut c_void,
    pub book: *mut c_void,
    pub end: *mut c_void,
    pub path: app_abi::Text,
    pub counter: i32,
    pub forced: u8,
    pub rendered: u8,
    pub remembered: u8,
    pub end_ready: u8,
    pub hooks: Hooks,
    pub finish: unsafe extern "C" fn(*mut c_void),
    pub update: unsafe extern "C" fn(*mut c_void),
}
#[repr(C)]
pub struct Observation {
    pub counter: i32,
    pub forced: u8,
    pub rendered: u8,
    pub remembered: u8,
    pub end_ready: u8,
    pub end: *mut c_void,
}
unsafe extern "C" fn unexpected_push(_: *mut c_void, _: *mut c_void) {
    panic!("unexpected base child");
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_create_legacy_book(
    seed: *const Seed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(
        seed.path.bytes().expect("fixture path"),
        false,
        3,
        &*leaves,
        &*runtime,
        &*resources,
    ) else {
        return core::ptr::null_mut();
    };
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    owner.legacy_pages.set(Some(&legacy_book.section.page));
    let state = owner.state.get_mut();
    // Standalone composition intentionally renders after early enter failures.
    // Entry-gate integration and real accounting have their own fixtures.
    state.ready = true;
    state.base.refresh_counter = seed.counter;
    state.base.refresh_requests = if seed.forced != 0 { REFRESH_MANUAL } else { 0 };
    state.base.rendered = seed.rendered != 0;
    state.base.remembered = seed.remembered != 0;
    state.base.end_ready = seed.end_ready != 0;
    let guarded = owner.guarded.get_mut();
    guarded.native = NonNull::new(seed.native);
    guarded.end_options = NonNull::new(seed.end);
    owner.base_hooks = Some(BaseHooks {
        context: seed.hooks.context,
        enter: seed.hooks.enter,
        exit: seed.hooks.exit,
        orientation: seed.hooks.orientation,
        load: seed.hooks.load,
        failure: seed.hooks.failure,
        at_end: seed.hooks.at_end,
        return_end: seed.hooks.return_end,
        end_rendered: seed.hooks.end_rendered,
        back: seed.hooks.back,
        format: seed.hooks.format,
        touch: seed.hooks.touch,
        buttons: seed.hooks.buttons,
        turn: Some(seed.hooks.turn),
        skip: Some(seed.hooks.skip),
        snapshot: seed.hooks.snapshot,
        worthy: seed.hooks.worthy,
        reset: seed.hooks.reset,
        complete: seed.hooks.complete,
        mark: seed.hooks.mark,
        note: seed.hooks.note,
        force: seed.hooks.force,
        percent: seed.hooks.percent,
        home: seed.hooks.home,
        navigate: seed.hooks.navigate,
    });
    owner.navigation_hooks = Some(NavigationHooks {
        context: seed.hooks.context,
        finish: seed.finish,
        update: seed.update,
        push: unexpected_push,
        reopen: seed.hooks.navigate,
    });
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        if let Some(end) = owner.take_end_options(&access) {
            (owner.leaves.end_close)(end.as_ptr());
        }
    }
    owner.guarded.get_mut().native = None;
    owner.discard_legacy_fixture();
    drop(owner);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_observe(owner: *mut c_void, out: *mut Observation) {
    let owner = &*owner.cast::<EpubSession>();
    let end = owner
        .guarded
        .fixture()
        .end_options
        .map_or(core::ptr::null_mut(), |end| end.as_ptr());
    *out = owner.model(|state| Observation {
        counter: state.base.refresh_counter,
        forced: u8::from(state.base.refresh_requests & REFRESH_MANUAL != 0),
        rendered: u8::from(state.base.rendered),
        remembered: u8::from(state.base.remembered),
        end_ready: u8::from(state.base.end_ready),
        end,
    });
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_exit(owner: *mut c_void) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.exit(&access);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_render(
    owner: *mut c_void,
    paint: unsafe extern "C" fn(*mut c_void),
    context: *mut c_void,
) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    u8::from(owner.render_with(|access| owner.render_base(access, || paint(context))))
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_enter(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).enter();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_loop(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).loop_base();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_remember(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).remember_book();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_clear_end(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).clear_end_options();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_flush(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).flush_session();
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_end_active(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).end_menu_active())
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_end_menu(owner: *mut c_void, suppressed: u8) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).handle_end_menu(suppressed != 0))
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_end_turn(
    owner: *mut c_void,
    previous: u8,
    next: u8,
) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).handle_end_turn(previous != 0, next != 0))
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_base_force(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).force_refresh())
}
