extern crate alloc;

use super::{
    abi,
    resources::{Identity, Text},
};
use alloc::vec::Vec;
use core::{ffi::c_void, fmt::Write};
use xp_core::i18n::ids::*;
use xp_ui::{
    home::{grid, view},
    scene::Error,
};

pub const WRAPPED_TITLE: u32 = 0x10000;
pub const CLIPPED_AUTHOR: u32 = 0x11000;
pub const CLIPPED_MENU: u32 = 0x12000;

pub struct PreparedTexts {
    wrapped: [Vec<Text>; 3],
    author: Text,
    menu: [Text; 6],
    pub keys: [u8; 6],
    pub count: usize,
    progress: [u8; 12],
    pub grid: bool,
    pub body_bold: bool,
}

impl PreparedTexts {
    pub fn new(progress: i32, grid: bool) -> Self {
        let mut text = [0; 12];
        if progress >= 0 {
            struct Writer<'a>(&'a mut [u8], usize);
            impl Write for Writer<'_> {
                fn write_str(&mut self, value: &str) -> core::fmt::Result {
                    let end = self.1.checked_add(value.len()).ok_or(core::fmt::Error)?;
                    if end >= self.0.len() {
                        return Err(core::fmt::Error);
                    }
                    self.0[self.1..end].copy_from_slice(value.as_bytes());
                    self.1 = end;
                    Ok(())
                }
            }
            let _ = write!(Writer(&mut text, 0), "{progress}%");
        }
        Self {
            wrapped: core::array::from_fn(|_| Vec::new()),
            author: Text::default(),
            menu: core::array::from_fn(|_| Text::default()),
            keys: [0; 6],
            count: 0,
            progress: text,
            grid,
            body_bold: false,
        }
    }
}

pub struct Texts<'a> {
    pub identities: &'a [Identity],
    pub prepared: &'a mut PreparedTexts,
    pub services: &'static abi::Resources,
}

impl Texts<'_> {
    pub fn lookup(&self, handle: u32) -> Option<abi::Text> {
        if (view::TITLE..view::TITLE + 3).contains(&handle) {
            return self
                .identities
                .get((handle - view::TITLE) as usize)
                .map(|book| book.title.wire());
        }
        if (view::AUTHOR..view::AUTHOR + 3).contains(&handle) {
            return self
                .identities
                .get((handle - view::AUTHOR) as usize)
                .map(|book| book.author.wire());
        }
        if (WRAPPED_TITLE..WRAPPED_TITLE + 12).contains(&handle) {
            let n = (handle - WRAPPED_TITLE) as usize;
            return self.prepared.wrapped[n / 4].get(n % 4).map(Text::wire);
        }
        if handle == CLIPPED_AUTHOR {
            return Some(self.prepared.author.wire());
        }
        if (CLIPPED_MENU..CLIPPED_MENU + self.prepared.count as u32).contains(&handle) {
            return Some(self.prepared.menu[(handle - CLIPPED_MENU) as usize].wire());
        }
        if handle == grid::PROGRESS && self.prepared.grid {
            let length = self
                .prepared
                .progress
                .iter()
                .position(|b| *b == 0)
                .unwrap_or(11);
            return Some(abi::Text::from_bytes(&self.prepared.progress[..length]));
        }
        let string_id = if (view::MENU..view::MENU + self.prepared.count as u32).contains(&handle) {
            match self.prepared.keys[(handle - view::MENU) as usize] {
                0 => STR_CONTINUE_READING,
                1 => STR_BROWSE_FILES,
                2 => STR_LIBRARY,
                3 => STR_PLUGINS,
                4 => STR_OPDS_BROWSER,
                5 => STR_FILE_TRANSFER,
                6 => STR_SETTINGS_TITLE,
                _ => return None,
            }
        } else {
            match handle {
                view::NO_OPEN_BOOK => STR_NO_OPEN_BOOK,
                view::START_READING => STR_START_READING,
                view::CONTINUE => STR_CONTINUE_READING,
                _ => return None,
            }
        };
        Some(unsafe { (self.services.translate)(string_id) })
    }

    fn measure(&self, handle: u32, font: i32, bold: bool) -> Result<i16, Error> {
        let text = self.lookup(handle).ok_or(Error::Invalid)?;
        i16::try_from(unsafe { (self.services.text_width)(text, font, u8::from(bold)) })
            .map_err(|_| Error::Invalid)
    }

    fn line_height(&self, font: i32) -> Result<i16, Error> {
        i16::try_from(unsafe { (self.services.line_height)(font) }).map_err(|_| Error::Invalid)
    }

    fn is_empty(&self, handle: u32) -> bool {
        self.lookup(handle).is_none_or(|text| {
            text.len == 0 || unsafe { text.bytes() }.is_none_or(|b| b.first() == Some(&0))
        })
    }
}

