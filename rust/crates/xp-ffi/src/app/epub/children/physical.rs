use crate::app::abi;
use core::ffi::c_void;
use xp_ui::{model::Metrics, reader_chrome::list, scene::Rect, utility};

// Dormant physical leaves, installed only when the native owners are retired.
// Every pointer is borrowed synchronously under the parent's resource guard.
#[repr(C)]
pub(crate) struct Physical {
    pub(super) list_theme: unsafe extern "C" fn(*mut list::Theme, u8),
    pub(super) utility_layout: unsafe extern "C" fn(*mut utility::Layout) -> u8,
    pub(super) header: unsafe extern "C" fn(Rect, abi::Text, u8),
    pub(super) utility_footer: unsafe extern "C" fn(Rect, *const abi::Text),
    pub(super) qr: unsafe extern "C" fn(Rect, abi::Text),
    pub(super) toolbar_icon: unsafe extern "C" fn(u32, Rect, Rect),
    pub(super) clear_fonts: unsafe extern "C" fn(),
    pub(super) prewarm_rows: unsafe extern "C" fn(
        i32,
        *const c_void,
        unsafe extern "C" fn(*const c_void, u32) -> abi::Text,
        u32,
    ),
}

pub(super) struct Fonts<'a>(pub(super) &'a abi::Resources);
impl Metrics for Fonts<'_> {
    fn width(&mut self, _: i32, _: &[u8], _: u8) -> i32 {
        // Utility views measure terminated resource labels through Texts.
        -1
    }
    fn line_height(&mut self, font: i32) -> i32 {
        unsafe { (self.0.line_height)(font) }
    }
}
