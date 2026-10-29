extern crate alloc;
use super::bytes;
use alloc::vec::Vec;
use core::ffi::c_void;
#[repr(C)]
pub struct Registry {
    context: *mut c_void,
    open: unsafe extern "C" fn(*mut c_void, u8, *const u8) -> bool,
    next: unsafe extern "C" fn(*mut c_void, u8, *mut u8) -> u8,
    close: unsafe extern "C" fn(*mut c_void, u8),
    exists: unsafe extern "C" fn(*mut c_void, *const u8) -> bool,
    emit: unsafe extern "C" fn(*mut c_void, *const u8, usize, *const u8, usize),
    diagnostic: unsafe extern "C" fn(*mut c_void, u8, *const u8),
}
impl Registry {
    fn open(&self, id: u8, path: &[u8]) -> bool {
        unsafe { (self.open)(self.context, id, path.as_ptr()) }
    }
    fn next(&self, id: u8, name: &mut [u8; 128]) -> u8 {
        unsafe { (self.next)(self.context, id, name.as_mut_ptr()) }
    }
    fn exists(&self, path: &[u8]) -> bool {
        unsafe { (self.exists)(self.context, path.as_ptr()) }
    }
    fn diagnostic(&self, event: u8, path: &[u8]) {
        unsafe { (self.diagnostic)(self.context, event, path.as_ptr()) }
    }
    fn emit(&self, name: &[u8], stem: &[u8]) {
        unsafe {
            (self.emit)(
                self.context,
                name.as_ptr(),
                name.len(),
                stem.as_ptr(),
                stem.len(),
            )
        }
    }
}
struct Directory<'a> {
    io: &'a Registry,
    slot: u8,
}
impl Drop for Directory<'_> {
    fn drop(&mut self) {
        unsafe {
            (self.io.close)(self.io.context, self.slot);
        }
    }
}
fn path(parts: &[&[u8]]) -> Option<Vec<u8>> {
    let n = parts
        .iter()
        .try_fold(1usize, |n, p| n.checked_add(p.len()))?;
    let mut out = Vec::new();
    out.try_reserve_exact(n).ok()?;
    for p in parts {
        out.extend_from_slice(p);
    }
    out.push(0);
    Some(out)
}
fn text(buf: &[u8]) -> &[u8] {
    &buf[..buf.iter().position(|b| *b == 0).unwrap_or(buf.len())]
}
fn stem(io: &Registry, folder: &[u8], found: &mut [u8; 128]) -> bool {
    let _directory = Directory { io, slot: 1 };
    if !io.open(1, folder) {
        return false;
    }
    found[0] = 0;
    let mut name = [0; 128];
    loop {
        let kind = io.next(1, &mut name);
        if kind == 0 {
            break;
        }
        let name = text(&name);
        if kind == 2 || name.starts_with(b"._") || name.len() <= 4 || !name.ends_with(b".idx") {
            continue;
        }
        let name = &name[..name.len() - 4];
        if found[0] != 0 && text(found) != name {
            io.diagnostic(0, folder);
            return false;
        }
        found[..name.len()].copy_from_slice(name);
        found[name.len()] = 0;
    }
    if found[0] == 0 {
        return false;
    }
    let Some(mut data) = path(&[text(folder), b"/", text(found), b".dict"]) else {
        return false;
    };
    if io.exists(&data) {
        return true;
    }
    if data.try_reserve_exact(3).is_err() {
        return false;
    }
    data.pop();
    data.extend_from_slice(b".dz\0");
    if !io.exists(&data) {
        io.diagnostic(1, folder);
        return false;
    }
    true
}
const ROOTS: [&[u8]; 2] = [b"/dictionaries\0", b"/.dictionaries\0"];
/// # Safety
/// Callbacks and context are valid for the call; folder is a borrowed byte slice.
#[no_mangle]
pub unsafe extern "C" fn xp_dictionary_registry(
    io: *const Registry,
    folder: *const u8,
    len: usize,
    discover: bool,
) -> bool {
    let io = &*io;
    let folder = bytes(folder, len);
    if !discover
        && (folder.is_empty()
            || folder[0] == b'.'
            || folder.contains(&b'/')
            || folder.contains(&b'\\'))
    {
        return false;
    }
    for root in ROOTS {
        if discover {
            let _directory = Directory { io, slot: 0 };
            if !io.open(0, root) {
                io.diagnostic(2, root);
                continue;
            }
            let mut name = [0; 128];
            loop {
                let kind = io.next(0, &mut name);
                if kind == 0 {
                    break;
                }
                if kind != 2 || name[0] == b'.' {
                    continue;
                }
                let name = text(&name);
                let Some(folder) = path(&[text(root), b"/", name]) else {
                    return false;
                };
                let mut found = [0; 128];
                if !stem(io, &folder, &mut found) {
                    continue;
                }
                io.emit(name, text(&found));
                io.diagnostic(3, name);
            }
        } else {
            let Some(folder) = path(&[text(root), b"/", folder]) else {
                return false;
            };
            let mut found = [0; 128];
            if stem(io, &folder, &mut found) {
                io.emit(text(&folder), text(&found));
                return true;
            }
        }
    }
    discover
}
