//! KOReader ancestry and visible-text anchors over the shared XML event stream.
extern crate alloc;
use crate::xml::{Kind, Parser};
use alloc::vec::Vec;

type Result<T> = core::result::Result<T, u32>;
fn append(out: &mut Vec<u8>, bytes: &[u8]) -> Result<()> {
    out.try_reserve(bytes.len()).map_err(|_| 1u32)?;
    out.extend_from_slice(bytes);
    Ok(())
}
fn copy(bytes: &[u8]) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    append(&mut out, bytes)?;
    Ok(out)
}
fn push<T>(out: &mut Vec<T>, value: T) -> Result<()> {
    out.try_reserve(1).map_err(|_| 1u32)?;
    out.push(value);
    Ok(())
}
fn number(out: &mut Vec<u8>, mut value: usize) -> Result<()> {
    let mut digits = [0u8; 20];
    let mut at = digits.len();
    loop {
        at -= 1;
        digits[at] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            break;
        }
    }
    append(out, &digits[at..])
}
fn whitespace(b: u8) -> bool {
    matches!(b, b' ' | b'\r' | b'\n' | b'\t')
}
pub fn invisible(name: &[u8]) -> bool {
    [b"head".as_slice(), b"style", b"script", b"title", b"rp"]
        .iter()
        .any(|s| name.eq_ignore_ascii_case(s))
}
#[derive(Clone, Copy)]
pub enum Mode {
    Count,
    Paragraph(u16),
    Offset { target: usize, inclusive: bool },
}
enum Name {
    Inline { bytes: [u8; 15], len: u8 },
    Long(Vec<u8>),
}
impl Name {
    fn new(name: &[u8]) -> Result<Self> {
        if name.len() <= 15 {
            let mut bytes = [0; 15];
            bytes[..name.len()].copy_from_slice(name);
            Ok(Self::Inline {
                bytes,
                len: name.len() as u8,
            })
        } else {
            Ok(Self::Long(copy(name)?))
        }
    }
    fn bytes(&self) -> &[u8] {
        match self {
            Self::Inline { bytes, len } => &bytes[..*len as usize],
            Self::Long(v) => v,
        }
    }
}
struct Child {
    name: Name,
    count: i32,
}
struct Frame {
    name: Name,
    index: i32,
    text_node: i32,
    children: Vec<Child>,
}
impl Frame {
    fn new(name: Name, index: i32) -> Self {
        Self {
            name,
            index,
            text_node: 0,
            children: Vec::new(),
        }
    }
}
struct State {
    mode: Mode,
    spine: i32,
    inside: bool,
    depth: i32,
    body_depth: i32,
    hidden: u16,
    paragraphs: i32,
    visible: usize,
    last_text_end: usize,
    pending: bool,
    text_start: usize,
    frames: Vec<Frame>,
    pub output: Vec<u8>,
    stopped: bool,
}
impl State {
    fn new(mode: Mode, spine: i32) -> Self {
        Self {
            mode,
            spine,
            inside: false,
            depth: 0,
            body_depth: -1,
            hidden: 0,
            paragraphs: 0,
            visible: 0,
            last_text_end: 0,
            pending: true,
            text_start: 0,
            frames: Vec::new(),
            output: Vec::new(),
            stopped: false,
        }
    }
    fn path(&mut self, text_node: i32, offset: usize) -> Result<()> {
        append(&mut self.output, b"/body/DocFragment[")?;
        number(&mut self.output, self.spine.wrapping_add(1) as usize)?;
        append(&mut self.output, b"]/body")?;
        for f in self.frames.iter().skip(1) {
            append(&mut self.output, b"/")?;
            append(&mut self.output, f.name.bytes())?;
            append(&mut self.output, b"[")?;
            number(&mut self.output, f.index as usize)?;
            append(&mut self.output, b"]")?;
        }
        if text_node > 0 {
            append(&mut self.output, b"/text()[")?;
            number(&mut self.output, text_node as usize)?;
            append(&mut self.output, b"].")?;
            number(&mut self.output, offset)?;
        }
        self.stopped = true;
        Ok(())
    }
    fn start(&mut self, name: &[u8]) -> Result<()> {
        if !self.inside {
            if name == b"body" {
                self.inside = true;
                self.body_depth = self.depth;
                if !matches!(self.mode, Mode::Count) {
                    push(&mut self.frames, Frame::new(Name::new(&[])?, 0))?;
                }
            }
            self.depth = self.depth.wrapping_add(1);
            return Ok(());
        }
        if !matches!(self.mode, Mode::Count) {
            self.pending = true;
            let parent = self.frames.last_mut().ok_or(1u32)?;
            let index = if let Some(child) = parent
                .children
                .iter_mut()
                .find(|child| child.name.bytes() == name)
            {
                child.count = child.count.wrapping_add(1);
                child.count
            } else {
                push(
                    &mut parent.children,
                    Child {
                        name: Name::new(name)?,
                        count: 1,
                    },
                )?;
                1
            };
            push(&mut self.frames, Frame::new(Name::new(name)?, index))?;
        }
        match self.mode {
            Mode::Paragraph(target) => {
                if name == b"p" || name == b"li" {
                    self.paragraphs = self.paragraphs.wrapping_add(1);
                }
                if self.paragraphs == i32::from(target) {
                    self.path(0, 0)?;
                }
            }
            _ => {
                if self.hidden > 0 || invisible(name) {
                    self.hidden = self.hidden.wrapping_add(1);
                }
            }
        }
        self.depth = self.depth.wrapping_add(1);
        Ok(())
    }
    fn end(&mut self, name: &[u8]) {
        self.depth = self.depth.wrapping_sub(1);
        if !self.inside {
            return;
        }
        if self.depth == self.body_depth && name == b"body" {
            self.inside = false;
            self.frames.clear();
            self.hidden = 0;
            return;
        }
        if self.hidden > 0 {
            self.hidden -= 1;
        }
        self.pending = true;
        self.frames.pop();
    }
    fn text(&mut self, data: &[u8]) -> Result<()> {
        if !self.inside
            || self.hidden > 0
            || self.stopped
            || data.is_empty()
            || matches!(self.mode, Mode::Paragraph(_))
        {
            return Ok(());
        }
        let mut pos = 0;
        let mut count = 0;
        while pos < data.len() {
            if crate::utf8::next(data, &mut pos).is_none() {
                break;
            }
            count += 1;
        }
        let blank = data.iter().all(|&b| whitespace(b));
        if matches!(self.mode, Mode::Count) {
            self.visible = self.visible.saturating_add(count);
            if !blank {
                self.last_text_end = self
                    .visible
                    .saturating_sub(data.iter().rev().take_while(|&&b| whitespace(b)).count());
            }
            return Ok(());
        }
        if count == 0 {
            return Ok(());
        }
        if self.pending {
            if let Some(f) = self.frames.last_mut() {
                f.text_node = f.text_node.wrapping_add(1);
            }
            self.text_start = self.visible;
            self.pending = false;
        }
        let next = self.visible.saturating_add(count);
        if let Mode::Offset { target, inclusive } = self.mode {
            if (if inclusive {
                target <= next
            } else {
                target < next
            }) && !blank
            {
                let offset = self
                    .visible
                    .saturating_sub(self.text_start)
                    .saturating_add(target.saturating_sub(self.visible));
                let node = self.frames.last().map_or(0, |f| f.text_node);
                self.path(node, offset)?;
                return Ok(());
            }
        }
        self.visible = next;
        Ok(())
    }
}
pub struct XPath {
    parser: Parser,
    state: State,
    error: u32,
}
impl XPath {
    pub fn new(mode: Mode, spine: i32) -> Self {
        Self {
            parser: Parser::new(),
            state: State::new(mode, spine),
            error: 0,
        }
    }
    pub fn output(&self) -> &[u8] {
        &self.state.output
    }
    pub fn count(&self) -> usize {
        self.state.last_text_end
    }
    pub fn feed(&mut self, bytes: &[u8], final_input: bool) -> u32 {
        if self.error != 0 || self.state.stopped {
            return self.error;
        }
        match self.parse(bytes, final_input) {
            Ok(()) => 0,
            Err(code) => {
                self.error = code;
                code
            }
        }
    }
    fn parse(&mut self, bytes: &[u8], final_input: bool) -> Result<()> {
        let mut offset = 0;
        loop {
            let consumed = self.parser.feed(&bytes[offset..], final_input);
            offset += consumed;
            loop {
                let event = self.parser.next();
                let raw = event
                    .fields
                    .first()
                    .and_then(|f| event.data.get(f.range.clone()))
                    .unwrap_or(&[]);
                let name = raw.rsplit(|&b| b == b':').next().unwrap_or(raw);
                match event.kind {
                    Kind::Start => self.state.start(name)?,
                    Kind::End => self.state.end(name),
                    Kind::Text => self.state.text(raw)?,
                    Kind::Comment | Kind::Pi | Kind::CdataStart | Kind::CdataEnd => {
                        if self.state.inside && self.state.hidden == 0 {
                            self.state.pending = true;
                        }
                    }
                    Kind::Error => return Err(event.error),
                    Kind::Done => return Ok(()),
                    Kind::NeedInput => break,
                    _ => {}
                }
                if self.state.stopped {
                    return if final_input { Err(35) } else { Ok(()) };
                }
            }
            if offset == bytes.len() {
                return Ok(());
            }
            if consumed == 0 {
                return Err(1);
            }
        }
    }
}
