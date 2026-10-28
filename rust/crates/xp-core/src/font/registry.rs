//! Font selection and discovery policy. Container and directory ownership stays
//! with the facade so public C++ references and allocation behavior stay stable.
use core::{
    ffi::{c_char, c_void, CStr},
    ptr,
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct File {
    pub name: *const c_char,
    pub size: u8,
    pub style: u8,
}
unsafe fn string<'a>(s: *const c_char) -> &'a [u8] {
    CStr::from_ptr(s).to_bytes()
}

/// # Safety
/// files loans `count` ABI-compatible file records for the duration of this call.
pub unsafe fn nearest(files: *const File, count: usize, size: u8, style: u8, exact: bool) -> i32 {
    let mut best = -1;
    let mut delta = 255;
    let mut best_size = 0;
    for i in 0..count {
        let f = &*files.add(i);
        if f.style != style {
            continue;
        }
        if exact {
            if f.size == size {
                return i as i32;
            }
            continue;
        }
        let d = f.size.abs_diff(size);
        if best < 0 || d < delta || (d == delta && f.size < best_size) {
            best = i as i32;
            delta = d;
            best_size = f.size;
        }
    }
    best
}
pub fn font_id(mut hash: u32, name: &[u8], size: u8) -> i32 {
    for &b in name {
        hash = (hash ^ u32::from(b)).wrapping_mul(16777619);
    }
    hash = (hash ^ u32::from(size)).wrapping_mul(16777619);
    if hash == 0 {
        1
    } else {
        hash as i32
    }
}
pub fn vector_name(name: &[u8]) -> Option<usize> {
    let n = name.len().checked_sub(4)?;
    if n == 0 || n > 127 {
        return None;
    }
    let suffix = &name[n..];
    [b".ttf", b".otf", b".ttc"]
        .iter()
        .any(|s| suffix.eq_ignore_ascii_case(*s))
        .then_some(n)
}
pub fn vector_style(name: &[u8]) -> u8 {
    let bold = name.windows(4).any(|s| s.eq_ignore_ascii_case(b"bold"));
    let italic = name.windows(6).any(|s| s.eq_ignore_ascii_case(b"italic"))
        || name.windows(7).any(|s| s.eq_ignore_ascii_case(b"oblique"));
    u8::from(bold) | (u8::from(italic) << 1)
}
fn tail(size: u8, out: &mut [u8; 11]) -> usize {
    out[0] = b'_';
    let mut n = 1;
    if size >= 100 {
        out[n] = b'0' + size / 100;
        n += 1;
    }
    if size >= 10 {
        out[n] = b'0' + size / 10 % 10;
        n += 1;
    }
    out[n] = b'0' + size % 10;
    n += 1;
    out[n..n + 7].copy_from_slice(b".cpfont");
    n + 7
}
pub fn canonical(name: &[u8], family: &[u8], size: u8) -> bool {
    let mut buf = [0u8; 11];
    let n = tail(size, &mut buf);
    name.starts_with(family) && name.get(family.len()..) == Some(&buf[..n])
}
/// # Safety
/// Strings are terminated; out is writable for capacity bytes (null if zero).
pub unsafe fn path(
    root: *const c_char,
    family: *const c_char,
    file: *const c_char,
    size: u8,
    loose: bool,
    out: *mut u8,
    capacity: usize,
) -> bool {
    if root.is_null() {
        return false;
    }
    let mut tail_buf = [0u8; 11];
    let suffix_len = tail(size, &mut tail_buf);
    let mut parts: [&[u8]; 6] = [&[]; 6];
    parts[0] = string(root);
    parts[1] = b"/";
    if file.is_null() {
        parts[2] = string(family);
        parts[3] = b"/";
        parts[4] = parts[2];
        parts[5] = &tail_buf[..suffix_len];
    } else if loose {
        parts[2] = string(file);
    } else {
        parts[2] = string(family);
        parts[3] = b"/";
        parts[4] = string(file);
    }
    let mut length = 0;
    for part in parts {
        for &b in part {
            if length + 1 < capacity {
                out.add(length).write(b);
            }
            length += 1;
        }
    }
    if capacity != 0 {
        out.add(length.min(capacity - 1)).write(0);
    }
    length > 0 && length < capacity
}

#[repr(C)]
pub struct Candidate {
    pub index: usize,
    pub weight: u16,
    pub italic: bool,
}
/// # Safety
/// Candidates index files and filenames remain live and terminated during selection.
pub unsafe fn select_faces(
    candidates: *const Candidate,
    count: usize,
    files: *const File,
    selected: *mut i32,
) -> bool {
    let pick = |italic: bool, target: u16, exclude: i32| {
        let mut best = -1;
        for i in 0..count {
            let c = &*candidates.add(i);
            if c.italic != italic || i as i32 == exclude {
                continue;
            }
            let replace = if best < 0 {
                true
            } else {
                let b = &*candidates.add(best as usize);
                let dc = c.weight.abs_diff(target);
                let db = b.weight.abs_diff(target);
                dc < db
                    || (dc == db
                        && (c.weight < b.weight
                            || (c.weight == b.weight
                                && string((*files.add(c.index)).name)
                                    < string((*files.add(b.index)).name))))
            };
            if replace {
                best = i as i32;
            }
        }
        best
    };
    let mut regular = pick(false, 400, -1);
    if regular < 0 {
        regular = pick(true, 400, -1);
    }
    if regular < 0 {
        for i in 0..4 {
            selected.add(i).write(-1);
        }
        return false;
    }
    let r = &*candidates.add(regular as usize);
    let mut bold = pick(false, 700, regular);
    if bold >= 0 && (*candidates.add(bold as usize)).weight <= r.weight {
        bold = -1;
    }
    let italic = if r.italic { -1 } else { pick(true, 400, -1) };
    let mut bi = pick(true, 700, if italic >= 0 { italic } else { regular });
    if bi >= 0
        && italic >= 0
        && (*candidates.add(bi as usize)).weight <= (*candidates.add(italic as usize)).weight
    {
        bi = -1;
    }
    for (i, index) in [regular, bold, italic, bi].iter().enumerate() {
        selected.add(i).write(if *index < 0 {
            -1
        } else {
            (*candidates.add(*index as usize)).index as i32
        });
    }
    r.italic
}

#[repr(C)]
pub struct Discovery {
    pub ctx: *mut c_void,
    pub vectors: bool,
    pub open: unsafe extern "C" fn(*mut c_void, *const c_char, bool) -> i32,
    pub next: unsafe extern "C" fn(*mut c_void, bool, *mut u8, *mut bool) -> bool,
    pub close: unsafe extern "C" fn(*mut c_void, bool),
    pub families: unsafe extern "C" fn(*mut c_void) -> usize,
    pub family_name: unsafe extern "C" fn(*mut c_void, usize) -> *const c_char,
    pub begin_family: unsafe extern "C" fn(*mut c_void, *const c_char, *const u8, usize, bool),
    pub files: unsafe extern "C" fn(*mut c_void, bool, *mut usize) -> *const File,
    pub add_file: unsafe extern "C" fn(*mut c_void, bool, *const c_char, u8, u8, bool) -> bool,
    pub commit_family: unsafe extern "C" fn(*mut c_void, bool, *const c_char) -> usize,
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, *const c_char, *const c_char, usize),
    pub end_family: unsafe extern "C" fn(*mut c_void),
}
unsafe fn family_exists(io: &Discovery, name: &[u8]) -> bool {
    for i in 0..(io.families)(io.ctx) {
        if string((io.family_name)(io.ctx, i)) == name {
            return true;
        }
    }
    false
}
unsafe fn scan_directory(
    io: &Discovery,
    root: *const c_char,
    family: *const c_char,
    dir: *const c_char,
) {
    if (io.open)(io.ctx, dir, false) != 1 {
        (io.close)(io.ctx, false);
        return;
    }
    let mut name = [0u8; 128];
    let mut directory = false;
    while (io.next)(io.ctx, false, name.as_mut_ptr(), &mut directory) {
        if directory || name[0] == b'.' || name[0] == b'_' {
            continue;
        }
        let text = string(name.as_ptr().cast());
        let (vector, size, style) = if let Some(size) = crate::sdfont::filename(text) {
            (false, size, 0)
        } else if io.vectors {
            if let Some(base) = vector_name(text) {
                (true, 0, vector_style(&text[..base]))
            } else {
                continue;
            }
        } else {
            continue;
        };
        if !vector {
            let mut count = 0;
            let files = (io.files)(io.ctx, false, &mut count);
            if nearest(files, count, size, style, true) >= 0 {
                (io.diagnostic)(io.ctx, 3, name.as_ptr().cast(), dir, 0);
                continue;
            }
        }
        let canonical = !vector && canonical(text, string(family), size);
        // The allocator callback preserves filename OOM handling before checking paths.
        if !(io.add_file)(io.ctx, vector, name.as_ptr().cast(), size, style, canonical) {
            continue;
        }
    }
    (io.close)(io.ctx, false);
    let mut cp_count = 0;
    (io.files)(io.ctx, false, &mut cp_count);
    let mut vec_count = 0;
    (io.files)(io.ctx, true, &mut vec_count);
    if cp_count == 0 && vec_count == 0 {
        return;
    }
    let n = (io.commit_family)(io.ctx, cp_count == 0, dir);
    (io.diagnostic)(io.ctx, 4, family, root, n);
}
/// # Safety
/// Discovery callbacks synchronously borrow names and maintain live container
/// records until the next mutation. Root/child file handles belong to the facade.
pub unsafe fn scan_root(io: &Discovery, root: *const c_char) {
    let opened = (io.open)(io.ctx, root, true);
    if opened != 1 {
        (io.diagnostic)(
            io.ctx,
            if opened == 0 { 1 } else { 2 },
            root,
            ptr::null(),
            0,
        );
        (io.close)(io.ctx, true);
        return;
    }
    let mut name = [0u8; 128];
    let mut directory = false;
    while (io.next)(io.ctx, true, name.as_mut_ptr(), &mut directory) {
        if name[0] == b'.' || name[0] == b'_' {
            continue;
        }
        let text = string(name.as_ptr().cast());
        if directory {
            if family_exists(io, text) {
                continue;
            }
            (io.begin_family)(io.ctx, root, name.as_ptr(), text.len(), false);
            let mut dir = [0u8; 160];
            path(
                root,
                name.as_ptr().cast(),
                name.as_ptr().cast(),
                0,
                true,
                dir.as_mut_ptr(),
                dir.len(),
            );
            scan_directory(io, root, name.as_ptr().cast(), dir.as_ptr().cast());
            (io.end_family)(io.ctx);
        } else if io.vectors {
            let Some(base) = vector_name(text) else {
                continue;
            };
            if family_exists(io, &text[..base]) {
                continue;
            }
            (io.begin_family)(io.ctx, root, name.as_ptr(), base, true);
            if !(io.add_file)(io.ctx, true, name.as_ptr().cast(), 0, 0, false) {
                (io.end_family)(io.ctx);
                continue;
            }
            (io.commit_family)(io.ctx, true, ptr::null());
            name[base] = 0;
            (io.diagnostic)(io.ctx, 5, name.as_ptr().cast(), root, 0);
            (io.end_family)(io.ctx);
        }
    }
    (io.close)(io.ctx, true);
}

