use super::alloc::vec::Vec;
use super::{number, Error, Record};

pub fn encode<N: AsRef<[u8]>>(records: &[Record<N>]) -> Result<Vec<u8>, Error> {
    // Six bytes per input byte covers every escape; 192 covers keys, punctuation
    // and five numeric values. Both this bound and the exact count are checked.
    let mut bound = 16usize;
    for row in records {
        if row.name.as_ref().len() > 128 {
            return Err(Error::NameTooLong);
        }
        let strings = row
            .xpath
            .len()
            .checked_add(row.summary.len())
            .and_then(|n| n.checked_add(row.name.as_ref().len()))
            .and_then(|n| n.checked_mul(6))
            .and_then(|n| n.checked_add(192))
            .ok_or(Error::Size)?;
        bound = bound.checked_add(strings).ok_or(Error::Size)?;
    }
    let mut length = 0usize;
    visit(records, &mut |bytes| {
        length = length.checked_add(bytes.len()).ok_or(Error::Size)?;
        Ok(())
    })?;
    if length > bound {
        return Err(Error::Size);
    }
    let mut result = Vec::new();
    result
        .try_reserve_exact(length)
        .map_err(|_| Error::Allocation)?;
    visit(records, &mut |bytes| {
        // The first pass covered these same immutable rows. Never grow here.
        if bytes.len() > length - result.len() {
            return Err(Error::Size);
        }
        result.extend_from_slice(bytes);
        Ok(())
    })?;
    Ok(result)
}

fn visit<N: AsRef<[u8]>>(
    records: &[Record<N>],
    emit: &mut impl FnMut(&[u8]) -> Result<(), Error>,
) -> Result<(), Error> {
    emit(b"{\"bookmarks\":[")?;
    let mut buffer = [0u8; 32];
    for (index, row) in records.iter().enumerate() {
        if index != 0 {
            emit(b",")?;
        }
        emit(b"{\"xpath\":")?;
        string(&row.xpath, emit)?;
        emit(b",\"percentage\":")?;
        emit(number::float(row.percentage, &mut buffer))?;
        emit(b",\"summary\":")?;
        string(&row.summary, emit)?;
        if !row.name.as_ref().is_empty() {
            emit(b",\"name\":")?;
            string(row.name.as_ref(), emit)?;
        }
        emit(b",\"si\":")?;
        emit(number::integer(u32::from(row.spine), &mut buffer))?;
        emit(b",\"pc\":")?;
        emit(number::integer(u32::from(row.page_count), &mut buffer))?;
        emit(b",\"pp\":")?;
        emit(number::integer(u32::from(row.page), &mut buffer))?;
        if let Some(offset) = row.visible_offset {
            emit(b",\"vo\":")?;
            emit(number::integer(offset, &mut buffer))?;
        }
        emit(b"}")?;
    }
    emit(b"]}")
}

fn string(bytes: &[u8], emit: &mut impl FnMut(&[u8]) -> Result<(), Error>) -> Result<(), Error> {
    emit(b"\"")?;
    for byte in bytes {
        match byte {
            b'"' => emit(b"\\\"")?,
            b'\\' => emit(b"\\\\")?,
            b'\n' => emit(b"\\n")?,
            b'\r' => emit(b"\\r")?,
            b'\t' => emit(b"\\t")?,
            8 => emit(b"\\b")?,
            12 => emit(b"\\f")?,
            0 => emit(b"\\u0000")?,
            _ => emit(core::slice::from_ref(byte))?,
        }
    }
    emit(b"\"")
}
