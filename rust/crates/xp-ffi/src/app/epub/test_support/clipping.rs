extern crate alloc;
use super::super::{abi, app_abi, state::Position, EpubSession};
use super::ClippingHooks;
use alloc::boxed::Box;
use core::{ffi::c_void, ptr::NonNull};

#[repr(C)]
pub struct Seed {
    pub native: *mut c_void,
    pub section: *mut c_void,
    pub spine: i32,
    pub page: i32,
    pub width: u16,
    pub height: u16,
    pub historical: u8,
    pub context: *mut c_void,
    pub signature: unsafe extern "C" fn(*mut c_void, u16, u16) -> u32,
    pub nonempty: unsafe extern "C" fn(*mut c_void, u8),
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_create(
    seed: *const Seed,
    leaves: *const abi::XpEpubLeaves,
    runtime: *const app_abi::Runtime,
    resources: *const app_abi::Resources,
) -> *mut c_void {
    let seed = &*seed;
    let Some(mut owner) = EpubSession::try_new(b"", false, 3, &*leaves, &*runtime, &*resources)
    else {
        return core::ptr::null_mut();
    };
    owner.state.get_mut().ready = true;
    owner.state.get_mut().current = Position {
        spine: seed.spine,
        page: seed.page,
    };
    owner.state.get_mut().viewport = (seed.width, seed.height);
    owner.guarded.get_mut().native = Some(NonNull::new(seed.native).expect("fixture root"));
    owner.guarded.get_mut().section = NonNull::new(seed.section);
    if seed.historical != 0 {
        owner.clipping_hooks = Some(ClippingHooks {
            context: seed.context,
            signature: seed.signature,
            nonempty: seed.nonempty,
        });
    }
    {
        let access = owner.borrowed_resources();
        owner.publish_section(&access);
    }
    Box::into_raw(owner).cast()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_destroy(owner: *mut c_void) {
    let mut owner = Box::from_raw(owner.cast::<EpubSession>());
    {
        let access = owner.borrowed_resources();
        owner.drop_section(&access);
    }
    owner.guarded.get_mut().native = None;
    drop(owner);
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_hit(
    owner: *mut c_void,
    page: *mut c_void,
    x: i32,
    y: i32,
) -> i32 {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.clipping_at_point(&access, NonNull::new(page).expect("fixture page"), x, y)
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_draw(
    owner: *mut c_void,
    page: *mut c_void,
    font: i32,
    top: i32,
    left: i32,
) {
    let owner = &*owner.cast::<EpubSession>();
    let access = owner.borrowed_resources();
    owner.draw_clipping_highlights(
        &access,
        NonNull::new(page).expect("fixture page"),
        font,
        top,
        left,
    );
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_signature(owner: *mut c_void) -> u32 {
    (&*owner.cast::<EpubSession>()).clipping_layout_signature()
}
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_clipping_nonempty(owner: *mut c_void) -> u8 {
    u8::from((&*owner.cast::<EpubSession>()).has_clippings())
}
