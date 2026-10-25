//! Streaming XML events with original byte locations and fallible storage.
extern crate alloc;

mod decode;
mod names;
use alloc::vec::Vec;
use core::ops::Range;
use decode::{xml_character, Decoded, Encoding};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Kind {
    Start = 1,
    End,
    Text,
    Comment,
    Pi,
    CdataStart,
    CdataEnd,
    Entity,
    Error,
    NeedInput,
    Done,
    Stopped,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Field {
    pub range: Range<usize>,
}

pub struct Event<'a> {
    pub kind: Kind,
    pub data: &'a [u8],
    pub fields: &'a [Field],
    pub byte: u64,
    pub line: u64,
    pub error: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Failure {
    code: u32,
    byte: u64,
    line: u64,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Phase {
    Prolog,
    Content,
    Epilog,
    Failed,
    Finished,
    Stopped,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Token {
    Unknown,
    Text,
    Entity,
    Markup,
    Cdata,
}

struct DefaultAttribute {
    element: Vec<u8>,
    name: Vec<u8>,
    value: Option<Vec<u8>>,
    cdata: bool,
}

pub struct Parser {
    input: Vec<u8>,
    pos: usize,
    scan: usize,
    offset: u64,
    line: u64,
    previous_cr: bool,
    encoding: Encoding,
    detected: bool,
    final_input: bool,
    phase: Phase,
    token: Vec<u8>,
    token_kind: Token,
    quote: u8,
    subset_depth: usize,
    cdata: bool,
    doctype_seen: bool,
    declaration_seen: bool,
    external_subset: bool,
    standalone: bool,
    elements: Vec<u8>,
    entities: Vec<(Vec<u8>, bool)>,
    internal_subset: bool,
    value: Vec<u8>,
    declaration_offset: u64,
    defaults: Vec<DefaultAttribute>,
    output: Vec<u8>,
    fields: Vec<Field>,
    kind: Kind,
    event_byte: u64,
    event_line: u64,
    failure: Option<Failure>,
    empty_end: bool,
    partial_token_bytes: usize,
    deferred: bool,
}

fn append(target: &mut Vec<u8>, bytes: &[u8]) -> Result<(), u32> {
    target.try_reserve(bytes.len()).map_err(|_| 1u32)?;
    target.extend_from_slice(bytes);
    Ok(())
}
fn copy(bytes: &[u8]) -> Result<Vec<u8>, u32> {
    let mut result = Vec::new();
    append(&mut result, bytes)?;
    Ok(result)
}
fn space(b: u8) -> bool {
    matches!(b, b' ' | b'\r' | b'\n' | b'\t')
}
fn name_start(c: char) -> bool {
    names::name(c, true)
}
fn name_continue(c: char) -> bool {
    names::name(c, false)
}
fn name_end(bytes: &[u8], start: usize) -> Option<usize> {
    let text = core::str::from_utf8(bytes.get(start..)?).ok()?;
    let mut chars = text.char_indices();
    let (_, first) = chars.next()?;
    if !name_start(first) {
        return None;
    }
    for (pos, c) in chars {
        if !name_continue(c) {
            return Some(start + pos);
        }
    }
    Some(bytes.len())
}
fn skip_space(bytes: &[u8], mut pos: usize) -> usize {
    while bytes.get(pos).is_some_and(|&c| space(c)) {
        pos += 1;
    }
    pos
}

impl Default for Parser {
    fn default() -> Self {
        Self::new()
    }
}
impl Parser {
    pub const fn new() -> Self {
        Self {
            input: Vec::new(),
            pos: 0,
            scan: 0,
            offset: 0,
            line: 1,
            previous_cr: false,
            encoding: Encoding::Utf8,
            detected: false,
            final_input: false,
            phase: Phase::Prolog,
            token: Vec::new(),
            token_kind: Token::Unknown,
            quote: 0,
            subset_depth: 0,
            cdata: false,
            doctype_seen: false,
            declaration_seen: false,
            external_subset: false,
            standalone: false,
            elements: Vec::new(),
            entities: Vec::new(),
            internal_subset: false,
            value: Vec::new(),
            declaration_offset: 0,
            defaults: Vec::new(),
            output: Vec::new(),
            fields: Vec::new(),
            kind: Kind::NeedInput,
            event_byte: 0,
            event_line: 1,
            failure: None,
            empty_end: false,
            partial_token_bytes: 0,
            deferred: false,
        }
    }

    /// Drain next() to NeedInput before feeding. At most 1024 new bytes are copied.
    /// Only the unconsumed token survives compaction; consumed is zero after stop/error.
    pub fn feed(&mut self, bytes: &[u8], final_input: bool) -> usize {
        if matches!(self.phase, Phase::Stopped | Phase::Failed | Phase::Finished)
            || self.final_input
        {
            return 0;
        }
        if self.pos != 0 {
            self.input.copy_within(self.pos.., 0);
            self.input.truncate(self.input.len() - self.pos);
            self.scan -= self.pos;
            self.pos = 0;
        }
        let count = bytes.len().min(1024);
        if append(&mut self.input, &bytes[..count]).is_err() {
            self.fail(1, 0);
            return 0;
        }
        self.final_input = final_input && count == bytes.len();
        // Match Expat's partial-token retry boundary without rescanning the token.
        self.deferred =
            !self.final_input && self.input.len() < self.partial_token_bytes.saturating_mul(2);
        count
    }

    /// Latch an allocation failure in an adapter's event storage.
    pub fn allocation_failed(&mut self) {
        self.fail(1, 0);
    }

    pub fn stop(&mut self) {
        if self.phase != Phase::Failed {
            self.phase = Phase::Stopped;
        }
    }

    fn event(&self) -> Event<'_> {
        let (kind, error, byte, line) = if let Some(f) = self.failure {
            (Kind::Error, f.code, f.byte, f.line)
        } else {
            (self.kind, 0, self.event_byte, self.event_line)
        };
        Event {
            kind,
            data: &self.output,
            fields: &self.fields,
            byte,
            line,
            error,
        }
    }

    fn fail(&mut self, code: u32, token_pos: usize) {
        let prefix = self
            .token
            .get(..token_pos.min(self.token.len()))
            .unwrap_or(&[]);
        let width = core::str::from_utf8(prefix)
            .map(|s| self.encoding.original_width(s))
            .unwrap_or(0);
        let mut line = self.line;
        let mut cr = self.previous_cr;
        for &c in prefix {
            if c == b'\r' || (c == b'\n' && !cr) {
                line += 1;
            }
            cr = c == b'\r';
        }
        self.failure = Some(Failure {
            code,
            byte: self.offset
                + width as u64
                + (token_pos.saturating_sub(self.token.len())
                    * if matches!(self.encoding, Encoding::Utf16Le | Encoding::Utf16Be) {
                        2
                    } else {
                        1
                    }) as u64,
            line,
        });
        self.phase = Phase::Failed;
    }

    fn field(&mut self, value: &[u8]) -> Result<(), u32> {
        self.fields.try_reserve(1).map_err(|_| 1u32)?;
        let start = self.output.len();
        append(&mut self.output, value)?;
        self.fields.push(Field {
            range: start..self.output.len(),
        });
        append(&mut self.output, &[0])?;
        Ok(())
    }

    fn consume(&mut self) {
        let count = self.scan - self.pos;
        self.offset += count as u64;
        self.pos = self.scan;
        for &c in &self.token {
            if c == b'\r' || (c == b'\n' && !self.previous_cr) {
                self.line += 1;
            }
            self.previous_cr = c == b'\r';
        }
        self.token.clear();
        self.token_kind = Token::Unknown;
        self.quote = 0;
        self.subset_depth = 0;
    }

    fn detect(&mut self) -> bool {
        let data = &self.input[self.pos..];
        if data.is_empty() {
            return false;
        }
        if !self.final_input
            && ((data.len() == 1 && matches!(data[0], 0 | b'<' | 0xef | 0xfe | 0xff))
                || (data.starts_with(&[0xef, 0xbb]) && data.len() < 3))
        {
            return false;
        }
        let skip;
        if data.starts_with(&[0xff, 0xfe]) {
            self.encoding = Encoding::Utf16Le;
            skip = 2;
        } else if data.starts_with(&[0xfe, 0xff]) {
            self.encoding = Encoding::Utf16Be;
            skip = 2;
        } else if data.starts_with(&[0xef, 0xbb, 0xbf]) {
            skip = 3;
        } else {
            skip = 0;
            if data.starts_with(&[0, b'<']) {
                self.encoding = Encoding::Utf16Be;
            } else if data.starts_with(&[b'<', 0]) {
                self.encoding = Encoding::Utf16Le;
            }
        }
        self.pos += skip;
        self.scan = self.pos;
        self.offset += skip as u64;
        self.declaration_offset = self.offset;
        self.detected = true;
        true
    }

    fn scan_token(&mut self) -> Result<bool, (u32, usize)> {
        loop {
            let (c, width) = match self.encoding.decode(&self.input[self.scan..]) {
                Decoded::Character(c, width) => (c, width),
                Decoded::Invalid => return Err((4, self.token.len())),
                Decoded::Incomplete => {
                    if self.final_input && self.scan < self.input.len() {
                        return Err((6, self.token.len()));
                    }
                    if self.phase == Phase::Prolog
                        && !self.internal_subset
                        && self.token_kind == Token::Text
                        && self.token.iter().any(|&b| !space(b))
                        && !self.final_input
                    {
                        return Ok(false);
                    }
                    if matches!(self.token_kind, Token::Text | Token::Cdata)
                        && !self.token.is_empty()
                    {
                        return Ok(true);
                    }
                    return Ok(false);
                }
            };
            if !xml_character(c) {
                return Err((4, self.token.len()));
            }
            if self.token_kind == Token::Unknown {
                self.token_kind = if self.cdata {
                    Token::Cdata
                } else {
                    match c {
                        '<' => Token::Markup,
                        '&' => Token::Entity,
                        '%' if self.internal_subset => Token::Entity,
                        _ => Token::Text,
                    }
                };
            }
            if self.token_kind == Token::Text && matches!(c, '<' | '&') {
                return Ok(true);
            }
            // A final CR or closing bracket can be the start of a multi-character delimiter.
            if matches!(self.token_kind, Token::Text | Token::Cdata)
                && self.phase == Phase::Content
                && c == '\r'
                && !self.final_input
            {
                let tail = &self.input[self.scan + width..];
                if tail.is_empty() {
                    return Ok(matches!(self.token_kind, Token::Text | Token::Cdata)
                        && !self.token.is_empty());
                }
            }
            // Expat reports literal line endings separately from surrounding text.
            // XPath whitespace anchors depend on these callback boundaries.
            if matches!(self.token_kind, Token::Text | Token::Cdata)
                && self.phase == Phase::Content
                && matches!(c, '\r' | '\n')
            {
                if !self.token.is_empty() {
                    return Ok(true);
                }
                append(&mut self.token, &[c as u8]).map_err(|e| (e, 0))?;
                self.scan += width;
                if c == '\r' {
                    if let Decoded::Character('\n', second) =
                        self.encoding.decode(&self.input[self.scan..])
                    {
                        append(&mut self.token, b"\n").map_err(|e| (e, 0))?;
                        self.scan += second;
                    }
                }
                return Ok(true);
            }
            if matches!(self.token_kind, Token::Text | Token::Cdata) && c == ']' {
                if self.token_kind == Token::Cdata && !self.token.is_empty() {
                    return Ok(true);
                }
                match self.encoding.decode(&self.input[self.scan + width..]) {
                    Decoded::Character(']', second) => match self
                        .encoding
                        .decode(&self.input[self.scan + width + second..])
                    {
                        Decoded::Character('>', third) => {
                            if self.token_kind == Token::Text {
                                return Err((4, self.token.len() + 2));
                            }
                            if !self.token.is_empty() {
                                return Ok(true);
                            }
                            append(&mut self.token, b"]]>").map_err(|e| (e, 0))?;
                            self.scan += width + second + third;
                            return Ok(true);
                        }
                        Decoded::Incomplete
                            if !self.final_input || self.token_kind == Token::Cdata =>
                        {
                            return Ok(!self.token.is_empty())
                        }
                        _ => {}
                    },
                    Decoded::Incomplete if !self.final_input || self.token_kind == Token::Cdata => {
                        return Ok(!self.token.is_empty())
                    }
                    _ => {}
                }
            }
            let mut utf8 = [0; 4];
            append(&mut self.token, c.encode_utf8(&mut utf8).as_bytes()).map_err(|e| (e, 0))?;
            self.scan += width;
            match self.token_kind {
                Token::Unknown => unreachable!(),
                Token::Text => {
                    if self.token.ends_with(b"]]>") {
                        return Err((4, self.token.len() - 1));
                    }
                }
                Token::Entity => {
                    if c == ';' {
                        return Ok(true);
                    }
                    if c == '<' || space(c as u8) {
                        return Err((4, self.token.len() - c.len_utf8()));
                    }
                }
                Token::Cdata => {
                    if self.token.ends_with(b"]]>") {
                        return Ok(true);
                    }
                }
                Token::Markup => {
                    if self.token.starts_with(b"<!--") {
                        if self.token.ends_with(b"-->") {
                            return Ok(true);
                        }
                        if self.token.len() >= 7
                            && self.token[self.token.len() - 3..].starts_with(b"--")
                        {
                            return Err((4, self.token.len() - 1));
                        }
                    } else if self.token.starts_with(b"<?") {
                        if self.token.ends_with(b"?>") {
                            return Ok(true);
                        }
                    } else if self.token == b"<![CDATA[" {
                        return Ok(true);
                    } else if self.quote != 0 {
                        if c as u32 == u32::from(self.quote) {
                            self.quote = 0;
                        }
                    } else if matches!(c, '\'' | '"') {
                        self.quote = c as u8;
                    } else if self.token.starts_with(b"<!DOCTYPE") {
                        match c {
                            '[' => {
                                self.subset_depth += 1;
                                return Ok(true);
                            }
                            ']' => self.subset_depth = self.subset_depth.saturating_sub(1),
                            '>' if self.subset_depth == 0 => return Ok(true),
                            _ => {}
                        }
                    } else if c == '>' {
                        return Ok(true);
                    }
                }
            }
        }
    }

    // Events borrow the parser, which the Iterator trait cannot express.
    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> Event<'_> {
        if self.phase == Phase::Failed {
            return self.event();
        }
        if self.phase == Phase::Stopped {
            self.kind = Kind::Stopped;
            return self.event();
        }
        if self.phase == Phase::Finished {
            self.kind = Kind::Done;
            return self.event();
        }
        if self.deferred {
            self.kind = Kind::NeedInput;
            return self.event();
        }
        if self.empty_end {
            self.empty_end = false;
            self.kind = Kind::End;
            self.fields.truncate(1);
            self.event_byte = self.offset;
            self.event_line = self.line;
            if self.elements.is_empty() {
                self.phase = Phase::Epilog;
            }
            return self.event();
        }
        self.output.clear();
        self.fields.clear();
        loop {
            self.event_byte = self.offset;
            self.event_line = self.line;
            if !self.detected && !self.detect() {
                if self.final_input {
                    self.fail(3, 0);
                } else {
                    self.kind = Kind::NeedInput;
                    self.partial_token_bytes = if self.pos == 0 { self.input.len() } else { 0 };
                }
                return self.event();
            }
            self.event_byte = self.offset;
            match self.scan_token() {
                Err((code, pos)) => {
                    self.fail(code, pos);
                    return self.event();
                }
                Ok(false) => {
                    if self.final_input {
                        if !self.token.is_empty() {
                            self.fail(if self.cdata { 20 } else { 5 }, 0);
                        } else if self.phase != Phase::Epilog {
                            self.fail(if self.cdata { 20 } else { 3 }, 0);
                        } else {
                            self.phase = Phase::Finished;
                            self.kind = Kind::Done;
                        }
                    } else {
                        self.kind = Kind::NeedInput;
                        self.partial_token_bytes = if self.pos == 0 { self.input.len() } else { 0 };
                        if matches!(self.phase, Phase::Prolog | Phase::Epilog)
                            && self.pos == self.input.len()
                        {
                            self.previous_cr = false;
                        }
                    }
                    return self.event();
                }
                Ok(true) => {}
            }
            let token = core::mem::take(&mut self.token);
            let result = self.interpret(&token);
            self.token = token;
            match result {
                Err((code, pos)) => {
                    self.fail(code, pos);
                    return self.event();
                }
                Ok(emit) => {
                    self.consume();
                    if emit {
                        return self.event();
                    }
                }
            }
        }
    }

    fn normalized(
        &self,
        bytes: &[u8],
        attribute: bool,
        tokenized: bool,
        target: &mut Vec<u8>,
    ) -> Result<(), u32> {
        let mut pos = 0;
        while pos < bytes.len() {
            let byte = bytes[pos];
            if byte == b'&' && attribute {
                let end = bytes[pos..].iter().position(|&b| b == b';').ok_or(5u32)? + pos + 1;
                let before = target.len();
                self.reference(&bytes[pos..end], target, true)?;
                if tokenized
                    && target.len() == before + 1
                    && target.last() == Some(&b' ')
                    && (before == 0 || target[before - 1] == b' ')
                {
                    target.pop();
                }
                pos = end;
                continue;
            }
            let value = if byte == b'\r' {
                if bytes.get(pos + 1) == Some(&b'\n') {
                    pos += 1;
                }
                if attribute {
                    b' '
                } else {
                    b'\n'
                }
            } else if attribute && matches!(byte, b'\n' | b'\t') {
                b' '
            } else {
                byte
            };
            if !tokenized || value != b' ' || (!target.is_empty() && target.last() != Some(&b' ')) {
                append(target, &[value])?;
            }
            pos += 1;
        }
        if tokenized && target.last() == Some(&b' ') {
            target.pop();
        }
        Ok(())
    }

    fn numeric_value(digits: &[u8], base: u32) -> Result<char, u32> {
        let mut value = Some(0u32);
        for &c in digits {
            let digit = char::from(c).to_digit(base).ok_or(4u32)?;
            value = value
                .and_then(|v| v.checked_mul(base))
                .and_then(|v| v.checked_add(digit));
        }
        value
            .and_then(char::from_u32)
            .filter(|&c| xml_character(c))
            .ok_or(14)
    }
    fn reference_syntax(token: &[u8]) -> Result<(), (u32, usize)> {
        if token.len() < 3 {
            return Err((4, 1));
        }
        let name = &token[1..token.len() - 1];
        if name.starts_with(b"#") {
            let start = if name.get(1) == Some(&b'x') { 3 } else { 2 };
            let base = if start == 3 { 16 } else { 10 };
            if start == token.len() - 1 {
                return Err((4, start));
            }
            for (i, &c) in token[start..token.len() - 1].iter().enumerate() {
                if char::from(c).to_digit(base).is_none() {
                    return Err((4, start + i));
                }
            }
        } else {
            let end = name_end(name, 0).ok_or((4, 1))?;
            if end != name.len() {
                return Err((4, end + 1));
            }
        }
        Ok(())
    }
    fn check_attribute_refs(&self, bytes: &[u8]) -> Result<(), (u32, usize)> {
        let mut pos = 0;
        while pos < bytes.len() {
            if bytes[pos] != b'&' {
                pos += 1;
                continue;
            }
            let end = bytes[pos..]
                .iter()
                .position(|&b| b == b';')
                .map(|p| p + pos + 1)
                .ok_or((4, bytes.len()))?;
            let token = &bytes[pos..end];
            Self::reference_syntax(token).map_err(|(c, p)| (c, pos + p))?;
            if token.starts_with(b"&#") {
                let start = if token.get(2) == Some(&b'x') { 3 } else { 2 };
                Self::numeric_value(
                    &token[start..token.len() - 1],
                    if start == 3 { 16 } else { 10 },
                )
                .map_err(|c| (c, pos))?;
            }
            pos = end;
        }
        Ok(())
    }

    fn reference(&self, token: &[u8], output: &mut Vec<u8>, attribute: bool) -> Result<bool, u32> {
        let name = &token[1..token.len() - 1];
        let predefined = match name {
            b"amp" => Some(b'&'),
            b"lt" => Some(b'<'),
            b"gt" => Some(b'>'),
            b"quot" => Some(b'"'),
            b"apos" => Some(b'\''),
            _ => None,
        };
        if let Some(c) = predefined {
            append(output, &[c])?;
            return Ok(true);
        }
        if name.starts_with(b"#") {
            let (digits, base) = if name.get(1) == Some(&b'x') {
                (&name[2..], 16)
            } else {
                (&name[1..], 10)
            };
            if digits.is_empty() {
                return Err(4);
            }
            let c = Self::numeric_value(digits, base)?;
            let mut encoded = [0; 4];
            append(output, c.encode_utf8(&mut encoded).as_bytes())?;
            return Ok(true);
        }
        if name_end(name, 0) != Some(name.len()) {
            return Err(4);
        }
        if let Some((_, unparsed)) = self
            .entities
            .iter()
            .find(|(entity, _)| entity.as_slice() == name)
        {
            if *unparsed {
                return Err(15);
            }
            append(output, token)?;
            return Ok(true);
        }
        if !self.external_subset || self.standalone {
            return Err(11);
        }
        if !attribute {
            append(output, name)?;
        }
        Ok(false)
    }

    #[inline(never)]
    fn interpret(&mut self, token: &[u8]) -> Result<bool, (u32, usize)> {
        match self.token_kind {
            Token::Unknown => Ok(false),
            Token::Text => {
                if self.internal_subset {
                    let start = skip_space(token, 0);
                    if start == token.len() {
                        return Ok(false);
                    }
                    if token.get(start..).is_some_and(|s| s.starts_with(b"]>"))
                        && skip_space(token, start + 2) == token.len()
                    {
                        self.internal_subset = false;
                        return Ok(false);
                    }
                    return Err((2, start));
                }
                if self.phase != Phase::Content {
                    if let Some(pos) = token.iter().position(|&b| !space(b)) {
                        return Err((
                            if self.phase == Phase::Epilog { 9 } else { 4 },
                            if self.phase == Phase::Prolog {
                                token.len()
                            } else {
                                pos
                            },
                        ));
                    }
                    return Ok(false);
                }
                let mut output = core::mem::take(&mut self.output);
                let result = self.normalized(token, false, false, &mut output);
                self.output = output;
                result.map_err(|code| (code, 0))?;
                self.fields.try_reserve(1).map_err(|_| (1, 0))?;
                self.fields.push(Field {
                    range: 0..self.output.len(),
                });
                self.kind = Kind::Text;
                Ok(true)
            }
            Token::Entity => {
                if self.internal_subset && token.starts_with(b"%") {
                    return Ok(false);
                }
                if self.phase != Phase::Content {
                    return Err((if self.phase == Phase::Epilog { 9 } else { 2 }, 0));
                }
                Self::reference_syntax(token)?;
                let mut output = core::mem::take(&mut self.output);
                let result = self.reference(token, &mut output, false);
                self.output = output;
                let text = result.map_err(|code| {
                    let mut pos = 0;
                    if code == 4 && token.starts_with(b"&#") {
                        let start = if token.get(2) == Some(&b'x') { 3 } else { 2 };
                        let base = if start == 3 { 16 } else { 10 };
                        pos = token[start..token.len() - 1]
                            .iter()
                            .position(|&c| char::from(c).to_digit(base).is_none())
                            .map(|p| p + start)
                            .unwrap_or(start);
                    }
                    (code, pos)
                })?;
                self.fields.try_reserve(1).map_err(|_| (1, 0))?;
                self.fields.push(Field {
                    range: 0..self.output.len(),
                });
                self.kind = if text { Kind::Text } else { Kind::Entity };
                Ok(true)
            }
            Token::Cdata => {
                if token == b"]]>" {
                    self.cdata = false;
                    self.kind = Kind::CdataEnd;
                    return Ok(true);
                }
                let mut output = core::mem::take(&mut self.output);
                let result = self.normalized(token, false, false, &mut output);
                self.output = output;
                result.map_err(|code| (code, 0))?;
                self.fields.try_reserve(1).map_err(|_| (1, 0))?;
                self.fields.push(Field {
                    range: 0..self.output.len(),
                });
                self.kind = Kind::Text;
                Ok(true)
            }
            Token::Markup => self.markup(token),
        }
    }

    #[inline(never)]
    fn markup(&mut self, token: &[u8]) -> Result<bool, (u32, usize)> {
        if token.starts_with(b"<!--") {
            let mut output = core::mem::take(&mut self.output);
            let result = self.normalized(&token[4..token.len() - 3], false, false, &mut output);
            self.output = output;
            result.map_err(|c| (c, 0))?;
            self.fields.try_reserve(1).map_err(|_| (1, 0))?;
            self.fields.push(Field {
                range: 0..self.output.len(),
            });
            self.kind = Kind::Comment;
            return Ok(true);
        }
        if token.starts_with(b"<?") {
            return self.pi(token);
        }
        if self.internal_subset && token.starts_with(b"<!") {
            self.dtd_declaration(token)?;
            return Ok(false);
        }
        if token.starts_with(b"<!DOCTYPE") {
            if self.phase != Phase::Prolog || self.doctype_seen {
                return Err((2, 0));
            }
            self.doctype_seen = true;
            self.dtd(token)?;
            return Ok(false);
        }
        if token == b"<![CDATA[" {
            if self.phase != Phase::Content {
                return Err((2, 0));
            }
            self.cdata = true;
            self.kind = Kind::CdataStart;
            return Ok(true);
        }
        if token.starts_with(b"<!") {
            return Err((2, 0));
        }
        if token.starts_with(b"</") {
            return self.end_tag(token);
        }
        if self.phase == Phase::Epilog {
            return Err((9, 0));
        }
        self.start_tag(token)
    }

    fn start_tag(&mut self, token: &[u8]) -> Result<bool, (u32, usize)> {
        let end = name_end(token, 1).ok_or((4, 1))?;
        let name = &token[1..end];
        self.field(name).map_err(|c| (c, 0))?;
        let mut pos = end;
        let mut empty = false;
        loop {
            let before = pos;
            pos = skip_space(token, pos);
            if token.get(pos) == Some(&b'>') {
                if pos + 1 != token.len() {
                    return Err((4, pos));
                }
                break;
            }
            if token.get(pos..) == Some(b"/>".as_slice()) {
                empty = true;
                break;
            }
            if token.get(pos) == Some(&b'/') {
                return Err((4, pos + 1));
            }
            if before == pos {
                return Err((4, pos));
            }
            let start = pos;
            pos = name_end(token, pos).ok_or((4, pos))?;
            let attribute = &token[start..pos];
            for field in self.fields.iter().skip(1).step_by(2) {
                if &self.output[field.range.clone()] == attribute {
                    return Err((8, start));
                }
            }
            pos = skip_space(token, pos);
            if token.get(pos) != Some(&b'=') {
                return Err((4, pos));
            }
            pos = skip_space(token, pos + 1);
            let quote = *token.get(pos).ok_or((5, 0))?;
            if !matches!(quote, b'\'' | b'"') {
                return Err((4, pos));
            }
            pos += 1;
            let value_start = pos;
            while token.get(pos).is_some_and(|&b| b != quote) {
                if token[pos] == b'<' {
                    return Err((4, pos));
                }
                pos += 1;
            }
            if pos >= token.len() {
                return Err((5, 0));
            }
            let tokenized = self
                .defaults
                .iter()
                .any(|d| d.element == name && d.name == attribute && !d.cdata);
            self.field(attribute).map_err(|c| (c, 0))?;
            self.check_attribute_refs(&token[value_start..pos])
                .map_err(|(c, p)| (c, value_start + p))?;
            let mut value = core::mem::take(&mut self.value);
            value.clear();
            self.normalized(&token[value_start..pos], true, tokenized, &mut value)
                .map_err(|c| (c, 0))?;
            self.field(&value).map_err(|c| (c, 0))?;
            self.value = value;
            pos += 1;
        }
        let defaults = core::mem::take(&mut self.defaults);
        let result = (|| {
            for d in &defaults {
                if d.element != name || d.value.is_none() {
                    continue;
                }
                if self
                    .fields
                    .iter()
                    .skip(1)
                    .step_by(2)
                    .any(|f| self.output[f.range.clone()] == d.name)
                {
                    continue;
                }
                self.field(&d.name).map_err(|c| (c, 0))?;
                self.field(d.value.as_deref().unwrap_or(&[]))
                    .map_err(|c| (c, 0))?;
            }
            Ok(())
        })();
        self.defaults = defaults;
        result?;
        if empty {
            self.empty_end = true;
        } else {
            append(&mut self.elements, name).map_err(|c| (c, 0))?;
            append(&mut self.elements, &[0]).map_err(|c| (c, 0))?;
        }
        self.phase = Phase::Content;
        self.kind = Kind::Start;
        Ok(true)
    }

    fn end_tag(&mut self, token: &[u8]) -> Result<bool, (u32, usize)> {
        let end = name_end(token, 2).ok_or((4, 2))?;
        let name = &token[2..end];
        if token.get(skip_space(token, end)..) != Some(b">".as_slice()) {
            return Err((4, skip_space(token, end)));
        }
        if self.elements.is_empty() {
            return Err((if self.phase == Phase::Epilog { 9 } else { 4 }, 0));
        }
        let last = self.elements.len() - 1;
        let start = self.elements[..last]
            .iter()
            .rposition(|&b| b == 0)
            .map(|p| p + 1)
            .unwrap_or(0);
        if &self.elements[start..last] != name {
            return Err((7, 2));
        }
        self.elements.truncate(start);
        self.field(name).map_err(|c| (c, 0))?;
        self.kind = Kind::End;
        if self.elements.is_empty() {
            self.phase = Phase::Epilog;
        }
        Ok(true)
    }

    fn pi(&mut self, token: &[u8]) -> Result<bool, (u32, usize)> {
        let end = name_end(token, 2).ok_or((4, 2))?;
        let name = &token[2..end];
        if name.eq_ignore_ascii_case(b"xml") {
            if name != b"xml" {
                return Err((4, end));
            }
            if self.phase != Phase::Prolog
                || self.declaration_seen
                || self.offset != self.declaration_offset
            {
                return Err((17, 0));
            }
            self.declaration_seen = true;
            let mut pos = end;
            let mut index = 0;
            let mut seen_encoding = false;
            let mut seen_standalone = false;
            while skip_space(token, pos) < token.len() - 2 {
                let old = pos;
                pos = skip_space(token, pos);
                if old == pos {
                    return Err((30, pos));
                }
                let start = pos;
                pos = name_end(token, pos).ok_or((30, pos))?;
                let key = &token[start..pos];
                pos = skip_space(token, pos);
                if token.get(pos) != Some(&b'=') {
                    return Err((30, pos));
                }
                pos = skip_space(token, pos + 1);
                let quote = *token.get(pos).ok_or((30, pos))?;
                if !matches!(quote, b'\'' | b'"') {
                    return Err((30, pos));
                }
                pos += 1;
                let value_start = pos;
                while token.get(pos).is_some_and(|&b| b != quote) {
                    pos += 1;
                }
                let value = &token[value_start..pos];
                match key {
                    b"version" if index == 0 => {
                        if value.is_empty()
                            || !value.iter().all(|b| {
                                b.is_ascii_alphanumeric() || matches!(b, b'.' | b'_' | b'-' | b':')
                            })
                        {
                            return Err((30, value_start));
                        }
                    }
                    b"encoding" if index > 0 && !seen_encoding && !seen_standalone => {
                        seen_encoding = true;
                        let encoding = if value.eq_ignore_ascii_case(b"UTF-8") {
                            Encoding::Utf8
                        } else if value.eq_ignore_ascii_case(b"ISO-8859-1") {
                            Encoding::Latin1
                        } else if value.eq_ignore_ascii_case(b"US-ASCII") {
                            Encoding::Ascii
                        } else if value.eq_ignore_ascii_case(b"UTF-16") {
                            if !matches!(self.encoding, Encoding::Utf16Le | Encoding::Utf16Be) {
                                return Err((19, value_start));
                            }
                            self.encoding
                        } else {
                            return Err((18, value_start));
                        };
                        if matches!(self.encoding, Encoding::Utf16Le | Encoding::Utf16Be)
                            && !matches!(encoding, Encoding::Utf16Le | Encoding::Utf16Be)
                        {
                            return Err((19, value_start));
                        }
                        self.encoding = encoding;
                    }
                    b"standalone" if index > 0 && !seen_standalone => {
                        seen_standalone = true;
                        self.standalone = match value {
                            b"yes" => true,
                            b"no" => false,
                            _ => return Err((30, value_start)),
                        };
                    }
                    _ => return Err((30, start)),
                }
                pos += 1;
                index += 1;
            }
            if index == 0 {
                return Err((30, end));
            }
            return Ok(false);
        }
        let pos = skip_space(token, end);
        if pos == end && pos != token.len() - 2 {
            return Err((4, pos));
        }
        self.field(name).map_err(|c| (c, 0))?;
        let mut data = core::mem::take(&mut self.value);
        data.clear();
        self.normalized(&token[pos..token.len() - 2], false, false, &mut data)
            .map_err(|c| (c, 0))?;
        self.field(&data).map_err(|c| (c, 0))?;
        self.value = data;
        self.kind = Kind::Pi;
        Ok(true)
    }

    fn quoted_literal<'a>(token: &'a [u8], pos: &mut usize) -> Result<&'a [u8], (u32, usize)> {
        let quote = *token.get(*pos).ok_or((2, *pos))?;
        if !matches!(quote, b'\'' | b'"') {
            return Err((2, *pos));
        }
        *pos += 1;
        let start = *pos;
        while token.get(*pos).is_some_and(|&b| b != quote) {
            *pos += 1;
        }
        if *pos >= token.len() {
            return Err((5, start - 1));
        }
        let value = &token[start..*pos];
        *pos += 1;
        Ok(value)
    }
    fn external_id(
        token: &[u8],
        pos: &mut usize,
        system_required: bool,
    ) -> Result<(), (u32, usize)> {
        let public = token.get(*pos..).is_some_and(|s| s.starts_with(b"PUBLIC"));
        if !public && !token.get(*pos..).is_some_and(|s| s.starts_with(b"SYSTEM")) {
            return Err((2, *pos));
        }
        *pos += 6;
        let old = *pos;
        *pos = skip_space(token, *pos);
        if old == *pos {
            return Err((2, *pos));
        }
        let start = *pos;
        let value = Self::quoted_literal(token, pos)?;
        if public {
            for (i, &b) in value.iter().enumerate() {
                if !b.is_ascii_alphanumeric() && !b" \r\n-'()+,./:=?;!*#@$_%".contains(&b) {
                    return Err((32, start + 1 + i));
                }
            }
            let old = *pos;
            *pos = skip_space(token, *pos);
            if token.get(*pos).is_some_and(|b| matches!(b, b'\'' | b'"')) {
                if old == *pos {
                    return Err((2, *pos));
                }
                Self::quoted_literal(token, pos)?;
            } else if system_required {
                return Err((2, *pos));
            }
        }
        Ok(())
    }
    fn dtd(&mut self, token: &[u8]) -> Result<(), (u32, usize)> {
        let mut pos = skip_space(token, 9);
        if pos == 9 {
            return Err((4, pos));
        }
        pos = name_end(token, pos).ok_or((2, pos))?;
        let old = pos;
        pos = skip_space(token, pos);
        if token
            .get(pos..)
            .is_some_and(|s| s.starts_with(b"SYSTEM") || s.starts_with(b"PUBLIC"))
        {
            if old == pos {
                return Err((2, pos));
            }
            Self::external_id(token, &mut pos, true)?;
            self.external_subset = true;
            pos = skip_space(token, pos);
        }
        match token.get(pos..) {
            Some(b">") => Ok(()),
            Some(b"[") => {
                self.internal_subset = true;
                Ok(())
            }
            _ => Err((2, pos)),
        }
    }
    fn dtd_declaration(&mut self, token: &[u8]) -> Result<(), (u32, usize)> {
        if token.starts_with(b"<!ATTLIST") {
            return self.attlist(token);
        }
        if token.starts_with(b"<!ENTITY") {
            let mut pos = skip_space(token, 8);
            if pos == 8 {
                return Err((2, pos));
            }
            let parameter = token.get(pos) == Some(&b'%');
            if parameter {
                pos = skip_space(token, pos + 1);
            }
            let end = name_end(token, pos).ok_or((2, pos))?;
            let name = &token[pos..end];
            pos = skip_space(token, end);
            if pos == end {
                return Err((2, pos));
            }
            let external = !token.get(pos).is_some_and(|b| matches!(b, b'\'' | b'"'));
            if external {
                Self::external_id(token, &mut pos, true)?;
            } else {
                Self::quoted_literal(token, &mut pos)?;
            }
            pos = skip_space(token, pos);
            let unparsed = token.get(pos..).is_some_and(|s| s.starts_with(b"NDATA"));
            if unparsed {
                if parameter || !external {
                    return Err((2, pos));
                }
                pos = skip_space(token, pos + 5);
                pos = name_end(token, pos).ok_or((2, pos))?;
                pos = skip_space(token, pos);
            }
            if token.get(pos..) != Some(b">".as_slice()) {
                return Err((2, pos));
            }
            if !parameter && !self.entities.iter().any(|(e, _)| e == name) {
                self.entities.try_reserve(1).map_err(|_| (1, 0))?;
                self.entities
                    .push((copy(name).map_err(|c| (c, 0))?, unparsed));
            }
            return Ok(());
        }
        if token.starts_with(b"<!NOTATION") {
            let mut pos = skip_space(token, 10);
            let end = name_end(token, pos).ok_or((2, pos))?;
            pos = skip_space(token, end);
            Self::external_id(token, &mut pos, false)?;
            pos = skip_space(token, pos);
            if token.get(pos..) != Some(b">".as_slice()) {
                return Err((2, pos));
            }
            return Ok(());
        }
        if token.starts_with(b"<!ELEMENT") {
            let mut pos = skip_space(token, 9);
            pos = name_end(token, pos).ok_or((2, pos))?;
            pos = skip_space(token, pos);
            if token.get(pos..).is_some_and(|s| s.starts_with(b"EMPTY")) {
                pos += 5;
            } else if token.get(pos..).is_some_and(|s| s.starts_with(b"ANY")) {
                pos += 3;
            } else if token.get(pos) == Some(&b'(') {
                pos = self.content_model(token, pos)?;
            } else {
                return Err((2, pos));
            }
            pos = skip_space(token, pos);
            if token.get(pos..) != Some(b">".as_slice()) {
                return Err((2, pos));
            }
            return Ok(());
        }
        Err((2, 0))
    }

    fn content_model(&mut self, token: &[u8], mut pos: usize) -> Result<usize, (u32, usize)> {
        let inside = skip_space(token, pos + 1);
        if token
            .get(inside..)
            .is_some_and(|s| s.starts_with(b"#PCDATA"))
        {
            pos = skip_space(token, inside + 7);
            let mut names = false;
            while token.get(pos) == Some(&b'|') {
                names = true;
                pos = skip_space(token, pos + 1);
                pos = name_end(token, pos).ok_or((2, pos))?;
                pos = skip_space(token, pos);
            }
            if token.get(pos) != Some(&b')') {
                return Err((2, pos));
            }
            pos += 1;
            if token.get(pos) == Some(&b'*') {
                pos += 1;
            } else if names {
                return Err((2, pos));
            }
            return Ok(pos);
        }
        self.value.clear();
        append(&mut self.value, &[0]).map_err(|c| (c, 0))?;
        pos += 1;
        let mut need_item = true;
        loop {
            pos = skip_space(token, pos);
            if need_item {
                if token.get(pos) == Some(&b'(') {
                    append(&mut self.value, &[0]).map_err(|c| (c, 0))?;
                    pos += 1;
                    continue;
                }
                pos = name_end(token, pos).ok_or((2, pos))?;
                need_item = false;
                if token
                    .get(pos)
                    .is_some_and(|b| matches!(b, b'?' | b'*' | b'+'))
                {
                    pos += 1;
                }
                continue;
            }
            match token.get(pos) {
                Some(b')') => {
                    self.value.pop();
                    pos += 1;
                    if token
                        .get(pos)
                        .is_some_and(|b| matches!(b, b'?' | b'*' | b'+'))
                    {
                        pos += 1;
                    }
                    if self.value.is_empty() {
                        return Ok(pos);
                    }
                }
                Some(separator @ (b',' | b'|')) => {
                    let last = self.value.last_mut().ok_or((2, pos))?;
                    if *last != 0 && *last != *separator {
                        return Err((2, pos));
                    }
                    *last = *separator;
                    need_item = true;
                    pos += 1;
                }
                _ => return Err((2, pos)),
            }
        }
    }

    fn enumeration(token: &[u8], mut pos: usize, notation: bool) -> Result<usize, (u32, usize)> {
        pos += 1;
        loop {
            pos = skip_space(token, pos);
            if notation {
                pos = name_end(token, pos).ok_or((2, pos))?;
            } else {
                let text = core::str::from_utf8(&token[pos..]).map_err(|_| (4, pos))?;
                let start = pos;
                for c in text.chars() {
                    if !name_continue(c) {
                        break;
                    }
                    pos += c.len_utf8();
                }
                if pos == start {
                    return Err((2, pos));
                }
            }
            pos = skip_space(token, pos);
            match token.get(pos) {
                Some(b')') => return Ok(pos + 1),
                Some(b'|') => pos += 1,
                _ => return Err((2, pos)),
            }
        }
    }

    fn attlist(&mut self, token: &[u8]) -> Result<(), (u32, usize)> {
        let mut pos = skip_space(token, 9);
        let end = name_end(token, pos).ok_or((2, pos))?;
        let element = &token[pos..end];
        pos = end;
        loop {
            pos = skip_space(token, pos);
            if token.get(pos) == Some(&b'>') {
                return Ok(());
            }
            let end = name_end(token, pos).ok_or((2, pos))?;
            let name = &token[pos..end];
            pos = skip_space(token, end);
            let type_start = pos;
            let cdata = token.get(pos..).is_some_and(|s| s.starts_with(b"CDATA"));
            let notation = token.get(pos..).is_some_and(|s| s.starts_with(b"NOTATION"));
            if notation {
                pos = skip_space(token, pos + 8);
            }
            if token.get(pos) == Some(&b'(') {
                pos = Self::enumeration(token, pos, notation)?;
            } else {
                let end = name_end(token, pos).ok_or((2, pos))?;
                if ![
                    b"CDATA".as_slice(),
                    b"ID",
                    b"IDREF",
                    b"IDREFS",
                    b"ENTITY",
                    b"ENTITIES",
                    b"NMTOKEN",
                    b"NMTOKENS",
                ]
                .contains(&&token[pos..end])
                {
                    return Err((2, type_start));
                }
                pos = end;
            }
            pos = skip_space(token, pos);
            let value;
            if token
                .get(pos..)
                .is_some_and(|s| s.starts_with(b"#REQUIRED"))
            {
                pos += 9;
                value = None;
            } else if token.get(pos..).is_some_and(|s| s.starts_with(b"#IMPLIED")) {
                pos += 8;
                value = None;
            } else {
                if token.get(pos..).is_some_and(|s| s.starts_with(b"#FIXED")) {
                    pos = skip_space(token, pos + 6);
                }
                let quote = *token.get(pos).ok_or((2, pos))?;
                if !matches!(quote, b'\'' | b'"') {
                    return Err((2, pos));
                }
                pos += 1;
                let start = pos;
                while token.get(pos).is_some_and(|&b| b != quote) {
                    pos += 1;
                }
                if pos >= token.len() {
                    return Err((5, start));
                }
                let mut decoded = Vec::new();
                self.normalized(&token[start..pos], true, !cdata, &mut decoded)
                    .map_err(|c| (c, start - 1))?;
                value = Some(decoded);
                pos += 1;
            }
            if !self
                .defaults
                .iter()
                .any(|d| d.element == element && d.name == name)
            {
                self.defaults.try_reserve(1).map_err(|_| (1, 0))?;
                self.defaults.push(DefaultAttribute {
                    element: copy(element).map_err(|c| (c, 0))?,
                    name: copy(name).map_err(|c| (c, 0))?,
                    value,
                    cdata,
                });
            }
        }
    }
}

#[cfg(test)]
mod tests;
