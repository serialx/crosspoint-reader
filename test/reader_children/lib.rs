#![allow(dead_code)]

mod app;
pub use app::epub::children::chapter_trace;
// The fixture's own test modules keep the flat `crate::` paths they use for the
// child modules mounted at their production depth under `app::epub::children`.
#[cfg(test)]
use app::epub::children::*;
#[cfg(test)]
use app::*;

#[cfg(test)]
use xp_ui::reader::percent;
