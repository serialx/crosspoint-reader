//! CSS cache and resident-store limits shared by the codec and book I/O.
pub const READ_BUFFER_BYTES: usize = 512;
pub const CACHE_VERSION: u8 = 19;
pub const WIRE_BYTES: usize = 88;
pub const MAX_RULES: usize = 1500;
pub const SELECTOR_CAP: usize = 32768;
pub const MAX_SELECTOR_BYTES: usize = 256;
// Canonicalization can add one byte (for example, :first-letter to ::first-letter).
pub const MAX_CACHE_KEY_BYTES: usize = MAX_SELECTOR_BYTES + 1;
pub const CACHE_BUFFER_BYTES: usize = 2 + MAX_CACHE_KEY_BYTES + WIRE_BYTES;
