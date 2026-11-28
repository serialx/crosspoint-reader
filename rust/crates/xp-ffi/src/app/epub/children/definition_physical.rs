use super::selection_physical::{DefinitionFill as Fill, LookupStorage};
use crate::app::abi::{Text, TextSink};
use core::ffi::c_void;
#[repr(C)]
pub struct PayloadStorage {
    pub words: [usize; 43],
}
#[repr(C)]
pub struct NativeTransfer {
    _opaque: [u8; 0],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PayloadView {
    pub pages: i32,
    pub styled: u8,
    pub reserved: [u8; 3],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct SelectionPosition {
    pub start_page: u16,
    pub end_page: u16,
    pub start_word: u16,
    pub end_word: u16,
    pub valid: u8,
    pub reserved: [u8; 3],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct SelectionRequest {
    pub page: i32,
    pub selection: SelectionPosition,
    pub x: i32,
    pub y: i32,
}
#[repr(C)]
pub struct DefinitionPhysical {
    pub payload_lookup: unsafe extern "C" fn(
        *mut PayloadStorage,
        *mut LookupStorage,
        TextSink,
        *mut c_void,
        *mut u8,
    ) -> u8,
    pub payload_plain:
        unsafe extern "C" fn(*mut PayloadStorage, Text, u32, Fill, *mut c_void) -> u8,
    pub payload_native:
        unsafe extern "C" fn(*mut PayloadStorage, *mut NativeTransfer, *mut u8) -> u8,
    pub payload_drop: unsafe extern "C" fn(*mut PayloadStorage),
    pub heap: unsafe extern "C" fn(*mut crate::app::epub::abi::XpEpubHeap),
    pub payload_prepare: unsafe extern "C" fn(
        *mut PayloadStorage,
        u8,
        u8,
        u8,
        xp_ui::scene::Rect,
        *mut PayloadView,
    ) -> u8,
    pub payload_release: unsafe extern "C" fn(*mut PayloadStorage, u8, u8),
    pub payload_find:
        unsafe extern "C" fn(*mut PayloadStorage, u8, *mut LookupStorage, Text, *mut u8) -> u8,
    pub payload_commit: unsafe extern "C" fn(*mut PayloadStorage),
    pub selection_build: unsafe extern "C" fn(*mut PayloadStorage, i32) -> u8,
    pub selection_child: unsafe extern "C" fn(
        *mut PayloadStorage,
        *const SelectionRequest,
        xp_ui::scene::Rect,
    ) -> *mut c_void,
    pub selection_release: unsafe extern "C" fn(*mut PayloadStorage),
    pub layout: unsafe extern "C" fn(*mut xp_ui::dictionary::definition::Layout, i32, i32) -> u8,
    pub draw_header: unsafe extern "C" fn(*const PayloadStorage, xp_ui::scene::Rect, i32, i32),
    pub draw_body: unsafe extern "C" fn(*const PayloadStorage, i32, i32, i32, i32),
}

const _: () = {
    assert!(core::mem::size_of::<PayloadStorage>() == 43 * core::mem::size_of::<usize>());
    assert!(core::mem::size_of::<PayloadView>() == 8);
    assert!(core::mem::size_of::<SelectionPosition>() == 12);
    assert!(core::mem::size_of::<SelectionRequest>() == 24);
    assert!(core::mem::size_of::<DefinitionPhysical>() == 15 * core::mem::size_of::<usize>());
};

#[cfg(test)]
impl DefinitionPhysical {
    pub(crate) const fn unused() -> Self {
        unsafe extern "C" fn lookup(
            _: *mut PayloadStorage,
            _: *mut LookupStorage,
            _: TextSink,
            _: *mut c_void,
            _: *mut u8,
        ) -> u8 {
            panic!("definition lookup")
        }
        unsafe extern "C" fn plain(
            _: *mut PayloadStorage,
            _: Text,
            _: u32,
            _: Fill,
            _: *mut c_void,
        ) -> u8 {
            panic!("definition plain")
        }
        unsafe extern "C" fn native(
            _: *mut PayloadStorage,
            _: *mut NativeTransfer,
            _: *mut u8,
        ) -> u8 {
            panic!("definition native")
        }
        unsafe extern "C" fn storage(_: *mut PayloadStorage) {
            panic!("definition storage")
        }
        unsafe extern "C" fn heap(_: *mut crate::app::epub::abi::XpEpubHeap) {
            panic!("definition heap")
        }
        unsafe extern "C" fn prepare(
            _: *mut PayloadStorage,
            _: u8,
            _: u8,
            _: u8,
            _: xp_ui::scene::Rect,
            _: *mut PayloadView,
        ) -> u8 {
            panic!("definition prepare")
        }
        unsafe extern "C" fn release(_: *mut PayloadStorage, _: u8, _: u8) {
            panic!("definition release")
        }
        unsafe extern "C" fn find(
            _: *mut PayloadStorage,
            _: u8,
            _: *mut LookupStorage,
            _: Text,
            _: *mut u8,
        ) -> u8 {
            panic!("definition find")
        }
        unsafe extern "C" fn build(_: *mut PayloadStorage, _: i32) -> u8 {
            panic!("definition selection build")
        }
        unsafe extern "C" fn child(
            _: *mut PayloadStorage,
            _: *const SelectionRequest,
            _: xp_ui::scene::Rect,
        ) -> *mut c_void {
            panic!("definition selection child")
        }
        unsafe extern "C" fn layout(
            _: *mut xp_ui::dictionary::definition::Layout,
            _: i32,
            _: i32,
        ) -> u8 {
            panic!("definition layout")
        }
        unsafe extern "C" fn header(
            _: *const PayloadStorage,
            _: xp_ui::scene::Rect,
            _: i32,
            _: i32,
        ) {
            panic!("definition header")
        }
        unsafe extern "C" fn body(_: *const PayloadStorage, _: i32, _: i32, _: i32, _: i32) {
            panic!("definition body")
        }
        Self {
            payload_lookup: lookup,
            payload_plain: plain,
            payload_native: native,
            payload_drop: storage,
            heap,
            payload_prepare: prepare,
            payload_release: release,
            payload_find: find,
            payload_commit: storage,
            selection_build: build,
            selection_child: child,
            selection_release: storage,
            layout,
            draw_header: header,
            draw_body: body,
        }
    }
}

impl DefinitionPhysical {
    pub(super) unsafe fn body(&self) -> xp_ui::scene::Rect {
        let mut layout = xp_ui::dictionary::definition::Layout::default();
        unsafe { (self.layout)(&mut layout, -1, -1) };
        xp_ui::dictionary::definition::body(&layout)
    }
}
#[cfg(feature = "app-test")]
#[no_mangle]
/// # Safety
/// The installed physical table and its callbacks remain live through this call.
pub unsafe extern "C" fn xp_app_test_definition_body(
    physical: &DefinitionPhysical,
) -> xp_ui::scene::Rect {
    unsafe { physical.body() }
}
