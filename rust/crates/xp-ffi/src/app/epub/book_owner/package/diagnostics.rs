use super::{
    super::{log, XpEpubLeaves},
    Kind,
};
use core::{ffi::c_void, slice};
use xp_core::epub::diagnostic::{Diagnostics, Error, Event};

pub(in super::super) fn services(leaves: &XpEpubLeaves) -> Diagnostics {
    Diagnostics {
        context: (leaves as *const XpEpubLeaves).cast_mut().cast(),
        diagnostic: Some(event),
    }
}

unsafe extern "C" fn event(
    raw: *mut c_void,
    event: u32,
    a: u32,
    _: u32,
    _: u32,
    _: u32,
    text: *const u8,
    size: u32,
    other: *const u8,
    other_size: u32,
) {
    let leaves = unsafe { &*raw.cast::<XpEpubLeaves>() };
    // Native %.*s stops at either the byte precision or the first NUL.
    let string = |bytes, length: u32| {
        if length == 0 {
            return &[][..];
        }
        let bytes = unsafe { slice::from_raw_parts(bytes, length.min(i32::MAX as u32) as usize) };
        &bytes[..bytes
            .iter()
            .position(|&byte| byte == 0)
            .unwrap_or(bytes.len())]
    };
    let value = string(text, size);
    let second = string(other, other_size);
    macro_rules! emit {
        ($level:expr, $origin:expr, $($arg:tt)*) => {
            log::message(leaves, $level, $origin, format_args!($($arg)*))
        };
    }
    let parts =
        |origin, parts: &[&[u8]]| log::with_bytes(leaves, 0, origin, parts, format_args!(""));
    match event {
        x if x == Event::NavBegin as u32 => emit!(0, b"NAV", "Found nav toc element"),
        x if x == Event::NavEnd as u32 => emit!(0, b"NAV", "Finished parsing nav toc"),
        x if x == Event::MetadataTruncated as u32 => {
            emit!(0, b"COF", "Metadata text exceeds {} bytes; truncating", a)
        }
        x if x == Event::CollectionIgnored as u32 => emit!(
            0,
            b"COF",
            "Ignoring collection metadata beyond {} entries",
            a
        ),
        x if x == Event::CoverIgnored as u32 => parts(
            b"COF",
            &[
                b"Ignoring meta cover item '",
                value,
                b"' with non-image media type: ",
                second,
            ],
        ),
        x if x == Event::NcxDuplicate as u32 => parts(
            b"COF",
            &[
                b"Warning: Multiple NCX files found in manifest. Ignoring duplicate: ",
                value,
            ],
        ),
        x if x == Event::NavDocument as u32 => {
            parts(b"COF", &[b"Found EPUB 3 nav document: ", value])
        }
        x if x == Event::GuideStart as u32 => {
            parts(b"COF", &[b"Found start reference in guide: ", value])
        }
        x if x == Event::GuideCover as u32 => {
            parts(b"COF", &[b"Found cover reference in guide: ", value])
        }
        x if x == Event::ManifestIndex as u32 => {
            emit!(0, b"COF", "Using fast index for {} manifest items", a)
        }
        x if x == Event::GuideBegin as u32 => emit!(0, b"COF", "Entering guide state."),
        x if x == Event::IndexOom as u32 => emit!(
            1,
            b"BMC",
            "Failed to allocate spine href index; scanning {} spine items per TOC entry",
            a as i32
        ),
        x if x == Event::IndexReady as u32 => {
            emit!(0, b"BMC", "Using fast index for {} spine items", a as i32)
        }
        x if x == Event::SpineClosed as u32 => {
            emit!(0, b"BMC", "createSpineEntry called but not in build mode")
        }
        x if x == Event::TocClosed as u32 => {
            emit!(0, b"BMC", "createTocEntry called but not in build mode")
        }
        x if x == Event::MissingTocHref as u32 => parts(
            b"BMC",
            &[
                b"createTocEntry: Could not find spine item for TOC href ",
                value,
            ],
        ),
        x if x == Event::MissingSpineTitle as u32 => log::with_tail(
            leaves,
            0,
            b"BMC",
            format_args!(
                "Warning: Could not find TOC entry for spine item {}: ",
                a as i32
            ),
            &[value, b", using title from last section"],
        ),
        _ => (),
    }
}

