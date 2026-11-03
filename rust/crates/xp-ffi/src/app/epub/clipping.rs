use super::{abi, app_abi, EpubSession, ResourceAccess};
use core::{ffi::c_void, mem::MaybeUninit, ptr::NonNull};
use xp_ui::reader::render::Query;

const PAGE_LINE: u8 = 1;
const UNDERLINE: u8 = 4;
const EM_SPACE: &[u8] = b"\xe2\x80\x83";
const EM_SPACE_C: &[u8] = b"\xe2\x80\x83\0";

#[derive(Clone, Copy)]
struct Rect {
    x: i32,
    y: i32,
    width: i32,
    height: i32,
}

struct ClipPage<'a, 'b> {
    owner: &'a EpubSession,
    access: &'a ResourceAccess<'b>,
    page: NonNull<c_void>,
}

impl ClipPage<'_, '_> {
    fn elements(&self) -> u16 {
        let mut info = MaybeUninit::<abi::XpEpubPageInfo>::uninit();
        unsafe {
            (self.owner.leaves.page_info)(self.page.as_ptr(), info.as_mut_ptr());
            info.assume_init().elements
        }
    }

    fn element(&self, index: u16) -> abi::XpEpubElement {
        let mut element = MaybeUninit::uninit();
        unsafe {
            (self.owner.leaves.page_element)(self.page.as_ptr(), index, element.as_mut_ptr());
            element.assume_init()
        }
    }

    fn word(&self, element: u16, index: u16) -> abi::XpEpubWord {
        let mut word = MaybeUninit::uninit();
        unsafe {
            (self.owner.leaves.page_word)(self.page.as_ptr(), element, index, word.as_mut_ptr());
            word.assume_init()
        }
    }

    fn word_font(&self, element: u16, index: u16, base: i32) -> i32 {
        unsafe { (self.owner.leaves.page_word_font)(self.page.as_ptr(), element, index, base) }
    }

    fn width(&self, word: &abi::XpEpubWord, font: i32) -> i32 {
        unsafe {
            (self.owner.leaves.font_text_advance)(
                word.text,
                font,
                word.style & !UNDERLINE,
                word.character_spacing,
            )
        }
    }

    // Keep the lazy Page metric inputs explicit and in their physical query order.
    #[allow(clippy::too_many_arguments)]
    fn rect(
        &self,
        element: u16,
        line: &abi::XpEpubElement,
        index: u16,
        word: &abi::XpEpubWord,
        base: i32,
        top: i32,
        left: i32,
        word_width: i32,
    ) -> Rect {
        let font = self.word_font(element, index, base);
        let text = unsafe { word.text.bytes() }.unwrap_or_default();
        let skip = if text.starts_with(EM_SPACE) {
            unsafe {
                (self.owner.leaves.font_text_advance)(
                    app_abi::Text::from_terminated(EM_SPACE_C),
                    font,
                    word.style & !UNDERLINE,
                    0,
                )
            }
        } else {
            0
        };
        let mut width = word_width.wrapping_sub(skip);
        if u32::from(index) + 1 < u32::from(line.words) {
            let next = self.word(element, index + 1).x;
            if next > word.x {
                width = width.min(next.wrapping_sub(word.x).wrapping_sub(skip));
            }
        }
        let line_font =
            unsafe { (self.owner.leaves.page_line_font)(self.page.as_ptr(), element, base) };
        let x = left
            .wrapping_add(line.x)
            .wrapping_add(word.x)
            .wrapping_add(skip);
        let ruby = unsafe {
            (self.owner.leaves.page_line_ruby_shift)(self.page.as_ptr(), element, line_font)
        };
        let offset = unsafe {
            (self.owner.leaves.page_word_y_offset)(self.page.as_ptr(), element, index, base)
        };
        let y = top
            .wrapping_add(line.y)
            .wrapping_add(ruby)
            .wrapping_add(offset);
        let height = unsafe { (self.owner.resources.line_height)(font) };
        Rect {
            x,
            y,
            width,
            height,
        }
    }

    fn fill(&self, rect: Rect) {
        let services =
            unsafe { &*(self.owner.leaves.render_services)(self.owner.native(self.access)) };
        let read = services.read.expect("reader renderer");
        if unsafe { read(services.context, Query::IsA4 as u8) } != 0
            && unsafe { read(services.context, Query::IsBw as u8) } == 0
        {
            let mut y = rect.y.wrapping_add(rect.y & 1);
            while y < rect.y.wrapping_add(rect.height) {
                let mut x = rect.x.wrapping_add(rect.x & 1);
                while x < rect.x.wrapping_add(rect.width) {
                    unsafe { (self.owner.leaves.paint_pixel)(x, y) };
                    x = x.wrapping_add(2);
                }
                y = y.wrapping_add(2);
            }
        } else {
            unsafe { (self.owner.leaves.paint_dither)(rect.x, rect.y, rect.width, rect.height) };
        }
    }
}

