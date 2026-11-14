//! Classify tag spelling once, then branch on its semantic role.
#[derive(Clone, Copy, PartialEq, Eq)]
pub(super) enum Tag {
    Other,
    Html,
    Body,
    Span,
    Link,
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,
    Paragraph,
    ListItem,
    Div,
    Break,
    Quote,
    UnorderedList,
    OrderedList,
    Pre,
    Figure,
    Table,
    Row,
    Cell,
    HeaderCell,
    Image,
    Ruby,
    RubyText,
    Rule,
    Underline,
    Strike,
    Bold,
    Italic,
    Sup,
    Sub,
    Small,
    Big,
}
impl Tag {
    pub fn parse(name: &[u8]) -> Self {
        const TAGS: &[(&[u8], Tag)] = &[
            (b"html", Tag::Html),
            (b"body", Tag::Body),
            (b"span", Tag::Span),
            (b"a", Tag::Link),
            (b"h1", Tag::H1),
            (b"h2", Tag::H2),
            (b"h3", Tag::H3),
            (b"h4", Tag::H4),
            (b"h5", Tag::H5),
            (b"h6", Tag::H6),
            (b"p", Tag::Paragraph),
            (b"li", Tag::ListItem),
            (b"div", Tag::Div),
            (b"br", Tag::Break),
            (b"blockquote", Tag::Quote),
            (b"ul", Tag::UnorderedList),
            (b"ol", Tag::OrderedList),
            (b"pre", Tag::Pre),
            (b"figure", Tag::Figure),
            (b"table", Tag::Table),
            (b"tr", Tag::Row),
            (b"td", Tag::Cell),
            (b"th", Tag::HeaderCell),
            (b"img", Tag::Image),
            (b"image", Tag::Image),
            (b"ruby", Tag::Ruby),
            (b"rt", Tag::RubyText),
            (b"hr", Tag::Rule),
            (b"u", Tag::Underline),
            (b"ins", Tag::Underline),
            (b"del", Tag::Strike),
            (b"s", Tag::Strike),
            (b"strike", Tag::Strike),
            (b"b", Tag::Bold),
            (b"strong", Tag::Bold),
            (b"i", Tag::Italic),
            (b"em", Tag::Italic),
            (b"sup", Tag::Sup),
            (b"sub", Tag::Sub),
            (b"small", Tag::Small),
            (b"big", Tag::Big),
        ];
        TAGS.iter()
            .find_map(|(spelling, tag)| (*spelling == name).then_some(*tag))
            .unwrap_or(Self::Other)
    }
    pub fn heading(self) -> u8 {
        match self {
            Self::H1 => 1,
            Self::H2 => 2,
            Self::H3 => 3,
            Self::H4 => 4,
            Self::H5 => 5,
            Self::H6 => 6,
            _ => 0,
        }
    }
    pub fn block(self) -> bool {
        matches!(
            self,
            Self::H1
                | Self::H2
                | Self::H3
                | Self::H4
                | Self::H5
                | Self::H6
                | Self::Paragraph
                | Self::ListItem
                | Self::Div
                | Self::Break
                | Self::Quote
                | Self::UnorderedList
                | Self::OrderedList
                | Self::Pre
                | Self::Figure
        )
    }
    pub fn structural(self) -> bool {
        matches!(
            self,
            Self::Table | Self::Row | Self::Cell | Self::HeaderCell
        )
    }
}
