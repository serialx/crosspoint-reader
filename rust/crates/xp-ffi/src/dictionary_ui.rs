//! Thin private ABI wrappers; dictionary screen policy and drawing live in xp-ui.
use core::ffi::c_char;
use xp_core::glyph::GlyphTarget;
use xp_ui::{
    dictionary::{definition, ffi as screen, paint, popup, selection},
    ffi::{Buffer, Metrics, SceneView},
    model::{Effect, Input},
    scene::Rect,
};
macro_rules! forward {
    ($name:ident($($arg:ident:$ty:ty),* $(,)?) -> $ret:ty, $target:ident) => {
        /// # Safety
        /// Callers follow the synchronous, disjoint borrowing contract in xp_ui_dictionary.h.
        #[no_mangle]
        #[inline(never)]
        pub unsafe extern "C" fn $name($($arg:$ty),*) -> $ret { unsafe {screen::$target($($arg),*)} }
    };
}
forward!(
    xp_ui_selection_init(
        state: *mut selection::State,
        mode: u8,
        words: *const selection::Word,
        count: u32,
        width: i32,
        first: i32,
        last: i32,
    ) -> (),
    selection_init
);
forward!(
    xp_ui_selection_update(
        state: *mut selection::State,
        words: *const selection::Word,
        count: u32,
        layout: *const selection::Layout,
        input: *const selection::Input,
    ) -> selection::Update,
    selection_update
);
forward!(
    xp_ui_selection_poll(state: *mut selection::State, input: *const Input) -> u8,
    selection_poll
);
forward!(
    xp_ui_selection_dequeue(state: *mut selection::State) -> u8,
    selection_dequeue
);
forward!(
    xp_ui_selection_buttons(
        state: *mut selection::State,
        words: *const selection::Word,
        count: u32,
        buttons: u8,
    ) -> selection::Update,
    selection_buttons
);
forward!(
    xp_ui_selection_view(
        state: *const selection::State,
        words: *const selection::Word,
        count: u32,
        layout: *const selection::Layout,
        old: *const selection::Painted,
        buffer: *const Buffer,
        next: *mut selection::Painted,
        out: *mut SceneView,
    ) -> u8,
    selection_view
);
forward!(
    xp_ui_selection_paint(
        state: *const selection::State,
        words: *const selection::Word,
        count: u32,
        layout: *const selection::Layout,
        old: *const selection::Painted,
        target: *const GlyphTarget,
        fonts: *const paint::Fonts,
        mode: u8,
    ) -> u8,
    selection_paint
);
forward!(
    xp_ui_definition_update(
        state: *mut definition::State,
        input: *const Input,
        tap: u8,
        long: u8,
        header: u8,
        width: i32,
        touch: u8,
    ) -> Effect,
    definition_update
);
forward!(
    xp_ui_definition_body(layout: *const definition::Layout) -> Rect,
    definition_body
);
forward!(
    xp_ui_definition_view(
        state: *const definition::State,
        layout: *const definition::Layout,
        buffer: *const Buffer,
        out: *mut SceneView,
    ) -> u8,
    definition_view
);
forward!(
    xp_ui_lookup_init(state: *mut popup::Lookup) -> (),
    lookup_init
);
forward!(
    xp_ui_lookup_view(
        state: *const popup::Lookup,
        rect: Rect,
        buffer: *const Buffer,
        out: *mut SceneView,
    ) -> u8,
    lookup_view
);
forward!(
    xp_ui_lookup_result(
        state: *mut popup::Lookup,
        opened: u8,
        index: u8,
        result: u8,
        found: u8,
        now: u32,
    ) -> (),
    lookup_result
);
forward!(
    xp_ui_lookup_expired(state: *const popup::Lookup, now: u32) -> u8,
    lookup_expired
);
forward!(
    xp_ui_dictionary_popup(
        layout: *const popup::Layout,
        message: *const c_char,
        metrics: *const Metrics,
        buffer: *const Buffer,
        out: *mut SceneView,
    ) -> u8,
    dictionary_popup
);
