use crate::app::abi;
use core::ffi::c_void;
use xp_ui::{
    model::Metrics,
    reader_chrome::{
        list,
        runtime::{ThemeFacts, ThemeMode},
    },
    scene::{Error, Rect},
    utility,
};

// Borrowed from the static EPUB leaf table.
// Every pointer is borrowed synchronously under the parent's resource guard.
#[repr(C)]
pub(crate) struct Physical {
    pub(super) list_theme: unsafe extern "C" fn(*mut list::Theme, *mut ThemeFacts, u8),
    pub(super) utility_layout: unsafe extern "C" fn(*mut utility::Layout) -> u8,
    pub(super) header: unsafe extern "C" fn(Rect, abi::Text, u8),
    pub(super) utility_footer: unsafe extern "C" fn(Rect, *const abi::Text),
    pub(super) qr: unsafe extern "C" fn(Rect, abi::Text),
    pub(super) toolbar_icon: unsafe extern "C" fn(u32, Rect, Rect),
    #[cfg(feature = "app-test")]
    pub(super) clear_fonts: unsafe extern "C" fn(),
    pub(super) prewarm_rows: unsafe extern "C" fn(
        i32,
        *const c_void,
        unsafe extern "C" fn(*const c_void, u32) -> abi::Text,
        u32,
    ),
}

impl Physical {
    pub(super) unsafe fn clear_font_cache(&self, _owner: abi::FontOwner) {
        #[cfg(feature = "app-test")]
        (self.clear_fonts)();
        #[cfg(not(feature = "app-test"))]
        crate::font_runtime::release(_owner.get().cast(), 0);
    }
    // Keep theme facts off shared child dispatch and session render frames.
    #[inline(never)]
    pub(super) unsafe fn sample_theme(
        &self,
        theme: &mut list::Theme,
        mode: ThemeMode,
    ) -> Result<(), Error> {
        let mut facts = ThemeFacts::default();
        unsafe { (self.list_theme)(theme, &mut facts, u8::from(mode == ThemeMode::Toolbar)) };
        facts.compose(theme, mode)
    }
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

const _: () = {
    let font_fields = if cfg!(feature = "app-test") { 1 } else { 0 };
    let pointer = core::mem::size_of::<unsafe extern "C" fn()>();
    assert!(core::mem::size_of::<Physical>() == (7 + font_fields) * pointer);
    assert!(core::mem::align_of::<Physical>() == core::mem::align_of::<unsafe extern "C" fn()>());
    assert!(core::mem::offset_of!(Physical, list_theme) == 0);
    assert!(core::mem::offset_of!(Physical, utility_layout) == pointer);
    assert!(core::mem::offset_of!(Physical, header) == 2 * pointer);
    assert!(core::mem::offset_of!(Physical, utility_footer) == 3 * pointer);
    assert!(core::mem::offset_of!(Physical, qr) == 4 * pointer);
    assert!(core::mem::offset_of!(Physical, toolbar_icon) == 5 * pointer);
    #[cfg(feature = "app-test")]
    assert!(core::mem::offset_of!(Physical, clear_fonts) == 6 * pointer);
    assert!(core::mem::offset_of!(Physical, prewarm_rows) == (6 + font_fields) * pointer);
};
const _: fn(&Physical) = |p| {
    let _: unsafe extern "C" fn(*mut list::Theme, *mut ThemeFacts, u8) = p.list_theme;
    let _: unsafe extern "C" fn(*mut utility::Layout) -> u8 = p.utility_layout;
    let _: unsafe extern "C" fn(Rect, abi::Text, u8) = p.header;
    let _: unsafe extern "C" fn(Rect, *const abi::Text) = p.utility_footer;
    let _: unsafe extern "C" fn(Rect, abi::Text) = p.qr;
    let _: unsafe extern "C" fn(u32, Rect, Rect) = p.toolbar_icon;
    #[cfg(feature = "app-test")]
    let _: unsafe extern "C" fn() = p.clear_fonts;
    let _: unsafe extern "C" fn(
        i32,
        *const c_void,
        unsafe extern "C" fn(*const c_void, u32) -> abi::Text,
        u32,
    ) = p.prewarm_rows;
};
