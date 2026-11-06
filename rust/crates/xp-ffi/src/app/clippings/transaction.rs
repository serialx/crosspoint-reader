extern crate alloc;

use super::{
    append, codec,
    io::{File, Io, Mode},
    record::{Record, TEXT_MAX},
    Result, Store,
};
use alloc::vec::Vec;

pub(super) enum Change {
    None,
    Insert(Record),
    Replace(usize, Record),
    Remove(usize),
}

pub(crate) struct Pending {
    change: Change,
    offsets: Vec<u32>,
}

pub(crate) struct Written {
    change: Change,
    offsets: Vec<u32>,
}

impl Change {
    fn count(&self, current: usize) -> usize {
        match self {
            Self::Insert(_) => current + 1,
            Self::Remove(_) => current - 1,
            _ => current,
        }
    }
    fn row<'a, I: Io, const B: usize>(&'a self, store: &'a Store<I, B>, i: usize) -> &'a Record {
        match self {
            Self::Insert(row) if i == store.count => row,
            Self::Replace(index, row) if *index == i => row,
            Self::Remove(index) if i >= *index => store.index.slot(i + 1),
            _ => store.index.slot(i),
        }
    }
    fn replaces(&self, i: usize, current: usize) -> bool {
        match self {
            Self::Insert(_) => i == current,
            Self::Replace(index, _) => i == *index,
            _ => false,
        }
    }
}

fn copy_text(source: &mut impl File, destination: &mut impl File, mut length: usize) -> Result<()> {
    let mut bytes = [0; 128];
    while length > 0 {
        let count = length.min(bytes.len());
        if source.read(&mut bytes[..count]) != count as i32 {
            return Err(());
        }
        codec::write(destination, &bytes[..count])?;
        length -= count;
    }
    Ok(())
}

impl<I: Io, const B: usize> Store<I, B> {
    pub(super) fn prepare_write(
        &self,
        change: Change,
        replacement: Option<&[u8]>,
    ) -> Result<Pending> {
        let count = change.count(self.count);
        if !self.loaded
            || self.path.is_empty()
            || self.title.len() > TEXT_MAX
            || self.author.len() > TEXT_MAX
            || self.book.len() > TEXT_MAX
            || count > B * 16
            || replacement.is_some_and(|s| s.len() > TEXT_MAX)
        {
            self.error(
                format_args!("Refusing to write invalid or incompletely loaded clipping store"),
                &[],
            );
            return Err(());
        }
        let mut offsets = Vec::new();
        offsets
            .try_reserve_exact(count)
            .map_err(|_| self.error(format_args!("OOM: clipping text offsets"), &[]))?;
        offsets.resize(count, 0);
        Ok(Pending { change, offsets })
    }

