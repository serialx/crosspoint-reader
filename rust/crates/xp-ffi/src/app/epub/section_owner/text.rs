use super::super::{page_owner::Page, Text};
use super::owner::ReaderSection;
use core::ptr::NonNull;

fn words(page: &Page, mut visit: impl FnMut(&[u8]) -> Option<()>) -> Option<()> {
    let view = page.view();
    for index in 0..view.element_count() {
        let element = view.element(index).ok()?;
        if element.tag().ok()? != 1 {
            continue;
        }
        let line = element.line().ok()?;
        for word in 0..line.count {
            let text = line.word(word, 0).ok()?.text;
            let length = text
                .iter()
                .position(|byte| *byte == 0)
                .unwrap_or(text.len());
            visit(&text[..length])?;
        }
    }
    Some(())
}
impl ReaderSection {
    pub(in crate::app::epub) unsafe fn page_text(raw: NonNull<Self>, index: i32) -> Option<Text> {
        let Some(page) = (unsafe { Self::load_page(raw, index) }) else {
            return Some(Text::default());
        };
        // Page stays immutable for both walks; allocate the final terminated text once.
        let mut length = 0usize;
        words(&page, |word| {
            length = length
                .checked_add(usize::from(length != 0))?
                .checked_add(word.len())?;
            Some(())
        })?;
        let mut valid = true;
        let text = Text::read(length, |output| {
            let mut previous = 0;
            valid = words(&page, |word| {
                let count =
                    xp_core::section::encode_text_word(previous, word, output.get_mut(previous..)?)
                        .ok()?;
                previous += count;
                Some(())
            })
            .is_some()
                && previous == length;
        })
        .ok()?;
        valid.then_some(text)
    }
}