pub(super) fn allocation(leaves: &XpEpubLeaves, kind: Kind) {
    let (level, origin): (u8, &[u8]) = match kind {
        Kind::Container => (1, b"CTR"),
        Kind::Opf => (0, b"COF"),
        Kind::Nav => (0, b"NAV"),
        Kind::Ncx => (0, b"TOC"),
        Kind::Encryption => (1, b"PKG"),
    };
    let text = if kind == Kind::Encryption {
        "Could not allocate package pass"
    } else {
        "Couldn't allocate memory for parser"
    };
    log::message(leaves, level, origin, format_args!("{}", text));
}

pub(super) fn error(leaves: &XpEpubLeaves, kind: Kind, error: &Error) {
    if error.code == 0 || kind == Kind::Encryption {
        return;
    }
    let origin: &[u8] = match kind {
        Kind::Container => b"CTR",
        Kind::Opf => b"COF",
        Kind::Nav => b"NAV",
        Kind::Ncx => b"TOC",
        Kind::Encryption => unreachable!(),
    };
    if error.buffer != 0 {
        let text = if kind == Kind::Container {
            "Couldn't allocate buffer"
        } else {
            "Couldn't allocate memory for buffer"
        };
        log::message(
            leaves,
            u8::from(kind == Kind::Opf),
            origin,
            format_args!("{}", text),
        );
    } else if kind == Kind::Container {
        log::message(
            leaves,
            1,
            origin,
            format_args!("Parse error: {}", xml_error(error.code)),
        );
    } else {
        log::message(
            leaves,
            0,
            origin,
            format_args!(
                "Parse error at line {}: {}",
                error.line,
                xml_error(error.code)
            ),
        );
    }
}

// Match the retained Expat diagnostic vocabulary in lib/expat/ExpatErrors.cpp.
pub(in crate::app::epub) fn xml_error(code: u32) -> &'static str {
    match code {
        1 => "out of memory",
        2 => "syntax error",
        3 => "no element found",
        4 => "not well-formed (invalid token)",
        5 => "unclosed token",
        6 => "partial character",
        7 => "mismatched tag",
        8 => "duplicate attribute",
        9 => "junk after document element",
        10 => "illegal parameter entity reference",
        11 => "undefined entity",
        12 => "recursive entity reference",
        13 => "asynchronous entity",
        14 => "reference to invalid character number",
        15 => "reference to binary entity",
        16 => "reference to external entity in attribute",
        17 => "XML or text declaration not at start of entity",
        18 => "unknown encoding",
        19 => "encoding specified in XML declaration is incorrect",
        20 => "unclosed CDATA section",
        21 => "error in processing external entity reference",
        22 => "document is not standalone",
        23 => "unexpected parser state - please send a bug report",
        24 => "entity declared in parameter entity",
        25 => "requested feature requires XML_DTD support in Expat",
        26 => "cannot change setting once parsing has begun",
        27 => "unbound prefix",
        28 => "must not undeclare prefix",
        29 => "incomplete markup in parameter entity",
        30 => "XML declaration not well-formed",
        31 => "text declaration not well-formed",
        32 => "illegal character(s) in public id",
        33 => "parser suspended",
        34 => "parser not suspended",
        35 => "parsing aborted",
        36 => "parsing finished",
        37 => "cannot suspend in external parameter entity",
        38 => "reserved prefix (xml) must not be undeclared or bound to another namespace name",
        39 => "reserved prefix (xmlns) must not be declared or undeclared",
        40 => "prefix must not be bound to one of the reserved namespace names",
        41 => "invalid argument",
        42 => "a successful prior call to function XML_GetBuffer is required",
        43 => "limit on input amplification factor (from DTD and entities) breached",
        44 => "parser not started",
        _ => "(null)",
    }
}
