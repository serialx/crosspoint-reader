// ArduinoJson-compatible token and escape rules; no allocated JSON tree.
// Unicode behavior follows ArduinoJson 7.4.2 Utf16 and JsonDeserializer.
// Copyright (c) 2014-2025, Benoit BLANCHON. See ARDUINOJSON-LICENSE.txt.
use super::{parse_number, Error};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Object,
    Array,
    String,
    Number,
    Null,
    Other,
}

#[derive(Clone, Copy)]
pub struct Value<'a> {
    pub kind: Kind,
    pub bytes: &'a [u8],
}

pub struct Parser<'a> {
    bytes: &'a [u8],
    index: usize,
}
impl<'a> Parser<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, index: 0 }
    }
    pub fn current(&self) -> u8 {
        self.bytes.get(self.index).copied().unwrap_or(0)
    }
    pub fn eat(&mut self, byte: u8) -> bool {
        if self.current() != byte {
            return false;
        }
        self.index += 1;
        true
    }
    pub fn spaces(&mut self) {
        while matches!(self.current(), b' ' | b'\n' | b'\r' | b'\t') {
            self.index += 1;
        }
    }
    fn required(&mut self) -> Result<(), Error> {
        self.spaces();
        if self.current() == 0 {
            Err(Error::IncompleteInput)
        } else {
            Ok(())
        }
    }
    fn quoted(&mut self) -> Result<&'a [u8], Error> {
        let quote = self.current();
        self.index += 1;
        let start = self.index;
        loop {
            match self.current() {
                0 => return Err(Error::IncompleteInput),
                b'\\' => {
                    self.index += 1;
                    match self.current() {
                        0 => return Err(Error::IncompleteInput),
                        b'u' => {
                            self.index += 1;
                            for _ in 0..4 {
                                let digit = self.current();
                                if digit == 0 {
                                    return Err(Error::IncompleteInput);
                                }
                                if !digit.is_ascii_hexdigit() {
                                    return Err(Error::InvalidInput);
                                }
                                self.index += 1;
                            }
                        }
                        b'/' | b'\'' | b'"' | b'\\' | b'b' | b'f' | b'n' | b'r' | b't' => {
                            self.index += 1
                        }
                        _ => return Err(Error::InvalidInput),
                    }
                }
                c if c == quote => {
                    let value = &self.bytes[start..self.index];
                    self.index += 1;
                    string(value, |_| {})?;
                    return Ok(value);
                }
                _ => self.index += 1,
            }
        }
    }
    pub fn key(&mut self) -> Result<Value<'a>, Error> {
        if matches!(self.current(), b'"' | b'\'') {
            Ok(Value {
                kind: Kind::String,
                bytes: self.quoted()?,
            })
        } else {
            let start = self.index;
            while matches!(self.current(), b'0'..=b'9' | b'_'..=b'z' | b'A'..=b'Z') {
                self.index += 1;
            }
            if start == self.index {
                return Err(Error::InvalidInput);
            }
            // Unquoted keys cannot contain escapes, so the string walker also applies.
            Ok(Value {
                kind: Kind::String,
                bytes: &self.bytes[start..self.index],
            })
        }
    }
    pub fn value(&mut self, depth: u8) -> Result<Value<'a>, Error> {
        self.required()?;
        let start = self.index;
        let kind = match self.current() {
            b'[' | b'{' => {
                if depth == 10 {
                    return Err(Error::TooDeep);
                }
                let object = self.current() == b'{';
                let close = if object { b'}' } else { b']' };
                self.index += 1;
                self.required()?;
                if !self.eat(close) {
                    loop {
                        if object {
                            self.key()?;
                            self.required()?;
                            if !self.eat(b':') {
                                return Err(Error::InvalidInput);
                            }
                        }
                        self.value(depth + 1)?;
                        self.required()?;
                        if self.eat(close) {
                            break;
                        }
                        if !self.eat(b',') {
                            return Err(Error::InvalidInput);
                        }
                        self.required()?;
                    }
                }
                if object {
                    Kind::Object
                } else {
                    Kind::Array
                }
            }
            b'"' | b'\'' => {
                return Ok(Value {
                    kind: Kind::String,
                    bytes: self.quoted()?,
                })
            }
            b't' | b'f' | b'n' => {
                let keyword: &[u8] = match self.current() {
                    b't' => b"true",
                    b'f' => b"false",
                    _ => b"null",
                };
                for byte in keyword {
                    if !self.eat(*byte) {
                        return Err(if self.current() == 0 {
                            Error::IncompleteInput
                        } else {
                            Error::InvalidInput
                        });
                    }
                }
                if keyword == b"null" {
                    Kind::Null
                } else {
                    Kind::Other
                }
            }
            _ => {
                while self.index - start < 63
                    && matches!(
                        self.current(),
                        b'0'..=b'9' | b'+' | b'-' | b'.' | b'e' | b'E'
                    )
                {
                    self.index += 1;
                }
                parse_number::parse(&self.bytes[start..self.index])?;
                Kind::Number
            }
        };
        Ok(Value {
            kind,
            bytes: &self.bytes[start..self.index],
        })
    }
}

