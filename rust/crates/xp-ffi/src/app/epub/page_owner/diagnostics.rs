use super::XpEpubLeaves;
use crate::app::epub::book_owner::log;
use xp_core::section::{Diagnostic, DiagnosticEvent};

pub(super) fn allocation(leaves: &XpEpubLeaves) {
    log::message(
        leaves,
        1,
        b"PGE",
        format_args!("Deserialization failed: could not allocate Page"),
    );
}

pub(super) fn decode(leaves: &XpEpubLeaves, detail: Diagnostic) {
    use DiagnosticEvent::*;
    let (a, b) = (detail.a, detail.b);
    let message = match detail.event {
        None => Option::None,
        WordCount => Some((
            b"TXB",
            format_args!("Deserialization failed: word count {} exceeds maximum", a),
        )),
        TextSize => Some((
            b"TXB",
            format_args!(
                "Deserialization failed: bad text size {} for {} words",
                a, b
            ),
        )),
        Rule => Some((
            b"PGE",
            format_args!(
                "Deserialization failed: invalid horizontal rule metadata (width={} thickness={})",
                a, b
            ),
        )),
        DropCap => Some((
            b"PGE",
            format_args!("Deserialization failed: invalid drop cap"),
        )),
        BorderStyle => Some((
            b"PGE",
            format_args!("Deserialization failed: invalid border style {}", a),
        )),
        EmptyBorder => Some((
            b"PGE",
            format_args!("Deserialization failed: empty border box"),
        )),
        Tag => Some((
            b"PGE",
            format_args!("Deserialization failed: Unknown tag {}", a),
        )),
        FootnoteCount => Some((b"PGE", format_args!("Invalid footnote count {}", a))),
        LinkCount => Some((b"PGE", format_args!("Invalid link count {}", a))),
        FontFlag => Some((
            b"TXB",
            format_args!("Deserialization failed: bad word font flag {}", a),
        )),
        ArenaRead => Some((
            b"TXB",
            format_args!("Deserialization failed: arena read ({} bytes)", a),
        )),
        ArenaLayout => Some((
            b"TXB",
            format_args!("Deserialization failed: corrupt text layout"),
        )),
        WordOffset => Some((
            b"TXB",
            format_args!("Deserialization failed: corrupt word offset {}", a),
        )),
        WordFonts => Some((b"TXB", format_args!("Deserialization failed: word fonts"))),
        FontSlot => Some((
            b"TXB",
            format_args!("Deserialization failed: bad word font slot {}", a),
        )),
        FootnoteRead => Some((b"PGE", format_args!("Failed to read footnote {}", a))),
        LinkRead => Some((b"PGE", format_args!("Failed to read link {}", a))),
        LinkGeometry => Some((b"PGE", format_args!("Invalid link geometry {}", a))),
    };
    if let Some((origin, message)) = message {
        log::message(leaves, 1, origin, message);
    }
    if detail.element == 1 {
        log::message(
            leaves,
            1,
            b"PGE",
            format_args!("Deserialization failed: null TextBlock"),
        );
    }
}
