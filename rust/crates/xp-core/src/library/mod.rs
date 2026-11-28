//! Library index collation and CLX1 persistence.
pub mod format;
pub mod reader;
pub mod text;

#[cfg(test)]
mod tests {
    extern crate alloc;
    use super::{format, text};
    use alloc::vec::Vec;

    fn run(input: &str, transform: fn(&mut [u8]) -> usize) -> Vec<u8> {
        let mut bytes = input.as_bytes().to_vec();
        let len = transform(&mut bytes);
        bytes.truncate(len);
        bytes
    }

    #[test]
    fn composed_names_and_author_spellings_share_keys() {
        assert_eq!(run("Été", text::fold), run("E\u{301}te\u{301}", text::fold));
        assert_eq!(run("Lu Xun [Xun, Lu]", text::author), b"lu xun");
        assert_eq!(run("Austen, Jane", text::clean), b"Jane Austen");
        assert_eq!(run("Herman Melville", text::surname), b"melville herman");
        assert!(text::matches(b"l'eneide", b"ene"));
    }

    #[test]
    fn truncated_multibyte_view_never_reads_past_its_end() {
        let mut bytes = [b'a', b' ', 0xe4, 0xb8];
        assert_eq!(text::fold(&mut bytes), 1);
        assert_eq!(bytes[0], b'a');
    }

    #[test]
    fn header_validation_preserves_rejection_precedence() {
        let mut header = format::Header {
            books: 2,
            ..format::Header::default()
        };
        format::layout(&mut header, 3, 27);
        let size = u64::from(header.size);
        assert_eq!(format::validate(&header, size, false), 0);
        assert_eq!(format::validate(&header, size - 1, false), 4);
        header.fold = 3;
        assert_eq!(format::validate(&header, size, false), 3);
        assert_eq!(format::validate(&header, size, true), 0);
        header.name_len = u32::MAX;
        assert_eq!(format::validate(&header, size, false), 6);
    }
}
