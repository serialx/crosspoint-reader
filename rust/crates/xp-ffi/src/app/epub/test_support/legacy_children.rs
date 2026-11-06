//! Historical native child/Toolbar callbacks for named host oracle fixtures only.
use super::super::{abi::*, EpubSession};
use crate::app::abi::Text as XpAppText;
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub(in crate::app) struct LegacyChildren {
    pub child_menu: unsafe extern "C" fn(args: *const XpEpubMenuArgs) -> *mut core::ffi::c_void,
    pub child_chapter:
        unsafe extern "C" fn(book: *mut core::ffi::c_void, toc: i32) -> *mut core::ffi::c_void,
    pub child_percent: unsafe extern "C" fn(percent: i32) -> *mut core::ffi::c_void,
    pub child_qr: unsafe extern "C" fn(text: XpAppText) -> *mut core::ffi::c_void,
    pub toolbar_open: unsafe extern "C" fn() -> *mut core::ffi::c_void,
    pub toolbar_close: unsafe extern "C" fn(toolbar: *mut core::ffi::c_void),
    pub toolbar_begin: unsafe extern "C" fn(toolbar: *mut core::ffi::c_void),
    pub toolbar_paint: unsafe extern "C" fn(
        toolbar: *mut core::ffi::c_void,
        model: *const XpEpubToolbarModel,
        rows: XpEpubRowSource,
        context: *mut core::ffi::c_void,
    ) -> u8,
    pub toolbar_route:
        unsafe extern "C" fn(toolbar: *mut core::ffi::c_void, out: *mut XpEpubRouted),
    pub toolbar_scroll:
        unsafe extern "C" fn(toolbar: *mut core::ffi::c_void, delta: i32, count: i32) -> u8,
    pub toolbar_visible_rows: unsafe extern "C" fn(toolbar: *mut core::ffi::c_void) -> i32,
    pub toolbar_reset_navigation:
        unsafe extern "C" fn(toolbar: *mut core::ffi::c_void, selected: i32, top: i32),
    pub child_clippings: Option<unsafe extern "C" fn() -> *mut c_void>,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_legacy_children(
    raw: *mut c_void,
    leaves: *const LegacyChildren,
) {
    let owner = unsafe { &*raw.cast::<EpubSession>() };
    owner
        .legacy_children
        .set(unsafe { leaves.as_ref() }.copied());
}

const _: () = {
    let pointer = core::mem::size_of::<unsafe extern "C" fn()>();
    assert!(core::mem::size_of::<LegacyChildren>() == 13 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, child_clippings) == 12 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, child_menu) == 0);
    assert!(core::mem::offset_of!(LegacyChildren, child_chapter) == pointer);
    assert!(core::mem::offset_of!(LegacyChildren, child_percent) == 2 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, child_qr) == 3 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_open) == 4 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_close) == 5 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_begin) == 6 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_paint) == 7 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_route) == 8 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_scroll) == 9 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_visible_rows) == 10 * pointer);
    assert!(core::mem::offset_of!(LegacyChildren, toolbar_reset_navigation) == 11 * pointer);
};
