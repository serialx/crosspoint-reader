//! Host-only access preserving the old direct semantic callback tests.
use super::*;
impl State {
    pub fn test_options(
        &mut self,
        css: *const crate::css::Store,
        alignment: u8,
        embedded: bool,
        images: u8,
        enabled: bool,
    ) {
        self.css = css;
        self.alignment = alignment;
        self.embedded = embedded;
        self.images = images;
        self.css_enabled = enabled;
    }
    pub fn test_query(&self, field: u32) -> u32 {
        match field {
            0 => self.link_id.into(),
            1 => self.word_len as u32,
            2 => self.italic.into(),
            _ => 0,
        }
    }
    pub fn test_columns(&mut self, first: u16, second: u16) {
        self.column_widths = [first, second, 0, 0];
        self.column_count = 2;
    }
}
