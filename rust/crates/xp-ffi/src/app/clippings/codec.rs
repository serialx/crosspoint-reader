extern crate alloc;

use super::{
    io::{File, Io},
    record::{Record, TEXT_MAX},
    Result, Store,
};
use alloc::vec::Vec;

pub(super) fn read<const N: usize>(file: &mut impl File) -> Result<[u8; N]> {
    let mut bytes = [0; N];
    if file.read(&mut bytes) != N as i32 {
        return Err(());
    }
    Ok(bytes)
}

pub(super) fn write(file: &mut impl File, bytes: &[u8]) -> Result<()> {
    if file.write(bytes) == bytes.len() {
        Ok(())
    } else {
        Err(())
    }
}

fn string(file: &mut impl File) -> Result<Vec<u8>> {
    let length = u32::from_le_bytes(read(file)?) as usize;
    let remaining = file.available();
    if length > TEXT_MAX || remaining < 0 || length > remaining as usize {
        return Err(());
    }
    let mut bytes = Vec::new();
    bytes.try_reserve_exact(length).map_err(|_| ())?;
    bytes.resize(length, 0);
    if length > 0 && file.read(&mut bytes) != length as i32 {
        return Err(());
    }
    Ok(bytes)
}

pub(super) fn write_string(file: &mut impl File, bytes: &[u8]) -> Result<()> {
    write(file, &(bytes.len() as u32).to_le_bytes())?;
    if !bytes.is_empty() {
        write(file, bytes)?;
    }
    Ok(())
}

impl<I: Io, const BLOCKS: usize> Store<I, BLOCKS> {
    pub(super) fn read_file(&mut self) -> Result<()> {
        self.count = 0;
        let mut file = self.io.open_read(&self.path).ok_or(())?;
        let version = read::<1>(&mut file)?[0];
        if !(1..=4).contains(&version) {
            return Err(());
        }
        let count = u16::from_le_bytes(read(&mut file)?) as usize;
        let title = string(&mut file)?;
        let author = string(&mut file)?;
        let _stored_path = string(&mut file)?;
        if count > BLOCKS * 16 {
            return Err(());
        }
        if self.title.is_empty() {
            self.title = title;
        }
        if self.author.is_empty() {
            self.author = author;
        }
        if !self.index.reserve(count) {
            return Err(());
        }
        for index in 0..count {
            let mut row = Record {
                spine: u16::from_le_bytes(read(&mut file)?),
                start_page: u16::from_le_bytes(read(&mut file)?),
                end_page: u16::from_le_bytes(read(&mut file)?),
                page_count: u16::from_le_bytes(read(&mut file)?),
                start_word: u16::from_le_bytes(read(&mut file)?),
                end_word: u16::from_le_bytes(read(&mut file)?),
                word_count: u16::from_le_bytes(read(&mut file)?),
                paragraph: u16::from_le_bytes(read(&mut file)?),
                timestamp: u32::from_le_bytes(read(&mut file)?),
                ..Record::default()
            };
            if version >= 3 {
                row.signature = u32::from_le_bytes(read(&mut file)?);
            }
            if version >= 4 {
                row.start_offset = u32::from_le_bytes(read(&mut file)?);
                row.end_offset = u32::from_le_bytes(read(&mut file)?);
                row.revision = u64::from_le_bytes(read(&mut file)?);
                row.pending = read::<1>(&mut file)?[0];
                row.id = read(&mut file)?;
            }
            row.id[64] = 0;
            row.chapter = read(&mut file)?;
            row.chapter[47] = 0;
            let length = if version == 1 {
                u32::from_le_bytes(read(&mut file)?)
            } else {
                let length = u16::from_le_bytes(read(&mut file)?);
                if length as usize > TEXT_MAX {
                    return Err(());
                }
                u32::from(length)
            };
            row.text_offset = file.position();
            row.text_length = length.min(TEXT_MAX as u32) as u16;
            let fits = if version == 1 {
                length <= file.available() as u32
            } else {
                i32::from(row.text_length) <= file.available()
            };
            if !fits || (length != 0 && !file.seek_relative(length)) {
                return Err(());
            }
            *self.index.slot_mut(index) = row;
            self.count += 1;
        }
        Ok(())
    }
}

pub(super) fn write_record(file: &mut impl File, row: &Record) -> Result<()> {
    for value in [
        row.spine,
        row.start_page,
        row.end_page,
        row.page_count,
        row.start_word,
        row.end_word,
        row.word_count,
        row.paragraph,
    ] {
        write(file, &value.to_le_bytes())?;
    }
    for value in [
        row.timestamp,
        row.signature,
        row.start_offset,
        row.end_offset,
    ] {
        write(file, &value.to_le_bytes())?;
    }
    write(file, &row.revision.to_le_bytes())?;
    write(file, &[row.pending])?;
    write(file, &row.id)?;
    write(file, &row.chapter)
}
