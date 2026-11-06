mod activity;
mod chapter_book;
mod chapter_view;
mod chapters;
#[path = "results.rs"]
mod child_result;
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

pub(in crate::app) use owned::{ChildTarget, ReaderChild};
pub(in crate::app) use physical::Physical;

pub(in crate::app::epub) use toolbar_owner::{ToolbarSlot, ToolbarTarget};

// Dormant ownership entry until the O4b switch checkpoint.
#[allow(dead_code)]
mod definition;
#[allow(dead_code)]
mod footnote;
#[allow(dead_code)]
mod lookup;
#[allow(dead_code)]
mod lookup_view;
#[allow(dead_code)]
mod selection_owned;
#[allow(dead_code)]
pub(in crate::app::epub) mod selection_physical;
#[allow(dead_code)]
mod selection_scene;

#[cfg(all(test, feature = "app-test"))]
mod selection_tests;
