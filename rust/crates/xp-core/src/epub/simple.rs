extern crate alloc;
use super::Element;
use super::{
    append, assign, attr, field, href, local, Event, Kind, ParserKind, RecordKind, Result,
};
use alloc::vec::Vec;

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum State {
    Start,
    Container,
    Rootfiles,
    Ncx,
    NavMap,
    NavPoint,
    NavLabel,
    NavText,
    Html,
    Body,
    NavToc,
    Ol,
    Li,
    Anchor,
}
pub(super) struct Simple {
    kind: ParserKind,
    state: State,
    base: Vec<u8>,
    pub label: Vec<u8>,
    raw: Vec<u8>,
    pub href: Vec<u8>,
    pub anchor: Vec<u8>,
    scratch: Vec<u8>,
    decoded: Vec<u8>,
    pub depth: u8,
    clear: bool,
}
impl Simple {
    pub const fn new(kind: ParserKind) -> Self {
        Self {
            kind,
            state: State::Start,
            base: Vec::new(),
            label: Vec::new(),
            raw: Vec::new(),
            href: Vec::new(),
            anchor: Vec::new(),
            scratch: Vec::new(),
            decoded: Vec::new(),
            depth: 0,
            clear: false,
        }
    }
    pub fn set_base(&mut self, base: &[u8]) -> Result<()> {
        assign(&mut self.base, base)
    }
    pub fn event(&mut self, event: &Event<'_>) -> Result<Option<RecordKind>> {
        if self.clear {
            self.label.clear();
            self.raw.clear();
            self.clear = false;
        }
        match self.kind {
            ParserKind::Container => self.container(event),
            ParserKind::Encryption => {
                if event.kind == Kind::Start
                    && Element::parse(local(field(event, 0))) == Element::EncryptionMethod
                {
                    for index in (1..event.fields.len()).step_by(2) {
                        let value = field(event, index + 1);
                        if local(field(event, index)) == b"Algorithm"
                            && (value
                                .windows(10)
                                .any(|w| w == b"aes128-cbc" || w == b"aes256-cbc"))
                        {
                            return Ok(Some(RecordKind::Protection));
                        }
                    }
                }
                Ok(None)
            }
            ParserKind::Ncx | ParserKind::Nav => self.toc(event),
        }
    }
    fn container(&mut self, event: &Event<'_>) -> Result<Option<RecordKind>> {
        let name = if matches!(event.kind, Kind::Start | Kind::End) {
            Element::parse(field(event, 0))
        } else {
            Element::Unknown
        };
        match (event.kind, self.state, name) {
            (Kind::Start, State::Start, Element::Container) => self.state = State::Container,
            (Kind::Start, State::Container, Element::Rootfiles) => self.state = State::Rootfiles,
            (Kind::Start, State::Rootfiles, Element::Rootfile) => {
                if attr(event, b"media-type", false) == Some(b"application/oebps-package+xml") {
                    if let Some(path) = attr(event, b"full-path", false) {
                        assign(&mut self.label, path)?;
                        return Ok(Some(RecordKind::ContainerPath));
                    }
                }
            }
            (Kind::End, State::Rootfiles, Element::Rootfiles) => self.state = State::Container,
            (Kind::End, State::Container, Element::Container) => self.state = State::Start,
            _ => {}
        }
        Ok(None)
    }
    fn emit(&mut self) -> Result<Option<RecordKind>> {
        if self.label.is_empty() || self.raw.is_empty() {
            return Ok(None);
        }
        href::target(
            &self.base,
            &self.raw,
            &mut self.href,
            &mut self.anchor,
            &mut self.scratch,
            &mut self.decoded,
        )?;
        self.clear = true;
        Ok(Some(RecordKind::Toc))
    }
    fn toc(&mut self, event: &Event<'_>) -> Result<Option<RecordKind>> {
        let name = if matches!(event.kind, Kind::Start | Kind::End) {
            Element::parse(field(event, 0))
        } else {
            Element::Unknown
        };
        if event.kind == Kind::Text && matches!(self.state, State::NavText | State::Anchor) {
            append(&mut self.label, field(event, 0))?;
        }
        if self.kind == ParserKind::Ncx {
            match (event.kind, self.state, name) {
                (Kind::Start, State::Start, Element::Ncx) => self.state = State::Ncx,
                (Kind::Start, State::Ncx, Element::NavMap) => self.state = State::NavMap,
                (Kind::Start, State::NavMap | State::NavPoint, Element::NavPoint) => {
                    self.state = State::NavPoint;
                    self.depth = self.depth.wrapping_add(1);
                    self.label.clear();
                    self.raw.clear();
                }
                (Kind::Start, State::NavPoint, Element::NavLabel) => self.state = State::NavLabel,
                (Kind::Start, State::NavLabel, Element::Text) => self.state = State::NavText,
                (Kind::Start, State::NavPoint, Element::Content) => {
                    if let Some(src) = attr(event, b"src", false) {
                        assign(&mut self.raw, src)?;
                    }
                }
                (Kind::End, State::NavText, Element::Text) => self.state = State::NavLabel,
                (Kind::End, State::NavLabel, Element::NavLabel) => self.state = State::NavPoint,
                (Kind::End, State::NavPoint, Element::NavPoint) => {
                    self.depth = self.depth.wrapping_sub(1);
                    if self.depth == 0 {
                        self.state = State::NavMap;
                    }
                }
                (Kind::End, State::NavPoint, Element::Content) => return self.emit(),
                _ => {}
            }
        } else if event.kind == Kind::Start {
            if name == Element::Html {
                self.state = State::Html;
            } else if self.state == State::Html && name == Element::Body {
                self.state = State::Body;
            } else if self.state >= State::Body && name == Element::Nav {
                if (1..event.fields.len()).step_by(2).any(|i| {
                    matches!(field(event, i), b"epub:type" | b"type")
                        && field(event, i + 1) == b"toc"
                }) {
                    self.state = State::NavToc;
                }
            } else if self.state >= State::NavToc {
                match (self.state, name) {
                    (_, Element::Ol) => {
                        self.depth = self.depth.wrapping_add(1);
                        self.state = State::Ol;
                    }
                    (State::Ol, Element::Li) => {
                        self.state = State::Li;
                        self.label.clear();
                        self.raw.clear();
                    }
                    (State::Li, Element::A) => {
                        self.state = State::Anchor;
                        if let Some(h) = attr(event, b"href", false) {
                            assign(&mut self.raw, h)?;
                        }
                    }
                    _ => {}
                }
            }
        } else if event.kind == Kind::End {
            match (self.state, name) {
                (State::Anchor, Element::A) => {
                    self.state = State::Li;
                    return self.emit();
                }
                (State::Li | State::Ol, Element::Li) => self.state = State::Ol,
                (s, Element::Ol) if s >= State::NavToc => {
                    self.depth = self.depth.wrapping_sub(1);
                    self.state = if self.depth == 0 {
                        State::NavToc
                    } else {
                        State::Li
                    };
                }
                (s, Element::Nav) if s >= State::NavToc => self.state = State::Body,
                _ => {}
            }
        }
        Ok(None)
    }
}
