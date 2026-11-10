//! Dormant catalog owner; existing discovery policy calls only Rust-owned records.
use super::{
    catalog_storage::{Array, Name},
    parser::Allocator,
};
use crate::font::registry;
use core::{
    ffi::{c_char, c_void, CStr},
    mem, ptr, slice,
};

const NAME_OOM: u32 = 6;
const BAD_PATH: u32 = 7;
const PROMOTED: u32 = 8;
const SELECTED: u32 = 9;
const COMPLETE: u32 = 10;

/// # Safety
/// Names are terminated within their 128-byte output. Calls do not unwind,
/// retain loans, or reenter the catalog. inspect returns library metadata only.
pub unsafe trait Directory {
    fn open(&mut self, path: &CStr, root: bool) -> i32;
    fn next(&mut self, root: bool, name: &mut [u8; 128], directory: &mut bool) -> bool;
    fn close(&mut self, root: bool);
    fn inspect(&mut self, path: &CStr) -> Option<(u16, bool)>;
    fn log(&mut self, event: u32, a: &CStr, b: Option<&CStr>, n: usize, total: usize);
}
#[repr(C)]
pub struct File {
    name: *mut c_char,
    pub size: u8,
    pub style: u8,
}
impl File {
    pub fn name(&self) -> Option<&CStr> {
        (!self.name.is_null()).then(|| unsafe { CStr::from_ptr(self.name) })
    }
    fn release(&mut self, memory: &Allocator) {
        if !self.name.is_null() {
            unsafe { (memory.free_resident)(self.name.cast(), false) };
            self.name = ptr::null_mut();
        }
    }
}
#[repr(C)]
pub struct Family {
    name: Name,
    files: Array<File>,
    root: *const c_char,
    pub vector: bool,
    pub loose: bool,
}
impl Family {
    fn new() -> Self {
        Self {
            name: Name::new(),
            files: Array::new(),
            root: ptr::null(),
            vector: false,
            loose: false,
        }
    }
    pub fn name(&self) -> &CStr {
        self.name.cstr()
    }
    pub fn files(&self) -> &[File] {
        self.files.values()
    }
    pub fn nearest(&self, size: u8, exact: bool) -> Option<&File> {
        let index = unsafe { registry::nearest(&file_view(&self.files), size, 0, exact) };
        (index >= 0).then(|| &self.files.values()[index as usize])
    }
    pub fn path(&self, file: &File, output: &mut [u8]) -> bool {
        unsafe {
            registry::path(
                self.root,
                self.name.cstr().as_ptr(),
                file.name,
                file.size,
                self.loose,
                output.as_mut_ptr(),
                output.len(),
            )
        }
    }
    fn release(&mut self, memory: &Allocator) {
        release_files(&mut self.files, memory);
        self.name.release(memory);
    }
}
fn release_files(files: &mut Array<File>, memory: &Allocator) {
    for file in files.values_mut() {
        file.release(memory);
    }
    files.release(memory);
}
unsafe extern "C" fn get_file(raw: *const c_void, index: usize) -> registry::File {
    let file = &(*raw.cast::<Array<File>>()).values()[index];
    registry::File {
        name: file.name,
        size: file.size,
        style: file.style,
    }
}
fn file_view(files: &Array<File>) -> registry::Files {
    registry::Files {
        ctx: (files as *const Array<File>).cast(),
        count: files.values().len(),
        get: get_file,
    }
}
pub struct Catalog<'a> {
    families: Array<Family>,
    memory: &'a Allocator,
}
impl<'a> Catalog<'a> {
    /// # Safety
    /// styles/free_styles pair aligned internal container allocations; resident/
    /// free_resident pair fallible internal filenames (not PSRAM). Their failure
    /// behavior is supplied by the binding; no Rust abort or allocator is added.
    pub const unsafe fn new(memory: &'a Allocator) -> Self {
        Self {
            families: Array::new(),
            memory,
        }
    }
    pub fn families(&self) -> &[Family] {
        self.families.values()
    }
    pub fn find(&self, name: &CStr) -> Option<&Family> {
        self.families().iter().find(|f| f.name() == name)
    }
    pub fn discover<P: Directory>(
        &mut self,
        physical: &mut P,
        vectors: bool,
    ) -> Result<bool, Allocation> {
        self.clear();
        for root in [c"/.fonts", c"/fonts"] {
            let mut context = Scan {
                catalog: self,
                physical,
                family: Family::new(),
                cp: Array::new(),
                vectors: Array::new(),
                failed: false,
            };
            let io = discovery(&mut context, vectors);
            unsafe { registry::scan_root(&io, root.as_ptr()) };
            context.finish_family();
            if context.failed {
                return Err(Allocation);
            }
        }
        self.families
            .values_mut()
            .sort_unstable_by(|a, b| a.name.bytes().cmp(b.name.bytes()));
        if self.families.values().len() > 128 {
            for family in &mut self.families.values_mut()[128..] {
                family.release(self.memory);
            }
            self.families.truncate(128);
        }
        if !self.families.shrink(self.memory) {
            return Err(Allocation);
        }
        physical.log(COMPLETE, c"", None, self.families.values().len(), 0);
        Ok(!self.families.values().is_empty())
    }
    fn clear(&mut self) {
        for family in self.families.values_mut() {
            family.release(self.memory);
        }
        self.families.clear();
    }
}
#[derive(Debug, PartialEq, Eq)]
pub struct Allocation;
impl Drop for Catalog<'_> {
    fn drop(&mut self) {
        self.clear();
        self.families.release(self.memory);
    }
}

