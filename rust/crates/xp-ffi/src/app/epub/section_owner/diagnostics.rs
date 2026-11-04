use super::super::{abi::XpEpubLeaves, book_owner::log};
use xp_core::chapter::assembly::Diagnostic;

pub(super) fn chapter(
    leaves: &XpEpubLeaves,
    event: Diagnostic,
    a: u32,
    b: u32,
    c: u32,
    text: &[u8],
) {
    use Diagnostic::*;
    let (severity, message) = match event {
        BorderBox => (1, format_args!("OOM: PageBorderBox")),
        DropCap => (1, format_args!("OOM: PageDropCap")),
        Paragraph => (1, format_args!("OOM: ParsedText")),
        HorizontalRule => (1, format_args!("Failed to create PageHorizontalRule")),
        RowSeparator => (1, format_args!("OOM: table row separator")),
        CellBorder => (1, format_args!("OOM: table cell border")),
        TableCell => (1, format_args!("OOM: table cell")),
        ImageBlock => (1, format_args!("Failed to create ImageBlock")),
        PageImage => (1, format_args!("Failed to create PageImage")),
        CharacterParagraph => (1, format_args!("OOM: text block for character data")),
        AfterTableParagraph => (1, format_args!("OOM: text block after table")),
        PageLine => (1, format_args!("OOM: PageLine")),
        NoParagraph => (1, format_args!("!! No text block to make pages for !!")),
        SoftFlush => (0, format_args!("Text block soft flush ({} words)", a)),
        NestedTable => (
            0,
            format_args!("nested table flattened into enclosing cell"),
        ),
        ImageCssBoth => (
            0,
            format_args!(
                "Display size from CSS height+width: {}x{}",
                a as i32, b as i32
            ),
        ),
        ImageCssHeight => (
            0,
            format_args!("Display size from CSS height: {}x{}", a as i32, b as i32),
        ),
        ImageCssWidth => (
            0,
            format_args!("Display size from CSS width: {}x{}", a as i32, b as i32),
        ),
        ImageScale => (
            0,
            format_args!(
                "Display size: {}x{} (scale {:.2})",
                a as i32,
                b as i32,
                f32::from_bits(c)
            ),
        ),
        DroppedLink => {
            log::with_bytes(
                leaves,
                0,
                b"EHP",
                &[b"Dropped page link: ", &text[..text.len().min(48)]],
                format_args!(""),
            );
            return;
        }
    };
    log::message(leaves, severity, b"EHP", message);
}

pub(super) fn parse_error(leaves: &XpEpubLeaves, result: u64) {
    let error = result as u32 >> 8;
    let message = super::super::book_owner::xml_error(error).as_bytes();
    if result as u8 == 1 {
        log::with_bytes(
            leaves,
            0,
            b"EHP",
            &[b"Ignoring trailing data after </html>: ", message],
            format_args!(""),
        );
    } else {
        // Match the native XML error spelling and line, without allocating text.
        log::with_tail(
            leaves,
            1,
            b"EHP",
            format_args!("Parse error at line {}:\n", result >> 32),
            &[message],
        );
    }
}