fn visible(word: &abi::XpEpubWord) -> bool {
    unsafe { word.text.bytes() }
        .unwrap_or_default()
        .iter()
        .take_while(|&&byte| byte != 0)
        .any(|&byte| byte > b' ')
}

impl EpubSession {
    pub(super) fn clipping_layout_signature(&self) -> u32 {
        let (width, height) = self.model(|state| state.viewport);
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.clipping_hooks {
            return unsafe { (hooks.signature)(hooks.context, width, height) };
        }
        let mut spec = MaybeUninit::<abi::XpEpubRenderSpec>::uninit();
        unsafe { (self.leaves.settings_render_spec)(width, height, spec.as_mut_ptr()) };
        let spec = unsafe { spec.assume_init() };
        let fields = [
            spec.font as u32,
            0,
            u32::from(spec.paragraph_spacing),
            u32::from(spec.character_spacing as u8),
            u32::from(spec.word_spacing_percent),
            u32::from(spec.alignment),
            u32::from(spec.width),
            u32::from(spec.height),
            u32::from(spec.hyphenation),
            u32::from(spec.embedded_style),
            u32::from(spec.images),
            u32::from(spec.focus_reading),
        ];
        unsafe { crate::book_policy::xp_book_layout_signature(&fields, spec.compression) }
    }

    pub(super) fn has_clippings(&self) -> bool {
        let nonempty = unsafe { (self.leaves.clippings_count)() } != 0;
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.clipping_hooks {
            unsafe { (hooks.nonempty)(hooks.context, u8::from(nonempty)) };
        }
        nonempty
    }

    fn clipping_row(&self, index: u32) -> abi::XpEpubClip {
        let mut row = MaybeUninit::uninit();
        unsafe {
            (self.leaves.clippings_row)(index, row.as_mut_ptr());
            row.assume_init()
        }
    }

    fn find_clipping(
        &self,
        word: u16,
        start: u32,
        end: u32,
        page: u16,
        signature: u32,
    ) -> Option<u32> {
        let mut index = 0;
        while index < unsafe { (self.leaves.clippings_count)() } {
            let row = self.clipping_row(index);
            let (spine, count) = self.model(|state| {
                (
                    state.current.spine as u16,
                    state.facts.section.map_or(0, |facts| facts.pages),
                )
            });
            let matched = if row.spine != spine {
                false
            } else if row.start_offset != u32::MAX && row.end_offset != u32::MAX {
                start != u32::MAX && end > row.start_offset && start < row.end_offset
            } else {
                row.page_count == count
                    && row.signature != 0
                    && signature != 0
                    && row.signature == signature
                    && page >= row.start_page
                    && page <= row.end_page
                    && (page != row.start_page || word >= row.start_word)
                    && (page != row.end_page || word <= row.end_word)
            };
            if matched {
                return Some(index);
            }
            index += 1;
        }
        None
    }

    pub(super) fn clipping_at_point(
        &self,
        access: &ResourceAccess<'_>,
        page: NonNull<c_void>,
        x: i32,
        y: i32,
    ) -> i32 {
        self.check_resources(access);
        #[cfg(feature = "app-test")]
        if let Some(hooks) = self.child_hooks {
            return unsafe { (hooks.hit)(hooks.context, page.as_ptr(), x, y) };
        }
        let page = ClipPage {
            owner: self,
            access,
            page,
        };
        let mut margins = [0; 4];
        unsafe { (self.leaves.render_margins)(margins.as_mut_ptr()) };
        let font = self.settings(true).font;
        let signature = self.clipping_layout_signature();
        let mut ordinal = 0u16;
        for element in 0..page.elements() {
            let line = page.element(element);
            if line.tag != PAGE_LINE || line.valid == 0 {
                continue;
            }
            for index in 0..line.words {
                let word = page.word(element, index);
                if !visible(&word) {
                    continue;
                }
                let word_font = page.word_font(element, index, font);
                let width = page.width(&word, word_font);
                if width <= 0 {
                    continue;
                }
                let current = ordinal;
                ordinal = ordinal.wrapping_add(1);
                let margin = i32::from(self.settings(false).screen_margin);
                let rect = page.rect(
                    element,
                    &line,
                    index,
                    &word,
                    font,
                    margins[0].wrapping_add(margin),
                    margins[3].wrapping_add(margin),
                    width,
                );
                if x < rect.x
                    || x >= rect.x.wrapping_add(rect.width)
                    || y < rect.y
                    || y >= rect.y.wrapping_add(rect.height)
                {
                    continue;
                }
                let active = self.model(|state| state.current.page as u16);
                if let Some(found) = self.find_clipping(
                    current,
                    word.source_start,
                    word.source_end,
                    active,
                    signature,
                ) {
                    return found as i32;
                }
            }
        }
        -1
    }

