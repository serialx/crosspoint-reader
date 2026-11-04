extern crate alloc;

use super::super::{
    abi, app_abi,
    state::{BasePublication, Position, SectionFacts, SessionAccounting},
    EpubSession, ResourceAccess,
};
use alloc::boxed::Box;
use core::ffi::c_void;
use xp_ui::reader::Session;

#[repr(C)]
pub struct PublicationSeed {
    pub book: *mut c_void,
    pub session: Session,
    pub refresh_counter: i32,
    pub ready: u8,
    pub remembered: u8,
}

#[repr(C)]
pub struct PublicationObservation {
    pub session: Session,
    pub refresh_counter: i32,
    pub refresh_requests: u8,
    pub end_ready: u8,
    pub rendered: u8,
    pub remembered: u8,
}

fn observation(base: &BasePublication) -> PublicationObservation {
    PublicationObservation {
        session: base.session.into(),
        refresh_counter: base.refresh_counter,
        refresh_requests: base.refresh_requests,
        end_ready: u8::from(base.end_ready),
        rendered: u8::from(base.rendered),
        remembered: u8::from(base.remembered),
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_create_legacy_book(
    seed: *const PublicationSeed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
    legacy_book: &'static super::legacy_book::LegacyBookServices,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 15, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.book_factory = super::super::session_book::Factory::Legacy(legacy_book);
    owner.legacy_pages.set(Some(&legacy_book.section.page));
    let state = owner.state.get_mut();
    state.ready = seed.ready != 0;
    state.base.session = SessionAccounting::from(&seed.session);
    state.base.refresh_counter = seed.refresh_counter;
    state.base.remembered = seed.remembered != 0;
    state.current = Position { spine: 0, page: 1 };
    state.facts.book = !seed.book.is_null();
    state.facts.section = Some(SectionFacts {
        pages: 100,
        estimated_pages: 100,
        ..SectionFacts::default()
    });
    super::super::session_book::publish_legacy_fixture(owner, seed.book, legacy_book)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    assert!(owner.guarded.get_mut().active_frame.is_none());
    // The book is borrowed from the host physical fixture.
    owner.discard_legacy_fixture();
    drop(owner);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_observe(
    owner: *mut c_void,
    out: *mut PublicationObservation,
) {
    let owner = &*owner.cast::<EpubSession>();
    *out = owner.model(|state| observation(&state.base));
    // Exercise the production snapshot method separately from other scalar reads.
    (*out).session = owner.snapshot_session().into();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_ready(owner: *mut c_void, ready: u8) {
    (&*owner.cast::<EpubSession>()).model(|state| state.ready = ready != 0);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_remember(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).model(|state| state.base.remembered = true);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_note(
    owner: *mut c_void,
    forward: u8,
    succeeded: u8,
) {
    (&*owner.cast::<EpubSession>()).note_page_turn(forward != 0, succeeded != 0);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_force(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).queue_forced_refresh();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_disable_fast(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).disable_fast_initial_refresh();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_counter(owner: *mut c_void, value: i32) {
    (&*owner.cast::<EpubSession>()).set_refresh_counter(value);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_accepts(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).accepts_remote_input())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_reset(owner: *mut c_void) {
    (&*owner.cast::<EpubSession>()).reset_session();
}

// Neither the owner nor frame is mutably borrowed across the C paint callback.
struct PaintAccess<'paint, 'resource> {
    owner: &'paint EpubSession,
    access: &'paint ResourceAccess<'resource>,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_render(
    owner: *mut c_void,
    paint: unsafe extern "C" fn(*mut c_void, *mut c_void),
    context: *mut c_void,
) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    u8::from(owner.render_with(|access| {
        let callback = PaintAccess { owner, access };
        paint(context, core::ptr::from_ref(&callback).cast_mut().cast());
    }))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_present(owner: *mut c_void) -> u8 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    u8::from(owner.active_frame(&access).is_some())
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_observe(
    callback: *mut c_void,
    out: *mut PublicationObservation,
) {
    let callback = &*callback.cast::<PaintAccess<'_, '_>>();
    let frame = callback
        .owner
        .active_frame(callback.access)
        .expect("fixture frame");
    *out = observation(&frame.as_ref().base);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_counter(callback: *mut c_void) -> i32 {
    let callback = &*callback.cast::<PaintAccess<'_, '_>>();
    callback.owner.render_refresh_counter(callback.access)
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_set_counter(
    callback: *mut c_void,
    value: i32,
) {
    let callback = &*callback.cast::<PaintAccess<'_, '_>>();
    callback
        .owner
        .set_render_refresh_counter(callback.access, value);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_take_force(callback: *mut c_void) -> u8 {
    let callback = &*callback.cast::<PaintAccess<'_, '_>>();
    u8::from(callback.owner.take_forced_refresh(callback.access))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_mark(callback: *mut c_void) {
    let callback = &*callback.cast::<PaintAccess<'_, '_>>();
    callback.owner.mark_page_rendered(callback.access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_publication_frame_complete(callback: *mut c_void) {
    let callback = &*callback.cast::<PaintAccess<'_, '_>>();
    callback.owner.complete_session(callback.access);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_accounting_note(
    state: *mut Session,
    forward: u8,
    succeeded: u8,
) {
    let mut value = SessionAccounting::from(&*state);
    value.note(forward != 0, succeeded != 0);
    *state = value.into();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_accounting_render(
    state: *mut Session,
    now: u32,
    epoch: i64,
    progress: i32,
) {
    let mut value = SessionAccounting::from(&*state);
    value.render(now, epoch, progress);
    *state = value.into();
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_accounting_worthy(state: *const Session) -> u8 {
    u8::from(SessionAccounting::from(&*state).worthy())
}
