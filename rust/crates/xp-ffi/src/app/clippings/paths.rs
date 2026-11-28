extern crate alloc;

use super::{
    append, copy,
    io::{File, Io, Mode},
    Result, Store,
};
use alloc::vec::Vec;

const SUFFIXES: [&[u8]; 5] = [b"", b".bak", b".tmp", b".deleted", b".migrate.bak"];
const DEPTH: usize = 32;

#[derive(Clone, Copy)]
enum Phase {
    Check,
    Move,
    Rollback,
}

struct Directory<F> {
    file: Option<F>,
    from: Vec<u8>,
    to: Vec<u8>,
}
struct Workspace<F: File> {
    stack: [Directory<F>; DEPTH],
    name: [u8; 1024],
}
impl<F: File> Drop for Workspace<F> {
    fn drop(&mut self) {
        // Native MoveDirectory[] destroys deeper directory handles first.
        for entry in self.stack.iter_mut().rev() {
            drop(entry.file.take());
        }
    }
}

impl<I: Io, const B: usize> Store<I, B> {
    pub fn delete_for_path(&self, book: &[u8], kind: &[u8]) -> bool {
        let Ok(path) = self.store_path(book, kind) else {
            return false;
        };
        let mut ok = true;
        for suffix in SUFFIXES {
            let Ok(sidecar) = append(&[&path, suffix]) else {
                return false;
            };
            if self.io.exists(&sidecar) && !self.io.remove(&sidecar) {
                self.error(
                    format_args!("Failed deleting clipping state: "),
                    &[&sidecar],
                );
                ok = false;
            }
        }
        ok
    }

    fn move_stores(&self, from: &[u8], to: &[u8], phase: Phase) -> Result<()> {
        let source = self.store_path(from, b"epub")?;
        let target = self.store_path(to, b"epub")?;
        if source == target {
            return Ok(());
        }
        for suffix in SUFFIXES {
            let old = append(&[&source, suffix])?;
            let new = append(&[&target, suffix])?;
            match phase {
                Phase::Check => {
                    if self.io.exists(&new) {
                        self.error(
                            format_args!("Move would overwrite saved clippings: "),
                            &[to],
                        );
                        return Err(());
                    }
                }
                Phase::Move | Phase::Rollback => {
                    let (src, dst) = match phase {
                        Phase::Rollback => (&new, &old),
                        _ => (&old, &new),
                    };
                    if self.io.exists(src) && !self.io.rename(src, dst) {
                        self.error(
                            format_args!("Failed moving clipping state: "),
                            &[src, b" -> ", dst],
                        );
                        return Err(());
                    }
                }
            }
        }
        Ok(())
    }

    fn move_tree(
        &self,
        from: &[u8],
        to: &[u8],
        phase: Phase,
        workspace: &mut Workspace<I::File>,
    ) -> Result<()> {
        for directory in &mut workspace.stack {
            if let Some(mut file) = directory.file.take() {
                file.close();
            }
        }
        let root = self.io.open(from, Mode::Read).ok_or(())?;
        if !root.is_directory() {
            return Err(());
        }
        workspace.stack[0] = Directory {
            file: Some(root),
            from: copy(from)?,
            to: copy(to)?,
        };
        let mut depth = 1;
        while depth > 0 {
            let directory = &mut workspace.stack[depth - 1];
            let file = directory.file.as_mut().ok_or(())?;
            let Some(entry) = file.next().map_err(|_| ())? else {
                file.close();
                depth -= 1;
                continue;
            };
            let length = entry.name(&mut workspace.name);
            if length == 0 || length >= workspace.name.len() {
                self.error(format_args!("Invalid name in folder move"), &[]);
                return Err(());
            }
            let name = &workspace.name[..length];
            if name == b"." || name == b".." {
                continue;
            }
            let old = append(&[
                &directory.from,
                if directory.from.ends_with(b"/") {
                    b""
                } else {
                    b"/"
                },
                name,
            ])?;
            let new = append(&[
                &directory.to,
                if directory.to.ends_with(b"/") {
                    b""
                } else {
                    b"/"
                },
                name,
            ])?;
            if entry.is_directory() {
                if depth == DEPTH {
                    self.error(
                        format_args!("Folder move exceeds directory depth limit"),
                        &[],
                    );
                    return Err(());
                }
                workspace.stack[depth] = Directory {
                    file: Some(entry),
                    from: old,
                    to: new,
                };
                depth += 1;
            } else {
                self.move_stores(&old, &new, phase)?;
            }
        }
        Ok(())
    }

    pub fn move_book(&self, from: &[u8], to: &[u8]) -> bool {
        if from == to {
            return true;
        }
        if self.io.exists(to) {
            return false;
        }
        let directory = {
            let Some(root) = self.io.open(from, Mode::Read) else {
                return false;
            };
            root.is_directory()
        };
        let mut workspace = if directory {
            // This workspace is temporary; the name buffer must not occupy the task stack.
            let layout = core::alloc::Layout::new::<Workspace<I::File>>();
            let Some(p) = core::ptr::NonNull::new(unsafe {
                alloc::alloc::alloc(layout).cast::<Workspace<I::File>>()
            }) else {
                self.error(format_args!("OOM: folder move work stack"), &[]);
                return false;
            };
            Some(unsafe {
                let slots =
                    core::ptr::addr_of_mut!((*p.as_ptr()).stack).cast::<Directory<I::File>>();
                for i in 0..DEPTH {
                    slots.add(i).write(Directory {
                        file: None,
                        from: Vec::new(),
                        to: Vec::new(),
                    });
                }
                core::ptr::addr_of_mut!((*p.as_ptr()).name)
                    .cast::<u8>()
                    .write_bytes(0, 1024);
                alloc::boxed::Box::from_raw(p.as_ptr())
            })
        } else {
            None
        };
        let mut run = |phase| match &mut workspace {
            Some(workspace) => self.move_tree(from, to, phase, workspace),
            None => self.move_stores(from, to, phase),
        };
        if run(Phase::Check).is_err() {
            return false;
        }
        if run(Phase::Move).is_ok() && self.io.rename(from, to) {
            return true;
        }
        if run(Phase::Rollback).is_err() {
            self.error(
                format_args!("Book move rollback failed: "),
                &[from, b" -> ", to],
            );
        }
        false
    }
}
