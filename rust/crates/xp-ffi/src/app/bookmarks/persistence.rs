use super::{alloc::vec::Vec, decode, encode, Error, Name, Record};

pub const DIRECTORY: &[u8] = b"/.crosspoint/bookmarks/";

// Each whole-file operation keeps the SDK transaction and its StorageLock in
// the native HAL. The read span cannot escape its synchronous callback.
pub trait Storage {
    fn exists(&self, path: &[u8]) -> bool;
    fn mkdir(&self, path: &[u8]);
    fn read_file<T>(&self, path: &[u8], consume: impl FnOnce(&[u8]) -> T) -> Option<T>;
    fn write_file_atomic(&self, path: &[u8], bytes: &[u8]) -> bool;
    fn log(&self, severity: u8, origin: &[u8], arguments: core::fmt::Arguments<'_>, tail: &[&[u8]]);
}

pub struct Prepared {
    bytes: Vec<u8>,
    count: usize,
}

pub fn path(book: &[u8]) -> Result<Vec<u8>, Error> {
    let name = book.get(1..).unwrap_or_default();
    let end = name
        .iter()
        .rposition(|byte| *byte == b'.')
        .unwrap_or(name.len());
    let length = DIRECTORY
        .len()
        .checked_add(end)
        .and_then(|n| n.checked_add(5))
        .ok_or(Error::Size)?;
    let mut path = Vec::new();
    path.try_reserve_exact(length)
        .map_err(|_| Error::Allocation)?;
    path.extend_from_slice(DIRECTORY);
    for byte in &name[..end] {
        path.push(if matches!(byte, b'/' | b'\\') {
            b'_'
        } else {
            *byte
        });
    }
    path.extend_from_slice(b".json");
    Ok(path)
}

pub fn prepare<N: AsRef<[u8]>>(records: &[Record<N>]) -> Result<Prepared, Error> {
    Ok(Prepared {
        bytes: encode(records)?,
        count: records.len(),
    })
}

// Call after releasing the record loan. Only encoded bytes cross the HAL.
pub fn write(io: &impl Storage, book: &[u8], prepared: Result<Prepared, Error>) -> bool {
    let prepared = match prepared {
        Ok(prepared) => prepared,
        Err(Error::NameTooLong) => {
            io.log(
                1,
                b"BKM",
                format_args!("Bookmark name exceeds 128 bytes"),
                &[],
            );
            return false;
        }
        Err(_) => return false,
    };
    io.log(
        0,
        b"BKM",
        format_args!("Saving {} bookmarks to file", prepared.count),
        &[],
    );
    io.mkdir(DIRECTORY);
    let Ok(path) = path(book) else {
        return false;
    };
    let path = c_string(&path);
    io.mkdir(b"/.crosspoint");
    if !io.write_file_atomic(path, &prepared.bytes) {
        io.log(1, b"PERSIST", format_args!("Failed to write "), &[path]);
        return false;
    }
    true
}

// The caller clears the old cache before reading, matching BookmarkFile::load.
pub fn read<N: Name>(io: &impl Storage, book: &[u8]) -> Option<Vec<Record<N>>> {
    let path = path(book).ok()?;
    let path = c_string(&path);
    if !io.exists(path) {
        return None;
    }
    let result = io.read_file(path, decode);
    match result {
        None => {
            io.log(
                1,
                b"PERSIST",
                format_args!("Failed to read "),
                &[path, b" (empty)"],
            );
            None
        }
        Some(Err(error)) => {
            let code: &[u8] = match error {
                Error::InvalidInput => b"InvalidInput",
                Error::IncompleteInput => b"IncompleteInput",
                Error::EmptyInput => b"EmptyInput",
                Error::TooDeep => b"TooDeep",
                Error::Allocation | Error::Size => b"NoMemory",
                Error::NameTooLong => unreachable!("load truncates names"),
            };
            io.log(
                1,
                b"PERSIST",
                format_args!("JSON parse error in "),
                &[path, b": ", code],
            );
            None
        }
        Some(Ok(records)) => {
            io.log(
                0,
                b"BKM",
                format_args!("Loaded {} bookmarks from file", records.len()),
                &[],
            );
            Some(records)
        }
    }
}

fn c_string(bytes: &[u8]) -> &[u8] {
    &bytes[..bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len())]
}