    pub(super) fn draw_clipping_highlights(
        &self,
        access: &ResourceAccess<'_>,
        page: NonNull<c_void>,
        font: i32,
        top: i32,
        left: i32,
    ) {
        if self.section(access).is_none() || !self.has_clippings() {
            return;
        }
        let (active, count) = self.model(|state| {
            (
                state.current.page,
                state.facts.section.map_or(0, |facts| facts.pages),
            )
        });
        if active < 0 || active >= i32::from(count) {
            return;
        }
        let mut chapter = 0u32;
        let mut index = 0;
        while index < unsafe { (self.leaves.clippings_count)() } {
            if i32::from(self.clipping_row(index).spine) == self.model(|state| state.current.spine)
            {
                chapter += 1;
            }
            index += 1;
        }
        if chapter == 0 {
            return;
        }
        let started = unsafe { (self.runtime.now_ms)() };
        let signature = self.clipping_layout_signature();
        let page = ClipPage {
            owner: self,
            access,
            page,
        };
        let mut highlighted = 0u32;
        let mut ordinal = 0u16;
        for element in 0..page.elements() {
            let line = page.element(element);
            if line.tag != PAGE_LINE || line.valid == 0 {
                continue;
            }
            let mut previous: Option<(i32, i32)> = None;
            for index in 0..line.words {
                let word = page.word(element, index);
                if !visible(&word) {
                    continue;
                }
                let word_font = page.word_font(element, index, font);
                let width = page.width(&word, word_font);
                if width <= 0 {
                    continue;
                }
                let current = ordinal;
                ordinal = ordinal.wrapping_add(1);
                if self
                    .find_clipping(
                        current,
                        word.source_start,
                        word.source_end,
                        active as u16,
                        signature,
                    )
                    .is_none()
                {
                    previous = None;
                    continue;
                }
                let rect = page.rect(element, &line, index, &word, font, top, left, width);
                if rect.width <= 0 {
                    previous = None;
                    continue;
                }
                let right = rect.x.wrapping_add(rect.width);
                if let Some((x, width)) = previous {
                    let previous_right = x.wrapping_add(width);
                    let bridge = if previous_right < rect.x {
                        Some((previous_right, rect.x.wrapping_sub(previous_right)))
                    } else if right < x {
                        Some((right, x.wrapping_sub(right)))
                    } else {
                        None
                    };
                    if let Some((x, width)) = bridge {
                        let height = unsafe { (self.resources.line_height)(font) };
                        page.fill(Rect {
                            x,
                            y: rect.y,
                            width,
                            height,
                        });
                    }
                }
                let height = unsafe { (self.resources.line_height)(font) };
                page.fill(Rect { height, ..rect });
                previous = Some((rect.x, rect.width));
                highlighted += 1;
            }
        }
        if unsafe { (self.leaves.log_enabled)(0) } != 0 {
            let elapsed = unsafe { (self.runtime.now_ms)() }.wrapping_sub(started);
            self.log_clipping(chapter, highlighted, elapsed);
        }
    }

    #[inline(never)]
    fn log_clipping(&self, chapter: u32, highlighted: u32, elapsed: u32) {
        // The longest message is 86 bytes, including three ten-digit values.
        let mut text = RenderLog {
            bytes: [0; 96],
            len: 0,
        };
        text.append(b"Highlight pass: chapter_clips=");
        text.number(chapter);
        text.append(b" highlighted_words=");
        text.number(highlighted);
        text.append(b" time=");
        text.number(elapsed);
        text.append(b"ms");
        unsafe {
            (self.leaves.log)(
                0,
                app_abi::Text::from_bytes(b"CLIP"),
                app_abi::Text::from_bytes(&text.bytes[..text.len]),
            )
        };
    }
}

struct RenderLog {
    bytes: [u8; 96],
    len: usize,
}
impl RenderLog {
    fn append(&mut self, bytes: &[u8]) {
        self.bytes[self.len..self.len + bytes.len()].copy_from_slice(bytes);
        self.len += bytes.len();
    }
    fn number(&mut self, value: u32) {
        let mut divisor = 1_000_000_000;
        while divisor > 1 && value < divisor {
            divisor /= 10;
        }
        loop {
            self.bytes[self.len] = b'0' + ((value / divisor) % 10) as u8;
            self.len += 1;
            if divisor == 1 {
                break;
            }
            divisor /= 10;
        }
    }
}
