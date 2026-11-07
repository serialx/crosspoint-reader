mod activity;
mod bookmark_actions;
mod bookmark_activity;
#[cfg(feature = "app-test")]
mod bookmark_host;
pub(super) mod bookmark_owned;
mod bookmark_view;
mod bookmark_window;
mod chapter_book;
mod chapter_view;
mod chapters;
#[path = "results.rs"]
mod child_result;
mod clipping_actions;
mod clipping_activity;
mod clipping_view;
mod clipping_window;
mod commands;
#[cfg(feature = "app-test")]
mod host;
mod input;
mod launch;
mod list;
mod menu;
mod menu_actions;
mod menu_view;
mod owned;
mod parent;
mod percent;
mod physical;
mod qr;
mod toolbar_owner;
mod toolbar_view;

pub(in crate::app) use child_result::ReaderChildResult;
pub(in crate::app) use definition::Definition;

pub(in crate::app) use owned::{ChildTarget, ReaderChild};
pub(in crate::app) use physical::Physical;

pub(in crate::app::epub) use toolbar_owner::{ToolbarSlot, ToolbarTarget};

mod definition;
pub(in crate::app) mod definition_physical;
#[allow(dead_code)]
mod definition_owned;
#[allow(dead_code)]
mod definition_navigation;
#[allow(dead_code)]
mod definition_input;
#[allow(dead_code)]
mod definition_view;
#[allow(dead_code)]
mod definition_text;
mod footnote;
mod lookup;
mod lookup_view;
mod selection_owned;
pub(in crate::app::epub) mod selection_physical;
mod selection_scene;

#[cfg(all(test, feature = "app-test"))]
mod selection_tests;

#[cfg(test)]
mod definition_session_tests;

#[cfg(feature = "app-test")]
mod definition_test_bridge;