pub fn member<'a>(object: Value<'a>, wanted: &[u8]) -> Result<Option<Value<'a>>, Error> {
    if object.kind != Kind::Object {
        return Ok(None);
    }
    let mut parser = Parser::new(&object.bytes[1..]);
    let mut found = None;
    loop {
        parser.spaces();
        if parser.eat(b'}') {
            return Ok(found);
        }
        let key = parser.key()?;
        let mut index = 0;
        let mut equal = true;
        string(key.bytes, |byte| {
            equal &= wanted.get(index) == Some(&byte);
            index += 1;
        })?;
        parser.required()?;
        if !parser.eat(b':') {
            return Err(Error::InvalidInput);
        }
        let value = parser.value(0)?;
        if equal && index == wanted.len() {
            found = Some(value);
        }
        parser.spaces();
        if parser.eat(b'}') {
            return Ok(found);
        }
        if !parser.eat(b',') {
            return Err(Error::InvalidInput);
        }
    }
}

pub fn elements<'a>(
    array: Value<'a>,
    mut each: impl FnMut(Value<'a>) -> Result<(), Error>,
) -> Result<(), Error> {
    if array.kind != Kind::Array {
        return Ok(());
    }
    let mut parser = Parser::new(&array.bytes[1..]);
    loop {
        parser.spaces();
        if parser.eat(b']') {
            return Ok(());
        }
        each(parser.value(0)?)?;
        parser.spaces();
        if parser.eat(b']') {
            return Ok(());
        }
        if !parser.eat(b',') {
            return Err(Error::InvalidInput);
        }
    }
}

pub fn string(bytes: &[u8], mut emit: impl FnMut(u8)) -> Result<(), Error> {
    let mut index = 0;
    let mut high = 0u32;
    while index < bytes.len() {
        let byte = bytes[index];
        index += 1;
        if byte != b'\\' {
            emit(byte);
            continue;
        }
        let escape = *bytes.get(index).ok_or(Error::IncompleteInput)?;
        index += 1;
        if escape != b'u' {
            emit(match escape {
                b'/' | b'\'' | b'"' | b'\\' => escape,
                b'b' => 8,
                b'f' => 12,
                b'n' => b'\n',
                b'r' => b'\r',
                b't' => b'\t',
                _ => return Err(Error::InvalidInput),
            });
            continue;
        }
        let mut value = 0u32;
        for _ in 0..4 {
            let digit = *bytes.get(index).ok_or(Error::IncompleteInput)?;
            index += 1;
            value = (value << 4)
                | match digit {
                    b'0'..=b'9' => u32::from(digit - b'0'),
                    b'a'..=b'f' => u32::from(digit - b'a' + 10),
                    b'A'..=b'F' => u32::from(digit - b'A' + 10),
                    _ => return Err(Error::InvalidInput),
                };
        }
        if (0xd800..0xdc00).contains(&value) {
            high = value & 0x3ff;
            continue;
        }
        if (0xdc00..0xe000).contains(&value) {
            value = 0x10000 + ((high << 10) | (value & 0x3ff));
        }
        let mut encoded = [0u8; 4];
        for byte in char::from_u32(value)
            .ok_or(Error::InvalidInput)?
            .encode_utf8(&mut encoded)
            .bytes()
        {
            emit(byte);
        }
    }
    Ok(())
}
