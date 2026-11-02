use super::super::{state::Overlay, EpubSession};
use core::ffi::c_void;

#[repr(C)]
pub struct SceneSetup {
    pub overlay: u8,
    pub stored: u8,
    pub hold_jumped: u8,
    pub cursor: u8,
    pub focused_tool: i32,
    pub panel_index: i32,
}

// The App owns this session; the test has installed a real toolbar and the
// physical backup corresponding to stored. This does not publish/reject a scene.
#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_scene_setup(raw: *mut c_void, setup: *const SceneSetup) {
    let owner = &*raw.cast::<EpubSession>();
    let setup = &*setup;
    let overlay = match setup.overlay {
        0 => Overlay::Page,
        1 => Overlay::Toolbar,
        2 => Overlay::Contents,
        3 => Overlay::Text,
        4 => Overlay::More,
        _ => panic!("fixture overlay"),
    };
    let access = owner.lock_resources();
    assert!(matches!(overlay, Overlay::Page) || (*owner.guarded(&access)).toolbar.is_some());
    owner.model(|state| {
        state.overlay = overlay;
        state.page_stored = setup.stored != 0;
        state.panel_hold_jumped = setup.hold_jumped != 0;
        state.panel_cursor = setup.cursor != 0;
        state.focused_tool = setup.focused_tool;
        state.panel_index = setup.panel_index;
    });
}
