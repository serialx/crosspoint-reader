#![no_std]

//! Bounded scenes. Owners lend storage; a failed build never becomes paintable.
pub mod draw;
pub mod scene;

#[cfg(test)]
mod tests;

pub mod model;

pub mod ffi;

pub mod list;

pub mod xtc;
