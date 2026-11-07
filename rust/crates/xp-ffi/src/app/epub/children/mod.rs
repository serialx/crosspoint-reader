mod activity;
#[cfg(feature = "app-test")]
mod bookmark_host;
#[allow(dead_code)]
mod bookmark_owned;
#[allow(dead_code)]
mod bookmark_view;
#[allow(dead_code)]
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
mod footnote;
mod lookup;
mod lookup_view;
mod selection_owned;
pub(in crate::app::epub) mod selection_physical;
mod selection_scene;

#[cfg(all(test, feature = "app-test"))]
mod selection_tests;
