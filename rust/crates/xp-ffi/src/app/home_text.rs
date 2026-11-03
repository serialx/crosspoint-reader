extern crate alloc;

use super::{
    abi,
    resources::{Identity, Text},
};
use alloc::vec::Vec;
use core::{ffi::c_void, fmt::Write};
use xp_core::i18n::ids::*;
use xp_ui::{
    home::{grid, resource::Text as TextResource, view},
    scene::Error,
};

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
    pub fn lookup(&self, resource: TextResource) -> Option<abi::Text> {
        let string_id = match resource {
            TextResource::Title(book) => {
                return self
                    .identities
                    .get(usize::from(book))
                    .map(|book| book.title.wire())
            }
            TextResource::Author(book) => {
                return self
                    .identities
                    .get(usize::from(book))
                    .map(|book| book.author.wire())
            }
            TextResource::WrappedTitle { book, line } => {
                return self
                    .prepared
                    .wrapped
                    .get(usize::from(book))?
                    .get(usize::from(line))
                    .map(Text::wire)
            }
            TextResource::ClippedAuthor => return Some(self.prepared.author.wire()),
            TextResource::ClippedMenu(index) => {
                return (usize::from(index) < self.prepared.count)
                    .then(|| self.prepared.menu[usize::from(index)].wire())
            }
            TextResource::Progress if self.prepared.grid => {
                let length = self
                    .prepared
                    .progress
                    .iter()
                    .position(|b| *b == 0)
                    .unwrap_or(11);
                return Some(abi::Text::from_terminated(
                    &self.prepared.progress[..=length],
                ));
            }
            TextResource::Menu(index) if usize::from(index) < self.prepared.count => {
                match self.prepared.keys[usize::from(index)] {
                    0 => STR_CONTINUE_READING,
                    1 => STR_BROWSE_FILES,
                    2 => STR_LIBRARY,
                    3 => STR_PLUGINS,
                    4 => STR_OPDS_BROWSER,
                    5 => STR_FILE_TRANSFER,
                    6 => STR_SETTINGS_TITLE,
                    _ => return None,
                }
            }
            TextResource::NoOpenBook => STR_NO_OPEN_BOOK,
            TextResource::StartReading => STR_START_READING,
            TextResource::Continue => STR_CONTINUE_READING,
            _ => return None,
        };
        Some(unsafe { (self.services.translate)(string_id) })
    }

    fn measure(&self, handle: TextResource, font: i32, bold: bool) -> Result<i16, Error> {
        let text = self.lookup(handle).ok_or(Error::Invalid)?;
        i16::try_from(unsafe { (self.services.text_width)(text, font, u8::from(bold)) })
            .map_err(|_| Error::Invalid)
    }

    fn line_height(&self, font: i32) -> Result<i16, Error> {
        i16::try_from(unsafe { (self.services.line_height)(font) }).map_err(|_| Error::Invalid)
    }

    fn is_empty(&self, handle: TextResource) -> bool {
        self.lookup(handle).is_none_or(|text| {
            text.len == 0 || unsafe { text.bytes() }.is_none_or(|b| b.first() == Some(&0))
        })
    }
}

impl view::Texts for Texts<'_> {
    fn width(&mut self, handle: TextResource, font: i32, bold: bool) -> Result<i16, Error> {
        self.measure(handle, font, bold)
    }
    fn line(&mut self, font: i32) -> Result<i16, Error> {
        self.line_height(font)
    }
    fn empty(&mut self, handle: TextResource) -> bool {
        self.is_empty(handle)
    }

    fn wrap(
        &mut self,
        handle: TextResource,
        font: i32,
        width: i16,
        max: u8,
        bold: bool,
    ) -> Result<view::Lines, Error> {
        let TextResource::Title(book) = handle else {
            return Err(Error::Invalid);
        };
        let text = self.lookup(handle).ok_or(Error::Invalid)?;
        let book = usize::from(book);
        let lines = &mut self.prepared.wrapped[book];
        lines
            .try_reserve_exact(usize::from(max).saturating_sub(lines.len()))
            .map_err(|_| Error::Capacity)?;
        lines.resize_with(usize::from(max), Text::default);
        struct Capture<'a> {
            lines: &'a mut Vec<Text>,
            count: usize,
            failed: bool,
        }
        unsafe extern "C" fn sink(context: *mut c_void, value: abi::Text) -> u8 {
            let capture = unsafe { &mut *context.cast::<Capture<'_>>() };
            if capture.failed || capture.count >= capture.lines.len() {
                capture.failed = true;
                return 0;
            }
            let Some(bytes) = (unsafe { value.bytes() }) else {
                capture.failed = true;
                return 0;
            };
            let Ok(()) = capture.lines[capture.count].assign(bytes) else {
                capture.failed = true;
                return 0;
            };
            capture.count += 1;
            1
        }
        let mut capture = Capture {
            lines,
            count: 0,
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
        capture.lines.truncate(capture.count);
        let mut max_width = 0;
        for text in &*capture.lines {
            max_width = max_width
                .max(unsafe { (self.services.text_width)(text.wire(), font, u8::from(bold)) });
        }
        Ok(view::Lines {
            book: book as u8,
            count: capture.lines.len() as u8,
            max_width: i16::try_from(max_width).map_err(|_| Error::Invalid)?,
        })
    }

    fn truncate(
        &mut self,
        handle: TextResource,
        font: i32,
        width: i16,
        bold: bool,
    ) -> Result<TextResource, Error> {
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
        match handle {
            TextResource::Author(0) => {
                self.prepared.author = text;
                Ok(TextResource::ClippedAuthor)
            }
            TextResource::Menu(index) if usize::from(index) < self.prepared.count => {
                self.prepared.menu[usize::from(index)] = text;
                Ok(TextResource::ClippedMenu(index))
            }
            _ => Err(Error::Invalid),
        }
    }
}

impl grid::Texts for Texts<'_> {
    fn width(&mut self, handle: TextResource, font: i32) -> Result<i16, Error> {
        self.measure(handle, font, false)
    }
    fn line(&mut self, font: i32) -> Result<i16, Error> {
        self.line_height(font)
    }
    fn empty(&mut self, handle: TextResource) -> bool {
        self.is_empty(handle)
    }
    fn height(
        &mut self,
        handle: TextResource,
        font: i32,
        width: i16,
        lines: u8,
    ) -> Result<i16, Error> {
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
