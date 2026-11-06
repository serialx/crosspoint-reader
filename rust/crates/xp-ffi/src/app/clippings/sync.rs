use super::{
    append,
    io::{File, Io, Mode},
    record::{Record, TEXT_MAX},
    transaction::Change,
    Store,
};

fn recover_tail(file: &mut impl File, io: &impl Io) -> bool {
    let size = file.size();
    let tail = size % 65;
    let ok = tail == 0 || file.truncate(size - tail);
    if !ok {
        io.log(
            1,
            format_args!("Failed to recover clipping deletion journal"),
            &[],
        );
    }
    ok
}

fn id(bytes: &[u8; 65]) -> &[u8] {
    &bytes[..bytes.iter().position(|&byte| byte == 0).unwrap_or(65)]
}

impl<I: Io, const B: usize> Store<I, B> {
    pub fn remove(&mut self, index: usize) -> bool {
        if !self.queue_remove(index) {
            return false;
        }
        let Ok(pending) = self.prepare_remove(index) else {
            return false;
        };
        let Ok(written) = self.persist(pending, None) else {
            return false;
        };
        self.publish_clean(written);
        true
    }

    // Journal I/O precedes dirty state and offset allocation, including on failure.
    pub(crate) fn queue_remove(&self, index: usize) -> bool {
        if !self.loaded || index >= self.count {
            return false;
        }
        if self.index.slot(index).id[0] != 0 {
            let Ok(path) = append(&[&self.path, b".deleted"]) else {
                return false;
            };
            let Some(mut journal) = self.io.open(&path, Mode::Append) else {
                self.error(
                    format_args!("Failed to open clipping deletion journal"),
                    &[],
                );
                return false;
            };
            if !recover_tail(&mut journal, &self.io) {
                return false;
            }
            let original = journal.size();
            if journal.write(&self.index.slot(index).id) != 65 {
                if !journal.truncate(original) {
                    self.error(
                        format_args!("Failed to roll back clipping deletion journal"),
                        &[],
                    );
                }
                self.error(format_args!("Failed to queue clipping deletion"), &[]);
                return false;
            }
            journal.flush();
        }
        true
    }

    pub(crate) fn prepare_remove(
        &mut self,
        index: usize,
    ) -> super::Result<super::transaction::Pending> {
        if !self.loaded || index >= self.count {
            return Err(());
        }
        self.dirty = true;
        self.prepare_write(Change::Remove(index), None)
    }

    pub fn prepare_sync(&mut self) -> bool {
        for index in 0..self.count {
            let row = self.index.slot_mut(index);
            if row.id[0] != 0 {
                continue;
            }
            let mut bytes = [0; 16];
            self.io.random(&mut bytes);
            for (i, byte) in bytes.into_iter().enumerate() {
                row.id[i * 2] = b"0123456789abcdef"[(byte >> 4) as usize];
                row.id[i * 2 + 1] = b"0123456789abcdef"[(byte & 15) as usize];
            }
            row.id[32] = 0;
            self.dirty = true;
        }
        self.save()
    }

    pub fn finish_uploads(&mut self) -> bool {
        for index in 0..self.count {
            let row = self.index.slot_mut(index);
            if row.pending == 0 {
                continue;
            }
            row.pending = 0;
            self.dirty = true;
        }
        self.save()
    }

    pub fn next_deletion(&self, offset: &mut u32, out: &mut [u8; 65]) -> bool {
        out[0] = 0;
        let Ok(path) = append(&[&self.path, b".deleted"]) else {
            return false;
        };
        if !self.io.exists(&path) {
            return true;
        }
        let Some(mut journal) = self.io.open(&path, Mode::ReadWrite) else {
            return false;
        };
        if !recover_tail(&mut journal, &self.io) {
            return false;
        }
        if !(*offset).is_multiple_of(65) || !journal.seek(*offset) {
            self.error(format_args!("Invalid clipping deletion journal"), &[]);
            return false;
        }
        while *offset < journal.size() {
            if journal.read(out) != 65 {
                self.error(format_args!("Failed to read clipping deletion"), &[]);
                return false;
            }
            *offset += 65;
            out[64] = 0;
            if !(0..self.count).any(|i| id(&self.index.slot(i).id) == id(out)) {
                return true;
            }
        }
        out[0] = 0;
        true
    }

    pub fn finish_deletions(&self) -> bool {
        let Ok(path) = append(&[&self.path, b".deleted"]) else {
            return false;
        };
        let ok = !self.io.exists(&path) || self.io.remove(&path);
        if !ok {
            self.error(format_args!("Failed to clear acknowledged deletions"), &[]);
        }
        ok
    }

    pub fn apply_remote(&mut self, row: Record, text: &[u8], deleted: bool) -> bool {
        if !self.loaded || (!deleted && text.len() > TEXT_MAX) {
            return false;
        }
        let found = (0..self.count).find(|&i| id(&self.index.slot(i).id) == id(&row.id));
        let change = match found {
            None => {
                if deleted {
                    return true;
                }
                if self.count >= B * 16 || !self.reserve(self.count + 1) {
                    self.error(format_args!("Remote clipping exceeds local capacity"), &[]);
                    return false;
                }
                Change::Insert(row)
            }
            Some(index) => {
                let previous = self.index.slot(index);
                if !deleted && previous.revision == row.revision && previous.pending == 0 {
                    return true;
                }
                if deleted {
                    Change::Remove(index)
                } else {
                    Change::Replace(index, row)
                }
            }
        };
        self.rewrite(change, (!deleted).then_some(text)).is_ok()
    }
}
