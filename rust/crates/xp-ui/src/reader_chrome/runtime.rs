//! List preparation borrows owner scratch; painting and publication stay separate.
use super::{
    hits::Layout,
    list::{self, Resources, Screen, Theme},
    ListModel,
};
use crate::scene::{Arena, Command, Error, Refresh};

pub struct ListRequest<'a> {
    pub snapshot: &'a ListModel,
    pub kind: Screen,
    pub theme: &'a Theme,
    pub count: u16,
    pub window_capacity: u16,
    pub generation: u32,
}

impl ListRequest<'_> {
    /// Scratch outputs are unpublished and may be partial on failure. A capacity
    /// retry starts from this same snapshot, with replacement command storage.
    pub fn prepare(
        &self,
        commands: &mut [Command],
        resources: &mut dyn Resources,
        next: &mut ListModel,
        hits: &mut Layout,
    ) -> Result<u16, Error> {
        *next = *self.snapshot;
        for pass in 0..9 {
            *hits = Layout::new(
                if self.kind == Screen::Panel { 6 } else { 1 },
                self.generation,
            );
            let mut arena = Arena::with_hit_sink(commands, hits)?;
            next.list = list::view(
                &mut arena,
                next.list,
                self.kind,
                *self.theme,
                resources,
                self.count,
                self.window_capacity,
                self.generation,
                next.revealed,
            )?;
            if next.list.follow != 0
                && pass < 8
                && matches!(self.kind, Screen::Chapters | Screen::Clippings)
            {
                continue;
            }
            let scene = arena.finish(Refresh::Fast, self.generation)?;
            return Ok(scene.commands.len() as u16);
        }
        Err(Error::Invalid)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RowSlot {
    Label,
    Subtitle,
    Value,
    Icon,
}
impl RowSlot {
    pub fn encode(self, index: u16) -> u32 {
        u32::from(index) * 4
            + 1
            + match self {
                Self::Label => 0,
                Self::Subtitle => 1,
                Self::Value => 2,
                Self::Icon => 3,
            }
    }
    pub fn decode(handle: u32) -> Option<(u16, Self)> {
        let offset = handle.checked_sub(1)?;
        let index = u16::try_from(offset / 4).ok()?;
        let slot = match offset % 4 {
            0 => Self::Label,
            1 => Self::Subtitle,
            2 => Self::Value,
            _ => Self::Icon,
        };
        Some((index, slot))
    }
}

/// Sampled geometry/fonts/metrics are fresh facts, not a settings cache.
pub struct ThemeFacts {
    pub sampled: Theme,
    pub body_line_height: i32,
    pub help_font: i32,
    pub bookmark_help_font: i32,
}
impl ThemeFacts {
    pub fn compose(self, bookmarks: bool) -> Result<Theme, Error> {
        if !(0..=32767).contains(&self.body_line_height) {
            return Err(Error::Invalid);
        }
        let mut theme = self.sampled;
        theme.help_font = if bookmarks {
            self.bookmark_help_font
        } else {
            self.help_font
        };
        if theme.touch != 0 {
            theme.row_min = 0;
        }
        theme.space_sm = (self.body_line_height / 6).max(4) as i16;
        theme.space_md = 8;
        theme.space_lg = 16;
        Ok(theme)
    }
}
