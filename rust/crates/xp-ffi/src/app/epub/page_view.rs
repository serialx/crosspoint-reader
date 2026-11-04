#[cfg(feature = "app-test")]
use super::test_support::legacy_page::LegacyPageServices;
use super::{abi, app_abi, page_records::*};
use xp_core::page;

#[derive(Clone, Copy)]
pub(super) enum View<'a> {
    Arena(&'a [u8]),
    #[cfg(feature = "app-test")]
    Legacy(
        core::ptr::NonNull<core::ffi::c_void>,
        &'static LegacyPageServices,
    ),
}

impl<'a> View<'a> {
    pub(super) unsafe fn from_wire(wire: &abi::XpEpubPageView) -> Self {
        Self::Arena(unsafe { core::slice::from_raw_parts(wire.arena, wire.size) })
    }

    pub(super) fn wire(self) -> abi::XpEpubPageView {
        let arena = self.arena();
        abi::XpEpubPageView {
            arena: arena.as_ptr(),
            size: arena.len(),
            visible_offset: page::Page::open(arena)
                .expect("validated Page")
                .visible_offset(),
        }
    }

    #[cfg(feature = "app-test")]
    pub(super) fn fixture_pointer(self) -> *mut core::ffi::c_void {
        match self {
            Self::Legacy(raw, _) => raw.as_ptr(),
            Self::Arena(_) => panic!("real Page cannot enter a synthetic owner oracle"),
        }
    }

    fn arena(self) -> &'a [u8] {
        match self {
            Self::Arena(arena) => arena,
            #[cfg(feature = "app-test")]
            Self::Legacy(..) => panic!("legacy query must use its named oracle"),
        }
    }
    fn core(self) -> page::Page<'a> {
        page::Page::open(self.arena()).expect("validated Page")
    }

    // Reborrow the complete terminated span from the owning arena.
    fn text(self, bytes: &[u8]) -> app_abi::Text {
        let arena = self.arena();
        let start = unsafe { bytes.as_ptr().offset_from(arena.as_ptr()) } as usize;
        let length = bytes
            .iter()
            .position(|byte| *byte == 0)
            .unwrap_or(bytes.len());
        app_abi::Text::from_terminated(&arena[start..start + length + 1])
    }

    pub(super) fn info(self, leaves: &abi::XpEpubLeaves) -> XpEpubPageInfo {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            let mut value = core::mem::MaybeUninit::uninit();
            unsafe {
                (services.page_info)(raw.as_ptr(), value.as_mut_ptr());
                return value.assume_init();
            }
        }
        let page = self.core();
        let mut images = false;
        let mut needs_decode = false;
        for index in 0..page.element_count() {
            let element = page.element(index).expect("validated element");
            if element.tag().expect("validated tag") != 2 {
                continue;
            }
            images = true;
            if !needs_decode {
                let (path, source, width, height) = element.image().expect("validated image");
                needs_decode = unsafe {
                    (leaves.image_needs_decode)(self.text(path), self.text(source), width, height)
                } != 0;
            }
        }
        XpEpubPageInfo {
            visible_offset: page.visible_offset(),
            elements: page.element_count(),
            footnotes: page.footnote_count(),
            links: page.link_count(),
            images: u8::from(images),
            images_need_decode: u8::from(needs_decode),
        }
    }

    pub(super) fn element(self, index: u16) -> XpEpubElement {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            let mut value = core::mem::MaybeUninit::uninit();
            unsafe {
                (services.page_element)(raw.as_ptr(), index, value.as_mut_ptr());
                return value.assume_init();
            }
        }
        let page = self.core();
        let (x, y, tag) = page.position(index).expect("validated element");
        let line = (tag == 1).then(|| {
            page.element(index)
                .expect("validated element")
                .line()
                .expect("validated line")
        });
        XpEpubElement {
            x: i32::from(x),
            y: i32::from(y),
            words: line.map_or(0, |v| v.count),
            tag: tag as u8,
            valid: u8::from(line.is_some()),
        }
    }

    fn line(self, element: u16) -> page::Line<'a> {
        self.core()
            .element(element)
            .expect("validated element")
            .line()
            .expect("validated line")
    }

    pub(super) fn word(self, element: u16, index: u16) -> XpEpubWord {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            let mut value = core::mem::MaybeUninit::uninit();
            unsafe {
                (services.page_word)(raw.as_ptr(), element, index, value.as_mut_ptr());
                return value.assume_init();
            }
        }
        let line = self.line(element);
        let word = line.word(index, 0).expect("validated word");
        XpEpubWord {
            text: self.text(word.text),
            x: i32::from(word.x),
            source_start: word.source_start,
            source_end: word.source_end,
            style: word.style,
            character_spacing: line.block_style().expect("validated style").values[14] as i8,
        }
    }

    pub(super) fn word_font(
        self,
        leaves: &abi::XpEpubLeaves,
        element: u16,
        index: u16,
        base: i32,
    ) -> i32 {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            return unsafe { (services.page_word_font)(raw.as_ptr(), element, index, base) };
        }
        let font = self
            .line(element)
            .word(index, base)
            .expect("validated word")
            .font;
        unsafe { (leaves.font_ready)(font, base) }
    }

    pub(super) fn line_font(self, leaves: &abi::XpEpubLeaves, element: u16, base: i32) -> i32 {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            return unsafe { (services.page_line_font)(raw.as_ptr(), element, base) };
        }
        let font = self.line(element).block_font(base).expect("validated font");
        unsafe { (leaves.font_ready)(font, base) }
    }

    pub(super) fn ruby_shift(self, leaves: &abi::XpEpubLeaves, element: u16, font: i32) -> i32 {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            return unsafe { (services.page_line_ruby_shift)(raw.as_ptr(), element, font) };
        }
        let ascent = unsafe { (leaves.font_ascent)(font) };
        if self.line(element).has_ruby() {
            ascent / 2
        } else {
            0
        }
    }

    pub(super) fn word_y_offset(
        self,
        leaves: &abi::XpEpubLeaves,
        element: u16,
        index: u16,
        base: i32,
    ) -> i32 {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            return unsafe { (services.page_word_y_offset)(raw.as_ptr(), element, index, base) };
        }
        let line = self.line(element);
        let Some(fonts) = line.fonts().expect("validated fonts") else {
            return 0;
        };
        let mut ascent = unsafe { (leaves.font_ascent)(self.line_font(leaves, element, base)) };
        for font in fonts {
            if font != 0 {
                ascent =
                    ascent.max(unsafe { (leaves.font_ascent)((leaves.font_ready)(font, base)) });
            }
        }
        ascent.wrapping_sub(unsafe {
            (leaves.font_ascent)(self.word_font(leaves, element, index, base))
        })
    }

    pub(super) fn paint(self, leaves: &abi::XpEpubLeaves, font: i32, left: i32, top: i32) {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(raw, services) = self {
            unsafe { (services.page_render)(raw.as_ptr(), font, left, top) };
            return;
        }
        unsafe { (leaves.paint_arena)(&self.wire(), font, left, top, 0) };
    }
}
