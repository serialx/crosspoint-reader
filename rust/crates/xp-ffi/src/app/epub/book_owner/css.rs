extern crate alloc;

use super::{writer::io::FileSlot, Text, XpEpubLeaves};
mod diagnostics;
mod files;
use crate::{
    app::{abi, resources::Buffer},
    book_css,
    css::Css,
};
use alloc::boxed::Box;
use core::{ffi::c_void, ptr};
pub(super) use files::{discover, parse_files};

#[cfg(feature = "app-test")]
#[path = "../test_support/book_css_files.rs"]
mod files_tests;

pub(in crate::app::epub) use book_css::Action;
use book_css::Op;

enum Path<'a> {
    Borrowed(&'a Text),
    Owned(Text),
}
impl Path<'_> {
    fn text(&self) -> &Text {
        match self {
            Self::Borrowed(text) => text,
            Self::Owned(text) => text,
        }
    }
}
const SUFFIXES: [&[u8]; 3] = [
    b"/css_rules.cache",
    b"/css_rules.cache.tmp",
    b"/css_rules.cache.bak",
];
fn path<'a>(base: &Text, paths: &'a [Text; 3], index: usize) -> Result<Path<'a>, ()> {
    if !paths[index].is_empty() {
        return Ok(Path::Borrowed(&paths[index]));
    }
    let mut text = Text::default();
    text.assign_parts(&[base.bytes(), SUFFIXES[index]])
        .map_err(|_| ())?;
    Ok(Path::Owned(text))
}

struct Context<'a> {
    destination: &'a mut Option<Box<Css>>,
    handle: *mut *mut Css,
    cache_path: &'a Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    file: FileSlot,
    source: Option<&'a mut FileSlot>,
    paths: [Text; 3],
    buffer: Option<Buffer>,
    order: Option<Buffer>,
    io: *mut book_css::Io,
}