    // The operation lease retains this immutable committed index through physical I/O.
    pub(crate) fn persist(&self, pending: Pending, replacement: Option<&[u8]>) -> Result<Written> {
        let Pending {
            change,
            mut offsets,
        } = pending;
        let count = offsets.len();
        self.io.mkdir(b"/.crosspoint");
        self.io.mkdir(b"/.crosspoint/clippings");
        let tmp = append(&[&self.path, b".tmp"])?;
        let backup = append(&[&self.path, b".bak"])?;
        if !self.io.exists(&self.path) && self.io.exists(&backup) {
            if !self.io.rename(&backup, &self.path) {
                self.error(
                    format_args!("Failed to recover clipping backup: "),
                    &[&backup],
                );
                return Err(());
            }
            self.io.log(
                2,
                format_args!("Recovered clipping backup: "),
                &[&self.path],
            );
        }
        if self.io.exists(&tmp) {
            self.io.remove(&tmp);
        }
        if self.io.exists(&backup) && self.io.exists(&self.path) {
            self.io.remove(&backup);
        }
        let has_source = self.io.exists(&self.path);
        let has_destination = self.io.exists(&self.path);
        let mut source = if has_source {
            Some(self.io.open_read(&self.path).ok_or_else(|| {
                self.error(
                    format_args!("Failed to open clipping source for rewrite: "),
                    &[&self.path],
                )
            })?)
        } else {
            None
        };
        let Some(mut file) = self.io.open(&tmp, Mode::Rewrite) else {
            if let Some(source) = &mut source {
                source.close();
            }
            self.error(
                format_args!("Failed to open clipping temp file for write: "),
                &[&tmp],
            );
            return Err(());
        };
        let wrote: Result<()> = (|| {
            (|| {
                codec::write(&mut file, &[4])?;
                codec::write(&mut file, &(count as u16).to_le_bytes())?;
                codec::write_string(&mut file, &self.title)?;
                codec::write_string(&mut file, &self.author)?;
                codec::write_string(&mut file, &self.book)?;
                Ok(())
            })()
            .map_err(|()| self.error(format_args!("Failed to write clipping header: "), &[&tmp]))?;
            for (i, offset) in offsets.iter_mut().enumerate() {
                let row = change.row(self, i);
                codec::write_record(&mut file, row).map_err(|()| {
                    self.error(
                        format_args!("Failed to write clipping record {}: ", i),
                        &[&self.path],
                    )
                })?;
                let text = replacement.filter(|_| change.replaces(i, self.count));
                let length = text.map_or(row.text_length as usize, |text| text.len().min(TEXT_MAX));
                codec::write(&mut file, &(length as u16).to_le_bytes()).map_err(|()| {
                    self.error(
                        format_args!("Failed to write clipping text length {}: ", i),
                        &[&tmp],
                    )
                })?;
                let position = file.position();
                if length > 0 {
                    (|| {
                        if let Some(text) = text {
                            codec::write(&mut file, &text[..length])?;
                        } else {
                            let source = source.as_mut().ok_or(())?;
                            if !source.seek(row.text_offset) {
                                return Err(());
                            }
                            copy_text(source, &mut file, length)?;
                        }
                        Ok(())
                    })()
                    .map_err(|()| {
                        self.error(
                            format_args!("Failed to write clipping text {}: ", i),
                            &[&tmp],
                        )
                    })?;
                }
                *offset = position;
            }
            Ok(())
        })();
        if wrote.is_ok() {
            file.flush();
        }
        file.close();
        if let Some(source) = &mut source {
            source.close();
        }
        if wrote.is_err() {
            self.io.remove(&tmp);
            return Err(());
        }
        if has_destination && !self.io.rename(&self.path, &backup) {
            self.error(
                format_args!("Failed to back up clipping file: "),
                &[&self.path],
            );
            self.io.remove(&tmp);
            return Err(());
        }
        if !self.io.rename(&tmp, &self.path) {
            self.error(
                format_args!("Failed to replace clipping file: "),
                &[&self.path],
            );
            self.io.remove(&tmp);
            if has_destination {
                self.io.rename(&backup, &self.path);
            }
            return Err(());
        }
        if has_destination && self.io.exists(&backup) {
            self.io.remove(&backup);
        }
        Ok(Written { change, offsets })
    }

    pub(super) fn rewrite(&mut self, change: Change, replacement: Option<&[u8]>) -> Result<()> {
        let pending = self.prepare_write(change, replacement)?;
        let written = self.persist(pending, replacement)?;
        self.publish(written);
        Ok(())
    }

    pub(crate) fn publish_clean(&mut self, written: Written) {
        self.publish(written);
        self.dirty = false;
    }

    fn publish(&mut self, written: Written) {
        let Written { change, offsets } = written;
        match change {
            Change::None => {}
            Change::Insert(row) => {
                *self.index.slot_mut(self.count) = row;
                self.count += 1;
            }
            Change::Replace(index, row) => {
                *self.index.slot_mut(index) = row;
            }
            Change::Remove(index) => {
                for i in index..self.count - 1 {
                    *self.index.slot_mut(i) = *self.index.slot(i + 1);
                }
                self.count -= 1;
            }
        }
        for (i, offset) in offsets.into_iter().enumerate() {
            self.index.slot_mut(i).text_offset = offset;
        }
    }
}
