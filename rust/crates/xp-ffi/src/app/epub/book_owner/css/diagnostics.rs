use super::{super::log::message, Context, XpEpubLeaves};
use crate::book_css::Event;
use core::ffi::c_void;
use xp_core::css::{Diagnostic, CACHE_BUFFER_BYTES, CACHE_VERSION, READ_BUFFER_BYTES};

pub(super) unsafe extern "C" fn transaction(raw: *mut c_void, event: u32, argument: u32) {
    let context = unsafe { &*raw.cast::<Context<'_>>() };
    let leaves = context.leaves;
    macro_rules! emit {
        ($severity:expr, $($argument:tt)*) => { message(leaves, $severity, b"CSS", format_args!($($argument)*)) };
    }
    match event {
        x if x == Event::RestoreFailed as u32 => emit!(1, "Failed to restore CSS cache backup"),
        x if x == Event::Restored as u32 => {
            emit!(0, "Restored CSS cache backup after interrupted replacement")
        }
        x if x == Event::OutputOom as u32 => {
            emit!(1, "OOM: cache output ({} bytes)", CACHE_BUFFER_BYTES)
        }
        x if x == Event::OrderOom as u32 => emit!(1, "OOM sorting CSS cache rules"),
        x if x == Event::OrderFailed as u32 => emit!(1, "Could not prepare cache source order"),
        x if x == Event::WriteFailed as u32 => emit!(1, "Failed to write temporary CSS cache"),
        x if x == Event::BackupFailed as u32 => emit!(1, "Failed to back up existing CSS cache"),
        x if x == Event::PromoteFailed as u32 => emit!(1, "Failed to promote temporary CSS cache"),
        x if x == Event::RollbackFailed as u32 => emit!(1, "Failed to restore previous CSS cache"),
        x if x == Event::SavedComplete as u32 => {
            emit!(0, "Saved {} rules to complete cache", argument)
        }
        x if x == Event::SavedPartial as u32 => {
            emit!(0, "Saved {} rules to partial cache", argument)
        }
        x if x == Event::Version as u32 => emit!(
            0,
            "Cache version mismatch (got {}, expected {}), removing stale cache for rebuild",
            argument,
            CACHE_VERSION
        ),
        x if x == Event::InputOom as u32 => {
            emit!(1, "OOM: cache input ({} bytes)", READ_BUFFER_BYTES)
        }
        x if x == Event::LoadedComplete as u32 => {
            emit!(0, "Loaded {} rules from complete cache", argument)
        }
        x if x == Event::LoadedPartial as u32 => {
            emit!(0, "Loaded {} rules from partial cache", argument)
        }
        x if x == Event::InvalidSource as u32 => emit!(1, "Cannot read from invalid file"),
        x if x == Event::SourceOom as u32 => {
            emit!(1, "OOM: CSS input ({} bytes)", READ_BUFFER_BYTES)
        }
        x if x == Event::Parsed as u32 => {
            let handle = unsafe { *context.handle };
            let count = if handle.is_null() {
                0
            } else {
                unsafe { (*handle).store.rule_count() }
            };
            emit!(0, "Parsed {} rules from {} bytes", count, argument);
        }
        x if x == Event::HandleOom as u32 => emit!(1, "OOM: CSS handle"),
        _ => (),
    }
}

pub(super) extern "C" fn parser(raw: *mut c_void, event: u32, a: u32, b: u32, _: u32, _: u32) {
    let leaves = unsafe { &*raw.cast::<XpEpubLeaves>() };
    macro_rules! emit {
        ($severity:expr, $($argument:tt)*) => { message(leaves, $severity, b"CSS", format_args!($($argument)*)) };
    }
    match event {
        x if x == Diagnostic::IndexOom as u32 => emit!(1, "OOM: selector index ({} entries)", a),
        x if x == Diagnostic::SelectorOom as u32 => emit!(1, "OOM: selector pool ({} bytes)", a),
        x if x == Diagnostic::StyleOom as u32 => emit!(1, "OOM: style pool ({} styles)", a),
        x if x == Diagnostic::SelectorLong as u32 => {
            emit!(0, "Selector too long ({} > {}), skipping", a, b)
        }
        x if x == Diagnostic::StoreLimit as u32 => {
            emit!(1, "CSS rule store limit reached at {} rules", a)
        }
        x if x == Diagnostic::StoreOom as u32 => {
            emit!(1, "OOM while growing CSS rule store at {} rules", a)
        }
        x if x == Diagnostic::InputTruncated as u32 => emit!(
            1,
            "CSS input exceeded parser buffer; cache will remain partial"
        ),
        x if x == Diagnostic::CacheFlags as u32 => emit!(0, "Invalid CSS cache flags: {}", a),
        x if x == Diagnostic::CacheCount as u32 => {
            emit!(0, "Invalid cache rule count ({} > {})", a, b)
        }
        x if x == Diagnostic::CacheSelectorLength as u32 => {
            emit!(0, "Invalid selector length in cache: {}", a)
        }
        x if x == Diagnostic::CacheDuplicate as u32 => emit!(0, "Duplicate selector in CSS cache"),
        x if x == Diagnostic::ScratchOom as u32 => emit!(1, "OOM: CSS parser scratch"),
        // Native long is signed 32-bit on both target architectures.
        x if x == Diagnostic::CacheScratchOom as u32 => {
            emit!(1, "Cache load failed ({})", a as i32)
        }
        _ => (),
    }
}