impl view::Texts for Texts<'_> {
    fn width(&mut self, handle: u32, font: i32, bold: bool) -> Result<i16, Error> {
        self.measure(handle, font, bold)
    }
    fn line(&mut self, font: i32) -> Result<i16, Error> {
        self.line_height(font)
    }
    fn empty(&mut self, handle: u32) -> bool {
        self.is_empty(handle)
    }

    fn wrap(
        &mut self,
        handle: u32,
        font: i32,
        width: i16,
        max: u8,
        bold: bool,
    ) -> Result<view::Lines, Error> {
        if !(view::TITLE..view::TITLE + 3).contains(&handle) {
            return Err(Error::Invalid);
        }
        let text = self.lookup(handle).ok_or(Error::Invalid)?;
        let book = (handle - view::TITLE) as usize;
        let lines = &mut self.prepared.wrapped[book];
        lines.clear();
        lines
            .try_reserve_exact(usize::from(max))
            .map_err(|_| Error::Capacity)?;
        struct Capture<'a> {
            lines: &'a mut Vec<Text>,
            max: usize,
            failed: bool,
        }
        unsafe extern "C" fn sink(context: *mut c_void, value: abi::Text) -> u8 {
            let capture = unsafe { &mut *context.cast::<Capture<'_>>() };
            if capture.failed || capture.lines.len() >= capture.max {
                capture.failed = true;
                return 0;
            }
            let Some(bytes) = (unsafe { value.bytes() }) else {
                capture.failed = true;
                return 0;
            };
            let Ok(text) = Text::copy(bytes) else {
                capture.failed = true;
                return 0;
            };
            capture.lines.push(text);
            1
        }
        let mut capture = Capture {
            lines,
            max: usize::from(max),
            failed: false,
        };
        let layout = abi::TextLayout {
            font,
            width: width.into(),
            lines: max,
            style: u8::from(bold),
        };
        let success = unsafe {
            (self.services.text_wrap)(
                text,
                layout,
                sink,
                (&mut capture as *mut Capture<'_>).cast(),
            )
        };
        if success == 0 || capture.failed {
            return Err(Error::Capacity);
        }
        let mut max_width = 0;
        for text in &*capture.lines {
            max_width = max_width
                .max(unsafe { (self.services.text_width)(text.wire(), font, u8::from(bold)) });
        }
        Ok(view::Lines {
            first: WRAPPED_TITLE + 4 * book as u32,
            count: capture.lines.len() as u8,
            reserved: 0,
            max_width: i16::try_from(max_width).map_err(|_| Error::Invalid)?,
        })
    }

    fn truncate(&mut self, handle: u32, font: i32, width: i16, bold: bool) -> Result<u32, Error> {
        let source = self.lookup(handle).ok_or(Error::Invalid)?;
        let layout = abi::TextLayout {
            font,
            width: width.into(),
            lines: 1,
            style: u8::from(bold),
        };
        let text = Text::receive(|sink, context| unsafe {
            (self.services.text_truncate)(source, layout, sink, context)
        })
        .ok_or(Error::Capacity)?;
        if handle == view::AUTHOR {
            self.prepared.author = text;
            Ok(CLIPPED_AUTHOR)
        } else if (view::MENU..view::MENU + self.prepared.count as u32).contains(&handle) {
            self.prepared.menu[(handle - view::MENU) as usize] = text;
            Ok(CLIPPED_MENU + handle - view::MENU)
        } else {
            Err(Error::Invalid)
        }
    }
}

impl grid::Texts for Texts<'_> {
    fn width(&mut self, handle: u32, font: i32) -> Result<i16, Error> {
        self.measure(handle, font, false)
    }
    fn line(&mut self, font: i32) -> Result<i16, Error> {
        self.line_height(font)
    }
    fn empty(&mut self, handle: u32) -> bool {
        self.is_empty(handle)
    }
    fn height(&mut self, handle: u32, font: i32, width: i16, lines: u8) -> Result<i16, Error> {
        let text = self.lookup(handle).ok_or(Error::Invalid)?;
        let layout = abi::TextLayout {
            font,
            width: width.into(),
            lines,
            style: u8::from(self.prepared.body_bold),
        };
        i16::try_from(unsafe { (self.services.text_box_height)(text, layout) })
            .map_err(|_| Error::Invalid)
    }
}
