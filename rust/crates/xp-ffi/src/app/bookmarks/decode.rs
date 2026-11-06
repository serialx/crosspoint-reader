use super::alloc::vec::Vec;
use super::{
    json::{self, Kind, Parser, Value},
    parse_number, Error, Record, Text,
};

// The final name storage supplies its own allocator. R2's moved keyboard block
// can implement this directly; no conversion to Vec or alternate name cache.
pub trait Name: AsRef<[u8]> + Sized {
    fn read(length: usize, fill: impl FnOnce(&mut [u8])) -> Result<Self, Error>;
}
impl Name for Text {
    fn read(length: usize, fill: impl FnOnce(&mut [u8])) -> Result<Self, Error> {
        Text::read(length, fill).map_err(|_| Error::Allocation)
    }
}
impl Name for Vec<u8> {
    fn read(length: usize, fill: impl FnOnce(&mut [u8])) -> Result<Self, Error> {
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(length)
            .map_err(|_| Error::Allocation)?;
        bytes.resize(length, 0);
        fill(&mut bytes);
        Ok(bytes)
    }
}

pub fn decode<N: Name>(bytes: &[u8]) -> Result<Vec<Record<N>>, Error> {
    let mut parser = Parser::new(bytes);
    parser.spaces();
    if parser.current() == 0 {
        return Err(Error::EmptyInput);
    }
    let root = parser.value(0)?;
    // ArduinoJson permits trailing data for collections/strings/keywords, but
    // rejects a numeric root unless its scanner reached the end of input.
    if root.kind == Kind::Number && parser.current() != 0 {
        return Err(Error::InvalidInput);
    }
    let Some(array) = json::member(root, b"bookmarks")? else {
        return Ok(Vec::new());
    };
    let mut count = 0usize;
    json::elements(array, |_| {
        count = count.checked_add(1).ok_or(Error::Size)?;
        Ok(())
    })?;
    let mut records = Vec::new();
    records
        .try_reserve_exact(count)
        .map_err(|_| Error::Allocation)?;
    json::elements(array, |object| {
        let xpath = text(json::member(object, b"xpath")?, usize::MAX)?;
        let summary = text(json::member(object, b"summary")?, usize::MAX)?;
        let name = text(json::member(object, b"name")?, 128)?;
        let percentage =
            number(json::member(object, b"percentage")?)?.map_or(0.0, |n| n.percentage());
        let spine = unsigned(json::member(object, b"si")?, u16::MAX.into())? as u16;
        let page_count = unsigned(json::member(object, b"pc")?, u16::MAX.into())? as u16;
        let page = unsigned(json::member(object, b"pp")?, u16::MAX.into())? as u16;
        let offset = json::member(object, b"vo")?;
        let visible_offset = match offset {
            None
            | Some(Value {
                kind: Kind::Null, ..
            }) => None,
            _ => Some(unsigned(offset, u32::MAX)?),
        };
        records.push(Record {
            xpath,
            summary,
            name,
            percentage,
            spine,
            page_count,
            page,
            visible_offset,
        });
        Ok(())
    })?;
    Ok(records)
}

fn text<N: Name>(value: Option<Value<'_>>, maximum: usize) -> Result<N, Error> {
    let bytes = match value {
        Some(Value {
            kind: Kind::String,
            bytes,
        }) => bytes,
        _ => return N::read(0, |_| {}),
    };
    let mut length = 0usize;
    let mut ended = false;
    json::string(bytes, |byte| {
        ended |= byte == 0;
        if !ended && length < maximum {
            length += 1;
        }
    })?;
    N::read(length, |output| {
        let mut index = 0;
        // The validated prefix ends at the first NUL, then at the name byte cap.
        json::string(bytes, |byte| {
            if index < output.len() {
                output[index] = byte;
                index += 1;
            }
        })
        .expect("validated string span");
    })
}

fn number(value: Option<Value<'_>>) -> Result<Option<parse_number::Number>, Error> {
    match value {
        Some(Value {
            kind: Kind::Number,
            bytes,
        }) => parse_number::parse(bytes).map(Some),
        _ => Ok(None),
    }
}
fn unsigned(value: Option<Value<'_>>, maximum: u32) -> Result<u32, Error> {
    Ok(number(value)?.map_or(0, |n| n.unsigned(maximum)))
}
