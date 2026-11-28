//! KOReader activity policy and bounded views; I/O and book resources are borrowed.
pub mod abi;
mod run;
mod state;
mod view;
pub use run::{Operation, Options, Services};
pub use state::{Message, Phase, State};
pub use view::Layout;

#[cfg(test)]
mod tests;
