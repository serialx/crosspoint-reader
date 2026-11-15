// Production has one owner type; only named historical fixtures substitute a handle.
#[cfg(not(feature = "app-test"))]
pub(super) use super::section_owner::{ReaderSection as Section, SectionSlot};
#[cfg(not(feature = "app-test"))]
pub(super) type Handle = core::ptr::NonNull<Section>;
#[cfg(feature = "app-test")]
pub(super) use super::test_support::legacy_section::{Handle, Section, SectionSlot};
