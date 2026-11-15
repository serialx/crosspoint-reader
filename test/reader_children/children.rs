// The reader children at their production depth: `super::` is this module and
// `super::super::` is `epub` (production: `app::epub`). Keep both levels; the
// production files use both forms.

#[path = "../../rust/crates/xp-ffi/src/app/epub/children/bookmark_owned.rs"]
pub(crate) mod bookmark_owned;
#[cfg(test)]
#[path = "bookmark_tests.rs"]
pub(crate) mod bookmark_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/bookmark_view.rs"]
pub(crate) mod bookmark_view;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/bookmark_window.rs"]
pub(crate) mod bookmark_window;
#[cfg(test)]
#[path = "chapter_scene_tests.rs"]
pub(crate) mod chapter_scene_tests;
#[cfg(test)]
#[path = "chapter_tests.rs"]
pub(crate) mod chapter_tests;
#[path = "chapter_trace.rs"]
pub mod chapter_trace;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/chapter_view.rs"]
pub(crate) mod chapter_view;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/chapters.rs"]
pub(crate) mod chapters;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/results.rs"]
pub(crate) mod child_result;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/clipping_view.rs"]
pub(crate) mod clipping_view;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/clipping_window.rs"]
pub(crate) mod clipping_window;
#[cfg(test)]
#[path = "clipping_window_tests.rs"]
pub(crate) mod clipping_window_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/commands.rs"]
pub(crate) mod commands;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/definition_physical.rs"]
pub(crate) mod definition_physical;
#[cfg(test)]
#[path = "definition_tests.rs"]
pub(crate) mod definition_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/input.rs"]
pub(crate) mod input;
#[cfg(test)]
#[path = "input_tests.rs"]
pub(crate) mod input_tests;
#[cfg(test)]
#[path = "lifecycle_tests.rs"]
pub(crate) mod lifecycle_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/list.rs"]
pub(crate) mod list;
#[cfg(test)]
#[path = "list_adapter_tests.rs"]
pub(crate) mod list_adapter_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/menu.rs"]
pub(crate) mod menu;
#[cfg(test)]
#[path = "menu_scene_tests.rs"]
pub(crate) mod menu_scene_tests;
#[cfg(test)]
#[path = "menu_tests.rs"]
pub(crate) mod menu_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/menu_view.rs"]
pub(crate) mod menu_view;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/owned.rs"]
pub(crate) mod owned;
#[cfg(test)]
#[path = "owner_tests.rs"]
pub(crate) mod owner_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/percent.rs"]
pub(crate) mod percent_adapter;
#[cfg(test)]
#[path = "percent_adapter_tests.rs"]
pub(crate) mod percent_adapter_tests;
#[cfg(test)]
#[path = "percent_scene_tests.rs"]
pub(crate) mod percent_scene_tests;
#[cfg(test)]
#[path = "percent_tests.rs"]
pub(crate) mod percent_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/physical.rs"]
pub(crate) mod physical;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/qr.rs"]
pub(crate) mod qr;
#[cfg(test)]
#[path = "qr_adapter_tests.rs"]
pub(crate) mod qr_adapter_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/selection_owned.rs"]
pub(crate) mod selection_owned;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/selection_physical.rs"]
pub(crate) mod selection_physical;
#[cfg(test)]
#[path = "tests.rs"]
pub(crate) mod tests;
#[cfg(test)]
#[path = "toolbar_adapter_tests.rs"]
pub(crate) mod toolbar_adapter_tests;
#[cfg(test)]
#[path = "toolbar_scene_tests.rs"]
pub(crate) mod toolbar_scene_tests;
#[path = "../../rust/crates/xp-ffi/src/app/epub/children/toolbar_view.rs"]
pub(crate) mod toolbar_view;
