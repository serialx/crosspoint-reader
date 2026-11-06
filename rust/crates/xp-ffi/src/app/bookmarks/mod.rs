extern crate alloc;

use super::resources::Text;
mod decode;
mod encode;
pub mod export;
#[cfg(feature = "app-test")]
mod export_host;
#[cfg(feature = "app-test")]
pub mod host;
mod json;
pub mod mutations;
mod number;
mod parse_number;
pub mod persistence;
pub use decode::{decode, Name};
pub use encode::encode;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Allocation,
    NameTooLong,
    Size,
    InvalidInput,
    IncompleteInput,
    EmptyInput,
    TooDeep,
}

pub struct Record<N> {
    pub xpath: Text,
    pub summary: Text,
    pub name: N,
    pub percentage: f32,
    pub spine: u16,
    pub page_count: u16,
    pub page: u16,
    pub visible_offset: Option<u32>,
}
