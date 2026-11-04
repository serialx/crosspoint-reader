use super::super::{
    abi::{XpEpubElement, XpEpubLeaves, XpEpubPageInfo, XpEpubWord},
    page::Page,
    Text,
};
use super::owner::ReaderSection;
use core::{mem::MaybeUninit, ptr::NonNull};

fn words(
    page: &Page,
    leaves: &XpEpubLeaves,
    mut visit: impl FnMut(&[u8]) -> Option<()>,
) -> Option<()> {
    let mut info = MaybeUninit::<XpEpubPageInfo>::uninit();
    unsafe { (leaves.page_info)(page.handle.as_ptr(), info.as_mut_ptr()) };
    for index in 0..unsafe { info.assume_init() }.elements {
        let mut element = MaybeUninit::<XpEpubElement>::uninit();
        unsafe { (leaves.page_element)(page.handle.as_ptr(), index, element.as_mut_ptr()) };
        let element = unsafe { element.assume_init() };
        if element.tag != 1 || element.valid == 0 {
            continue;
        }
        for index_word in 0..element.words {
            let mut word = MaybeUninit::<XpEpubWord>::uninit();
            unsafe {
                (leaves.page_word)(page.handle.as_ptr(), index, index_word, word.as_mut_ptr())
            };
            visit(unsafe { word.assume_init().text.bytes() }?)?;
        }
    }
    Some(())
}
impl ReaderSection {
    pub(in crate::app::epub) unsafe fn page_text(raw: NonNull<Self>, index: i32) -> Option<Text> {
        let leaves = unsafe { (*raw.as_ptr()).leaves };
        let Some(page) = (unsafe { Page::from_raw(Self::load_page(raw, index), leaves) }) else {
            return Some(Text::default());
        };
        // Page stays immutable for both walks; allocate the final terminated text once.
        let mut length = 0usize;
        words(&page, leaves, |word| {
            length = length
                .checked_add(usize::from(length != 0))?
                .checked_add(word.len())?;
            Some(())
        })?;
        let mut valid = true;
        let text = Text::read(length, |output| {
            let mut previous = 0;
            valid = words(&page, leaves, |word| {
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
