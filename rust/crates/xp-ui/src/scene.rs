use core::mem::size_of;

pub const MAX_COMMAND_BYTES: usize = 8192;
pub const MAX_TEXT_BYTES: usize = 4096;
pub const MAX_HITS: usize = 64;
pub const MAX_SCOPES: usize = 8;

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Rect {
    pub x: i16,
    pub y: i16,
    pub w: i16,
    pub h: i16,
}
impl Rect {
    pub fn contains(self, x: i16, y: i16) -> bool {
        self.w > 0
            && self.h > 0
            && i32::from(x) >= i32::from(self.x)
            && i32::from(y) >= i32::from(self.y)
            && i32::from(x) < i32::from(self.x) + i32::from(self.w)
            && i32::from(y) < i32::from(self.y) + i32::from(self.h)
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Fill,
    Stroke,
    Line,
    Text,
    Bitmap,
    Page,
    Clip,
    Translate,
    Pop,
    Clear,
    TextHandle,
    Triangle,
    HintText,
}
impl Kind {
    pub fn from_byte(value: u8) -> Option<Self> {
        Some(match value {
            0 => Self::Fill,
            1 => Self::Stroke,
            2 => Self::Line,
            3 => Self::Text,
            4 => Self::Bitmap,
            5 => Self::Page,
            6 => Self::Clip,
            7 => Self::Translate,
            8 => Self::Pop,
            9 => Self::Clear,
            10 => Self::TextHandle,
            11 => Self::Triangle,
            12 => Self::HintText,
            _ => return None,
        })
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Refresh {
    None,
    Fast,
    Half,
    Full,
}

/// Text is a terminated slice of the scene's text arena. Handles identify
/// immutable resources in the owning activity's generation.
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Command {
    pub rect: Rect,
    pub resource: u32,
    pub generation: u32,
    pub font: i32,
    pub text_start: u16,
    pub text_len: u16,
    pub kind: u8,
    pub paint: u8,
    pub radius: u8,
    pub width: u8,
    pub style: u8,
    pub align: u8,
    pub max_lines: u8,
    pub corners: u8,
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Interaction {
    pub rect: Rect,
    pub action: u32,
    pub value: i32,
    pub generation: u32,
    pub inputs: u16,
    pub flags: u16,
}
const _: () = assert!(size_of::<Command>() == 32);
const _: () = assert!(size_of::<Interaction>() == 24);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Capacity,
    Invalid,
    StaleResource,
}

pub struct Scene<'a> {
    pub commands: &'a [Command],
    pub text: &'a [u8],
    pub interactions: &'a [Interaction],
    pub refresh: Refresh,
    pub generation: u32,
}

/// A builder owns all mutable arena loans until finish. Overflow poisons the
/// entire frame, including its interaction table.
pub struct Arena<'a> {
    commands: &'a mut [Command],
    text: &'a mut [u8],
    hits: &'a mut [Interaction],
    used: usize,
    text_used: usize,
    hit_used: usize,
    error: Option<Error>,
}
impl<'a> Arena<'a> {
    pub fn new(
        commands: &'a mut [Command],
        text: &'a mut [u8],
        hits: &'a mut [Interaction],
    ) -> Result<Self, Error> {
        if commands.len() > MAX_COMMAND_BYTES / size_of::<Command>()
            || text.len() > MAX_TEXT_BYTES
            || hits.len() > MAX_HITS
        {
            return Err(Error::Capacity);
        }
        Ok(Self {
            commands,
            text,
            hits,
            used: 0,
            text_used: 0,
            hit_used: 0,
            error: None,
        })
    }
    fn fail<T>(&mut self, error: Error) -> Result<T, Error> {
        self.error = Some(error);
        Err(error)
    }
    pub fn push(&mut self, command: Command) -> Result<(), Error> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let Some(slot) = self.commands.get_mut(self.used) else {
            return self.fail(Error::Capacity);
        };
        *slot = command;
        self.used += 1;
        Ok(())
    }
    pub fn text(&mut self, mut command: Command, text: &[u8]) -> Result<(), Error> {
        let Some(end) = self
            .text_used
            .checked_add(text.len())
            .and_then(|n| n.checked_add(1))
        else {
            return self.fail(Error::Capacity);
        };
        if end > self.text.len() || text.contains(&0) {
            return self.fail(Error::Capacity);
        }
        command.kind = Kind::Text as u8;
        command.text_start = self.text_used as u16;
        command.text_len = text.len() as u16;
        self.text[self.text_used..end - 1].copy_from_slice(text);
        self.text[end - 1] = 0;
        self.text_used = end;
        self.push(command)
    }
    pub fn hit(&mut self, interaction: Interaction) -> Result<(), Error> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let Some(slot) = self.hits.get_mut(self.hit_used) else {
            return self.fail(Error::Capacity);
        };
        *slot = interaction;
        self.hit_used += 1;
        Ok(())
    }
    pub fn finish(self, refresh: Refresh, generation: u32) -> Result<Scene<'a>, Error> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let scene = Scene {
            commands: &self.commands[..self.used],
            text: &self.text[..self.text_used],
            interactions: &self.hits[..self.hit_used],
            refresh,
            generation,
        };
        scene.validate(|_, _| true)?;
        Ok(scene)
    }
}
impl Scene<'_> {
    pub fn validate(&self, mut resource_valid: impl FnMut(u32, u32) -> bool) -> Result<(), Error> {
        if self.commands.len() > MAX_COMMAND_BYTES / size_of::<Command>()
            || self.text.len() > MAX_TEXT_BYTES
            || self.interactions.len() > MAX_HITS
        {
            return Err(Error::Capacity);
        }
        let mut depth = 0;
        for command in self.commands {
            let kind = Kind::from_byte(command.kind).ok_or(Error::Invalid)?;
            if command.paint > 3 || command.align > 2 || command.style > 3 || command.corners > 15 {
                return Err(Error::Invalid);
            }
            match kind {
                Kind::Clip | Kind::Translate => {
                    depth += 1;
                    if depth > MAX_SCOPES {
                        return Err(Error::Capacity);
                    }
                }
                Kind::Pop => {
                    if depth == 0 {
                        return Err(Error::Invalid);
                    }
                    depth -= 1;
                }
                Kind::Text => {
                    let start = usize::from(command.text_start);
                    let end = start + usize::from(command.text_len);
                    if self.text.get(end) != Some(&0)
                        || self.text.get(start..end).is_none_or(|s| s.contains(&0))
                    {
                        return Err(Error::Invalid);
                    }
                }
                Kind::Bitmap | Kind::Page | Kind::TextHandle | Kind::HintText
                    if !resource_valid(command.resource, command.generation) =>
                {
                    return Err(Error::StaleResource);
                }
                _ => {}
            }
        }
        if depth != 0
            || self
                .interactions
                .iter()
                .any(|h| h.generation != self.generation)
        {
            return Err(Error::Invalid);
        }
        Ok(())
    }
    pub fn hit(&self, x: i16, y: i16, inputs: u16) -> Option<Interaction> {
        self.interactions
            .iter()
            .rev()
            .find(|h| {
                h.generation == self.generation
                    && h.flags & 1 == 0
                    && h.inputs & inputs != 0
                    && h.rect.contains(x, y)
            })
            .copied()
    }
}

/// Native coordinates. A ninth region conservatively invalidates the panel.
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Damage {
    pub rects: [Rect; 8],
    pub count: u8,
    pub full: u8,
}
impl Damage {
    pub fn add(&mut self, rect: Rect) {
        if self.full != 0 || rect.w <= 0 || rect.h <= 0 {
            return;
        }
        let Some(slot) = self.rects.get_mut(usize::from(self.count)) else {
            self.full = 1;
            self.count = 0;
            return;
        };
        *slot = rect;
        self.count += 1;
    }
}