// The CSS owner is detached into this transaction, then returned even on failure.
// The policy mutates only this raw owner slot; native callbacks own physical I/O.
pub(in crate::app::epub) fn run(
    owner: &mut Option<Box<Css>>,
    cache_path: &Text,
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    action: Action,
    complete: bool,
    source: Option<&mut FileSlot>,
) -> u32 {
    let mut handle = owner.take().map_or(ptr::null_mut(), Box::into_raw);
    // Policy slots stay outside the context borrowed by physical callbacks.
    let mut io = book_css::Io {
        owner: ptr::addr_of_mut!(handle),
        buffer: ptr::null_mut(),
        order: ptr::null_mut(),
        word: [0; 4],
        capacity: 0,
    };
    let mut context = Context {
        destination: owner,
        handle: ptr::addr_of_mut!(handle),
        cache_path,
        resources,
        leaves,
        file: FileSlot::new(),
        source,
        paths: core::array::from_fn(|_| Text::default()),
        buffer: None,
        order: None,
        io: ptr::addr_of_mut!(io),
    };
    unsafe extern "C" fn operation(raw: *mut c_void, op: u32, argument: u32) -> u32 {
        unsafe { (&mut *raw.cast::<Context<'_>>()).operation(op, argument) }
    }
    let services = book_css::Services {
        ctx: (&mut context as *mut Context<'_>).cast(),
        operation: Some(operation),
        millis: None,
        diagnostic: Some(diagnostics::transaction),
    };
    let diagnostics = xp_core::css::Diagnostics {
        ctx: (leaves as *const XpEpubLeaves).cast_mut().cast(),
        diagnostic: Some(diagnostics::parser),
    };
    unsafe {
        book_css::xp_book_css(
            &services,
            ptr::addr_of_mut!(io),
            &diagnostics,
            action as u32,
            complete.into(),
        )
    }
}

impl Context<'_> {
    fn file(&mut self) -> &mut FileSlot {
        self.source.as_deref_mut().unwrap_or(&mut self.file)
    }

    fn exists(&self, index: usize) -> u32 {
        let Ok(path) = path(self.cache_path, &self.paths, index) else {
            return 0;
        };
        u32::from(unsafe { (self.resources.path_exists)(path.text().wire()) })
    }

    fn remove(&self, index: usize) -> u32 {
        let Ok(path) = path(self.cache_path, &self.paths, index) else {
            return 0;
        };
        u32::from(unsafe { (self.leaves.path_remove)(path.text().wire()) })
    }

    fn rename(&self, from: usize, to: usize) -> u32 {
        let (Ok(from), Ok(to)) = (
            path(self.cache_path, &self.paths, from),
            path(self.cache_path, &self.paths, to),
        ) else {
            return 0;
        };
        u32::from(unsafe { (self.leaves.path_rename)(from.text().wire(), to.text().wire()) })
    }

    fn open(&mut self, index: usize, write: bool) -> u32 {
        let Ok(path) = path(self.cache_path, &self.paths, index) else {
            return 0;
        };
        let file = self.source.as_deref_mut().unwrap_or(&mut self.file);
        u32::from(file.open(self.leaves, path.text(), write, c"CSS"))
    }

    fn operation(&mut self, operation: u32, argument: u32) -> u32 {
        let io = unsafe { &mut *self.io };
        match operation {
            x if x == Op::EmptyPath as u32 => u32::from(self.cache_path.is_empty()),
            x if x == Op::PreparePaths as u32 => {
                for (index, suffix) in SUFFIXES.iter().enumerate() {
                    if argument & (1 << index) != 0
                        && self.paths[index]
                            .assign_parts(&[self.cache_path.bytes(), suffix])
                            .is_err()
                    {
                        return 0;
                    }
                }
                0
            }
            x if x == Op::ExistsFinal as u32 => self.exists(0),
            x if x == Op::ExistsBackup as u32 => self.exists(2),
            x if x == Op::RenameBackupFinal as u32 => self.rename(2, 0),
            x if x == Op::RemoveFinal as u32 => self.remove(0),
            x if x == Op::RemoveTemporary as u32 => self.remove(1),
            x if x == Op::RemoveBackup as u32 => self.remove(2),
            x if x == Op::RenameFinalBackup as u32 => self.rename(0, 2),
            x if x == Op::RenameTemporaryFinal as u32 => self.rename(1, 0),
            x if x == Op::OpenFinal as u32 => self.open(0, false),
            x if x == Op::OpenTemporary as u32 => self.open(1, true),
            x if x == Op::Close as u32 => u32::from(self.file().close()),
            x if x == Op::ReadWord as u32 => {
                let bytes = io.word.as_mut_ptr();
                unsafe { self.file().read(bytes, argument as usize) as u32 }
            }
            x if x == Op::Available as u32 => {
                let file = self.file();
                file.size().wrapping_sub(file.position())
            }
            x if x == Op::Skip as u32 => {
                let position = self.file().position().wrapping_add(argument);
                u32::from(self.file().seek(position))
            }
            x if x == Op::AllocateBuffer as u32 => {
                self.buffer =
                    Buffer::allocate(self.resources, abi::MEMORY_DEFAULT, argument as usize);
                io.buffer = self.buffer.as_ref().map_or(ptr::null_mut(), Buffer::as_ptr);
                io.capacity = argument as usize;
                if !io.buffer.is_null() {
                    unsafe { io.buffer.write_bytes(0, io.capacity) };
                }
                u32::from(self.buffer.is_some())
            }
            x if x == Op::ReadBuffer as u32 => {
                let (bytes, capacity) = (io.buffer, io.capacity);
                unsafe { self.file().read(bytes, capacity) as u32 }
            }
            x if x == Op::AllocateOrder as u32 => {
                let Some(bytes) = (argument as usize).checked_mul(core::mem::size_of::<u16>())
                else {
                    return 0;
                };
                // Like new uint16_t[0], an empty rule order still has an owner.
                self.order = Buffer::allocate(self.resources, abi::MEMORY_DEFAULT, bytes.max(1));
                io.order = self
                    .order
                    .as_ref()
                    .map_or(ptr::null_mut(), |buffer| buffer.as_ptr().cast());
                if !io.order.is_null() {
                    unsafe { io.order.write_bytes(0, argument as usize) };
                }
                u32::from(self.order.is_some())
            }
            x if x == Op::WriteBuffer as u32 => {
                let bytes = io.buffer;
                unsafe { self.file().write(bytes, argument as usize) as u32 }
            }
            x if x == Op::WriteWord as u32 => {
                let bytes = io.word.as_ptr();
                unsafe { self.file().write(bytes, 4) as u32 }
            }
            x if x == Op::DropOrder as u32 => {
                self.order = None;
                io.order = ptr::null_mut();
                0
            }
            x if x == Op::SourceValid as u32 => u32::from(self.file().is_open()),
            x if x == Op::Restore as u32 => {
                // Restore borrows no CSS state and keeps its paths/file in a separate scope.
                let mut unused = None;
                run(
                    &mut unused,
                    self.cache_path,
                    self.resources,
                    self.leaves,
                    Action::Restore,
                    true,
                    None,
                )
            }
            _ => 0,
        }
    }
}

impl Drop for Context<'_> {
    fn drop(&mut self) {
        self.order = None;
        self.buffer = None;
        self.file.close();
        let handle = unsafe { *self.handle };
        *self.destination = if handle.is_null() {
            None
        } else {
            Some(unsafe { Box::from_raw(handle) })
        };
        unsafe { *self.handle = ptr::null_mut() };
    }
}

#[cfg(feature = "app-test")]
#[path = "../test_support/book_css.rs"]
mod tests;
