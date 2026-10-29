//! SD font registration and ownership policy over the renderer's existing maps.
use super::registry::{self, File, Files};
use core::ffi::{c_char, c_void, CStr};
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Loaded {
    pub font: *mut c_void,
    pub id: i32,
    pub size: u8,
}
#[repr(C)]
pub struct View {
    pub name: *const c_char,
    pub name_len: usize,
    pub entries: *const Loaded,
    pub count: usize,
}
#[repr(C)]
pub struct Family {
    pub name: *const c_char,
    pub name_len: usize,
    pub root: *const c_char,
    pub files: Files,
    pub loose: bool,
}
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub view: unsafe extern "C" fn(*mut c_void) -> View,
    pub create: unsafe extern "C" fn() -> *mut c_void,
    pub load: unsafe extern "C" fn(*mut c_void, *const c_char) -> bool,
    pub destroy: unsafe extern "C" fn(*mut c_void),
    pub info: unsafe extern "C" fn(*mut c_void, *mut u8) -> u32,
    pub collision: unsafe extern "C" fn(*mut c_void, i32) -> bool,
    pub register_sd: unsafe extern "C" fn(*mut c_void, i32, *mut c_void),
    pub push: unsafe extern "C" fn(*mut c_void, Loaded),
    pub insert_font: unsafe extern "C" fn(*mut c_void, i32, *mut c_void),
    pub remove_font: unsafe extern "C" fn(*mut c_void, i32),
    pub erase: unsafe extern "C" fn(*mut c_void, usize),
    pub clear_fallbacks: unsafe extern "C" fn(*mut c_void),
    pub clear_sd: unsafe extern "C" fn(*mut c_void),
    pub clear: unsafe extern "C" fn(*mut c_void),
    pub family: unsafe extern "C" fn(*mut c_void, *const c_char, usize, u8),
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, *const c_char, i32, u32, u32),
}
unsafe fn name<'a>(p: *const c_char) -> &'a [u8] {
    CStr::from_ptr(p).to_bytes()
}
/// # Safety
/// Services borrow the manager; all pointers in each view remain live until its
/// next mutation. The renderer must retain no font pointer after removal.
pub unsafe fn destroy(io: &Services) {
    let view = (io.view)(io.ctx);
    for i in 0..view.count {
        (io.destroy)((*view.entries.add(i)).font);
    }
}
/// # Safety
/// Same ownership contract as destroy, with a live renderer.
pub unsafe fn unload(io: &Services, id: Option<i32>) {
    if let Some(id) = id {
        let view = (io.view)(io.ctx);
        for i in 0..view.count {
            let entry = *view.entries.add(i);
            if entry.id != id {
                continue;
            }
            (io.remove_font)(io.ctx, id);
            (io.destroy)(entry.font);
            (io.erase)(io.ctx, i);
            return;
        }
    } else {
        (io.clear_fallbacks)(io.ctx);
        (io.clear_sd)(io.ctx);
        let view = (io.view)(io.ctx);
        for i in 0..view.count {
            let entry = *view.entries.add(i);
            (io.remove_font)(io.ctx, entry.id);
            (io.destroy)(entry.font);
        }
        (io.clear)(io.ctx);
    }
}
/// # Safety
/// Same manager view contract as destroy; name is readable for len bytes.
pub unsafe fn get(io: &Services, family: *const c_char, len: usize, size: u8) -> i32 {
    let view = (io.view)(io.ctx);
    if core::slice::from_raw_parts(family.cast::<u8>(), len)
        != core::slice::from_raw_parts(view.name.cast::<u8>(), view.name_len)
        || view.count == 0
    {
        return 0;
    }
    if size == 0 {
        return (*view.entries).id;
    }
    for i in 0..view.count {
        let entry = &*view.entries.add(i);
        if entry.size == size {
            return entry.id;
        }
    }
    0
}
unsafe fn load_file(io: &Services, family: &Family, file: &File, override_id: i32) -> i32 {
    let mut path = [0u8; 160];
    if !registry::path(
        family.root,
        family.name,
        file.name,
        file.size,
        family.loose,
        path.as_mut_ptr(),
        path.len(),
    ) {
        (io.diagnostic)(io.ctx, 1, family.name, 0, 0, 0);
        return 0;
    }
    let font = (io.create)();
    if font.is_null() {
        (io.diagnostic)(io.ctx, 2, path.as_ptr().cast(), 0, 0, 0);
        return 0;
    }
    if !(io.load)(font, path.as_ptr().cast()) {
        (io.diagnostic)(io.ctx, 3, path.as_ptr().cast(), 0, 0, 0);
        (io.destroy)(font);
        return 0;
    }
    let mut styles = 0;
    let hash = (io.info)(font, &mut styles);
    let id = if override_id == 0 {
        registry::font_id(hash, name(family.name), file.size)
    } else {
        override_id
    };
    if (io.collision)(io.ctx, id) {
        (io.diagnostic)(io.ctx, 4, path.as_ptr().cast(), id, 0, 0);
        (io.destroy)(font);
        return 0;
    }
    (io.register_sd)(io.ctx, id, font);
    (io.push)(
        io.ctx,
        Loaded {
            font,
            id,
            size: file.size,
        },
    );
    (io.diagnostic)(
        io.ctx,
        5,
        path.as_ptr().cast(),
        id,
        u32::from(file.size),
        u32::from(styles),
    );
    (io.insert_font)(io.ctx, id, font);
    id
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
pub enum Mode {
    Replace,
    ExtraSize,
    Variant,
}

/// # Safety
/// Family and file pointers are stable throughout the synchronous operation.
pub unsafe fn load(io: &Services, family: &Family, size: u8, id: i32, mode: Mode) -> i32 {
    if mode == Mode::Replace && (io.view)(io.ctx).name_len != 0 {
        unload(io, None);
    }
    let index = registry::nearest(&family.files, size, 0, mode != Mode::Replace);
    if index < 0 {
        if mode == Mode::Replace {
            (io.diagnostic)(io.ctx, 6, family.name, 0, 0, 0);
        }
        return 0;
    }
    if mode == Mode::ExtraSize {
        let loaded = get(io, family.name, family.name_len, size);
        if loaded != 0 {
            return loaded;
        }
    }
    let file = family.files.at(index as usize);
    let loaded = load_file(
        io,
        family,
        &file,
        if mode == Mode::Replace { 0 } else { id },
    );
    if mode == Mode::Replace && loaded != 0 {
        (io.family)(io.ctx, family.name, family.name_len, file.size);
    }
    loaded
}
