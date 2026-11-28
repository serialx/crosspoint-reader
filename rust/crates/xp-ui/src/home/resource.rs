//! Home resources are typed until they cross the shared scene callback boundary.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Text {
    Title(u8),
    Author(u8),
    Menu(u8),
    WrappedTitle { book: u8, line: u8 },
    ClippedAuthor,
    ClippedMenu(u8),
    NoOpenBook,
    StartReading,
    Continue,
    Progress,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Resource {
    Text(Text),
    Header,
    Footer,
    Restore,
    Snapshot,
    MaskCorners,
    Ribbon,
    Placeholder,
    CoverIcon,
    Cover(u8),
    MenuIcon(u8),
    GridCover(u8),
    GridIcon(u8),
}

impl Resource {
    /// The encoding is private to Home's shared scene adapter; it owns no resource.
    pub fn encode(self) -> u32 {
        match self {
            Self::Header => 0x8000_0001,
            Self::Footer => 0x8000_0002,
            Self::Restore => 0x8000_0003,
            Self::Snapshot => 0x8000_0004,
            Self::MaskCorners => 0x8000_0005,
            Self::Ribbon => 0x8000_0006,
            Self::Placeholder => 0x8000_0007,
            Self::CoverIcon => 0x8000_0008,
            Self::Cover(i) => 0x1000 + u32::from(i),
            Self::MenuIcon(i) => 0x5000 + u32::from(i),
            Self::GridCover(i) => 0x6000 + u32::from(i),
            Self::GridIcon(i) => 0x7000 + u32::from(i),
            Self::Text(text) => match text {
                Text::Title(i) => 0x2000 + u32::from(i),
                Text::Author(i) => 0x3000 + u32::from(i),
                Text::Menu(i) => 0x4000 + u32::from(i),
                Text::WrappedTitle { book, line } => {
                    0x10000 + 4 * u32::from(book) + u32::from(line)
                }
                Text::ClippedAuthor => 0x11000,
                Text::ClippedMenu(i) => 0x12000 + u32::from(i),
                Text::NoOpenBook => 0x8000_0010,
                Text::StartReading => 0x8000_0011,
                Text::Continue => 0x8000_0012,
                Text::Progress => 0x8000_0013,
            },
        }
    }

    pub fn decode(value: u32) -> Option<Self> {
        Some(match value {
            0x8000_0001 => Self::Header,
            0x8000_0002 => Self::Footer,
            0x8000_0003 => Self::Restore,
            0x8000_0004 => Self::Snapshot,
            0x8000_0005 => Self::MaskCorners,
            0x8000_0006 => Self::Ribbon,
            0x8000_0007 => Self::Placeholder,
            0x8000_0008 => Self::CoverIcon,
            0x1000..=0x1006 => Self::Cover((value - 0x1000) as u8),
            0x5000..=0x5005 => Self::MenuIcon((value - 0x5000) as u8),
            0x6000..=0x6006 => Self::GridCover((value - 0x6000) as u8),
            0x7000..=0x7005 => Self::GridIcon((value - 0x7000) as u8),
            _ => Self::Text(match value {
                0x2000..=0x2002 => Text::Title((value - 0x2000) as u8),
                0x3000..=0x3002 => Text::Author((value - 0x3000) as u8),
                0x4000..=0x4005 => Text::Menu((value - 0x4000) as u8),
                0x10000..=0x1000b => Text::WrappedTitle {
                    book: ((value - 0x10000) / 4) as u8,
                    line: ((value - 0x10000) % 4) as u8,
                },
                0x11000 => Text::ClippedAuthor,
                0x12000..=0x12005 => Text::ClippedMenu((value - 0x12000) as u8),
                0x8000_0010 => Text::NoOpenBook,
                0x8000_0011 => Text::StartReading,
                0x8000_0012 => Text::Continue,
                0x8000_0013 => Text::Progress,
                _ => return None,
            }),
        })
    }
}