/// # Safety
/// Same file-array loan contract as nearest; emit only appends an output size.
pub unsafe fn sizes(
    files: *const File,
    count: usize,
    ctx: *mut c_void,
    emit: unsafe extern "C" fn(*mut c_void, u8),
) {
    let mut seen = [false; 256];
    for i in 0..count {
        seen[usize::from((*files.add(i)).size)] = true;
    }
    for (size, &present) in seen.iter().enumerate() {
        if present {
            emit(ctx, size as u8);
        }
    }
}
/// # Safety
/// Getter returns stable terminated names; target contains length readable bytes.
pub unsafe fn find_family(
    ctx: *const c_void,
    count: usize,
    get: unsafe extern "C" fn(*const c_void, usize) -> *const c_char,
    target: &[u8],
) -> i32 {
    for i in 0..count {
        if string(get(ctx, i)) == target {
            return i as i32;
        }
    }
    -1
}
/// # Safety
/// Roots are terminated and static; the synchronous exists callback retains no path.
pub unsafe fn root(
    family: *const c_char,
    hidden: *const c_char,
    visible: *const c_char,
    exists: unsafe extern "C" fn(*const c_char) -> bool,
    write: bool,
) -> *const c_char {
    if write {
        let hidden_exists = exists(hidden);
        let visible_exists = exists(visible);
        return if hidden_exists {
            hidden
        } else if visible_exists {
            visible
        } else {
            hidden
        };
    }
    if family.is_null() || *family == 0 {
        return ptr::null();
    }
    let mut buf = [0u8; 160];
    for root in [hidden, visible] {
        path(root, family, family, 0, true, buf.as_mut_ptr(), buf.len());
        if exists(buf.as_ptr().cast()) {
            return root;
        }
    }
    ptr::null()
}

/// # Safety
/// Same file-array loan contract as nearest.
pub unsafe fn has_size(files: *const File, count: usize, size: u8) -> bool {
    (0..count).any(|i| (*files.add(i)).size == size)
}

#[repr(C)]
pub struct Registry {
    pub ctx: *mut c_void,
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub scan: unsafe extern "C" fn(*mut c_void, *const c_char),
    pub sort: unsafe extern "C" fn(*mut c_void) -> usize,
    pub resize: unsafe extern "C" fn(*mut c_void, usize),
    pub finish: unsafe extern "C" fn(*mut c_void) -> usize,
}
/// # Safety
/// Registry owns its family container; scans are synchronous and roots are static.
pub unsafe fn discover(
    io: &Registry,
    hidden: *const c_char,
    visible: *const c_char,
    maximum: usize,
) -> bool {
    (io.clear)(io.ctx);
    (io.scan)(io.ctx, hidden);
    (io.scan)(io.ctx, visible);
    if (io.sort)(io.ctx) > maximum {
        (io.resize)(io.ctx, maximum);
    }
    (io.finish)(io.ctx) != 0
}