struct Scan<'a, 'm, P> {
    catalog: &'a mut Catalog<'m>,
    physical: &'a mut P,
    family: Family,
    cp: Array<File>,
    vectors: Array<File>,
    failed: bool,
}
impl<P: Directory> Scan<'_, '_, P> {
    fn finish_family(&mut self) {
        // Native endFamily resets the name, then discards vector before bitmap candidates.
        self.family.name.release(self.catalog.memory);
        release_files(&mut self.family.files, self.catalog.memory);
        release_files(&mut self.vectors, self.catalog.memory);
        release_files(&mut self.cp, self.catalog.memory);
        self.family = Family::new();
    }
    fn add(&mut self, vector: bool, name: &CStr, size: u8, style: u8, canonical: bool) -> bool {
        if self.failed {
            return false;
        }
        let mut file = File {
            name: ptr::null_mut(),
            size,
            style,
        };
        if !canonical {
            let bytes = name.to_bytes_with_nul();
            file.name = unsafe { (self.catalog.memory.resident)(bytes.len()).cast() };
            if file.name.is_null() {
                self.physical.log(NAME_OOM, name, None, bytes.len(), 0);
                return false;
            }
            unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), file.name.cast(), bytes.len()) };
        }
        let mut path = [0; 160];
        if !self.family.path(&file, &mut path) {
            let directory = if self.family.loose {
                unsafe { CStr::from_ptr(self.family.root) }
            } else {
                unsafe {
                    registry::path(
                        self.family.root,
                        self.family.name().as_ptr(),
                        self.family.name().as_ptr(),
                        0,
                        true,
                        path.as_mut_ptr(),
                        path.len(),
                    );
                    CStr::from_ptr(path.as_ptr().cast())
                }
            };
            self.physical.log(BAD_PATH, name, Some(directory), 0, 0);
            file.release(self.catalog.memory);
            return false;
        }
        let files = if vector {
            &mut self.vectors
        } else {
            &mut self.cp
        };
        match files.push(file, self.catalog.memory) {
            Ok(()) => true,
            Err(mut file) => {
                file.release(self.catalog.memory);
                self.failed = true;
                false
            }
        }
    }
    fn refine(&mut self, dir: &CStr) -> bool {
        let memory = self.catalog.memory;
        let mut candidates = Array::<registry::Candidate>::new();
        if !candidates.reserve_exact(self.vectors.values().len(), memory) {
            return false;
        }
        for (index, file) in self.vectors.values().iter().enumerate() {
            let mut path = [0; 160];
            let name = file.name().expect("vector filename");
            unsafe {
                registry::path(
                    dir.as_ptr(),
                    c"".as_ptr(),
                    name.as_ptr(),
                    0,
                    true,
                    path.as_mut_ptr(),
                    path.len(),
                )
            };
            let metadata = self
                .physical
                .inspect(unsafe { CStr::from_ptr(path.as_ptr().cast()) });
            let (weight, italic) = metadata.unwrap_or((
                if file.style & 1 != 0 { 700 } else { 400 },
                file.style & 2 != 0,
            ));
            if candidates
                .push(
                    registry::Candidate {
                        index,
                        weight,
                        italic,
                    },
                    memory,
                )
                .is_err()
            {
                candidates.release(memory);
                return false;
            }
        }
        let mut indices = [-1; 4];
        let promoted = unsafe {
            registry::select_faces(
                candidates.values().as_ptr(),
                candidates.values().len(),
                &file_view(&self.vectors),
                indices.as_mut_ptr(),
            )
        };
        if indices[0] < 0 {
            candidates.release(memory);
            return true;
        }
        if promoted {
            self.physical.log(
                PROMOTED,
                dir,
                self.vectors.values()[indices[0] as usize].name(),
                0,
                0,
            );
        }
        let mut selected = Array::new();
        if !selected.reserve_exact(4, memory) {
            candidates.release(memory);
            return false;
        }
        for (role, index) in indices.into_iter().enumerate() {
            if index < 0 {
                continue;
            }
            let mut file = mem::replace(
                &mut self.vectors.values_mut()[index as usize],
                File {
                    name: ptr::null_mut(),
                    size: 0,
                    style: 0,
                },
            );
            file.style = role as u8;
            if let Err(mut file) = selected.push(file, memory) {
                file.release(memory);
                release_files(&mut selected, memory);
                candidates.release(memory);
                return false;
            }
        }
        if selected.values().len() < self.vectors.values().len() {
            self.physical.log(
                SELECTED,
                dir,
                None,
                selected.values().len(),
                self.vectors.values().len(),
            );
        }
        release_files(&mut self.vectors, memory);
        self.vectors = selected;
        candidates.release(memory);
        true
    }
}
fn discovery<P: Directory>(context: &mut Scan<'_, '_, P>, vectors: bool) -> registry::Discovery {
    registry::Discovery {
        ctx: (context as *mut Scan<'_, '_, P>).cast(),
        vectors,
        open: open::<P>,
        next: next::<P>,
        close: close::<P>,
        families: count::<P>,
        family_name: family_name::<P>,
        begin_family: begin::<P>,
        files: files::<P>,
        add_file: add::<P>,
        commit_family: commit::<P>,
        diagnostic: log::<P>,
        end_family: end::<P>,
    }
}
unsafe fn context<'a, 'm, P>(raw: *mut c_void) -> &'a mut Scan<'a, 'm, P> {
    &mut *raw.cast()
}
unsafe extern "C" fn open<P: Directory>(raw: *mut c_void, path: *const c_char, root: bool) -> i32 {
    let c = context::<P>(raw);
    if c.failed {
        0
    } else {
        c.physical.open(CStr::from_ptr(path), root)
    }
}
unsafe extern "C" fn next<P: Directory>(
    raw: *mut c_void,
    root: bool,
    name: *mut u8,
    dir: *mut bool,
) -> bool {
    let c = context::<P>(raw);
    !c.failed
        && c.physical
            .next(root, &mut *name.cast::<[u8; 128]>(), &mut *dir)
}
unsafe extern "C" fn close<P: Directory>(raw: *mut c_void, root: bool) {
    context::<P>(raw).physical.close(root);
}
unsafe extern "C" fn count<P>(raw: *mut c_void) -> usize {
    context::<P>(raw).catalog.families().len()
}
unsafe extern "C" fn family_name<P>(raw: *mut c_void, index: usize) -> *const c_char {
    context::<P>(raw).catalog.families()[index].name().as_ptr()
}
unsafe extern "C" fn begin<P>(
    raw: *mut c_void,
    root: *const c_char,
    name: *const u8,
    len: usize,
    loose: bool,
) {
    let c = context::<P>(raw);
    c.failed = !c
        .family
        .name
        .assign(slice::from_raw_parts(name, len), c.catalog.memory);
    c.family.root = root;
    c.family.loose = loose;
    c.family.vector = loose;
}
unsafe extern "C" fn files<P>(raw: *mut c_void, vector: bool) -> registry::Files {
    let c = context::<P>(raw);
    file_view(if vector { &c.vectors } else { &c.cp })
}
unsafe extern "C" fn add<P: Directory>(
    raw: *mut c_void,
    vector: bool,
    name: *const c_char,
    size: u8,
    style: u8,
    canonical: bool,
) -> bool {
    context::<P>(raw).add(vector, CStr::from_ptr(name), size, style, canonical)
}
unsafe extern "C" fn commit<P: Directory>(
    raw: *mut c_void,
    vector: bool,
    dir: *const c_char,
) -> usize {
    let c = context::<P>(raw);
    if vector && !dir.is_null() && !c.refine(CStr::from_ptr(dir)) {
        c.failed = true;
        return 0;
    }
    c.family.vector = vector;
    c.family.files = mem::replace(
        if vector { &mut c.vectors } else { &mut c.cp },
        Array::new(),
    );
    release_files(
        if vector { &mut c.cp } else { &mut c.vectors },
        c.catalog.memory,
    );
    let family = mem::replace(&mut c.family, Family::new());
    let count = family.files().len();
    if let Err(mut family) = c.catalog.families.push(family, c.catalog.memory) {
        family.release(c.catalog.memory);
        c.failed = true;
        return 0;
    }
    count
}
unsafe extern "C" fn log<P: Directory>(
    raw: *mut c_void,
    event: u32,
    a: *const c_char,
    b: *const c_char,
    n: usize,
) {
    context::<P>(raw).physical.log(
        event,
        CStr::from_ptr(a),
        (!b.is_null()).then(|| CStr::from_ptr(b)),
        n,
        0,
    );
}
unsafe extern "C" fn end<P: Directory>(raw: *mut c_void) {
    context::<P>(raw).finish_family();
}
const _: () = {
    assert!(mem::size_of::<File>() == 2 * mem::size_of::<usize>());
    assert!(!cfg!(target_pointer_width = "32") || mem::size_of::<Family>() == 44);
};
