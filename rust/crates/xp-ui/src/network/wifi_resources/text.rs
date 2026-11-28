use super::{Error, Memory, SmallZ};

pub fn truncate<M: Memory>(
    out: &mut SmallZ<M>,
    text: &[u8],
    width: i32,
    measure: impl FnMut(&[u8]) -> i32,
) -> Result<(), Error> {
    truncate_joined(out, &[], text, width, measure)
}

/// Scratch holds the joined C text plus an ellipsis. Measurements borrow NUL-ended
/// bytes synchronously; no source loan may point into the output scratch.
pub fn truncate_joined<M: Memory>(
    out: &mut SmallZ<M>,
    prefix: &[u8],
    text: &[u8],
    width: i32,
    mut measure: impl FnMut(&[u8]) -> i32,
) -> Result<(), Error> {
    if width <= 0 {
        out.clear();
        return Ok(());
    }
    let full = prefix
        .len()
        .checked_add(text.len())
        .ok_or(Error::Capacity)?;
    let length = prefix
        .iter()
        .chain(text)
        .position(|b| *b == 0)
        .unwrap_or(full);
    if length
        .checked_add(3)
        .filter(|n| *n <= out.capacity())
        .is_none()
    {
        return Err(Error::Capacity);
    }
    let byte = |index: usize| {
        if index < prefix.len() {
            prefix[index]
        } else {
            text[index - prefix.len()]
        }
    };
    let write = |out: &mut SmallZ<M>, count: usize, suffix: &[u8]| {
        let first = count.min(prefix.len());
        out.write_joined(&[&prefix[..first], &text[..count - first], suffix])
    };
    write(out, length, &[])?;
    if measure(out.as_z()) <= width {
        return Ok(());
    }
    let mut low = 0;
    let mut high = (0..length).filter(|i| byte(*i) & 0xc0 != 0x80).count();
    let end = |characters: usize| {
        let mut index = 0;
        for _ in 0..characters {
            if index >= length {
                break;
            }
            index += 1;
            while index < length && byte(index) & 0xc0 == 0x80 {
                index += 1;
            }
        }
        index
    };
    const ELLIPSIS: &[u8] = b"\xe2\x80\xa6";
    while low < high {
        let middle = low + (high - low).div_ceil(2);
        write(out, end(middle), ELLIPSIS)?;
        if measure(out.as_z()) < width {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    write(out, end(low), ELLIPSIS)
}
