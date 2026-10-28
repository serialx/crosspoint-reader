extern crate alloc;
use alloc::vec::Vec;
use core::{
    ffi::c_void,
    marker::PhantomData,
    mem::size_of,
    ops::{Deref, DerefMut},
    ptr, slice,
};
use xp_core::library::{
    format::{self, Header, Record},
    reader::{self, Read},
    text,
};

const INDEX: &[u8] = b"/.crosspoint/library.idx\0";
const NEW: &[u8] = b"/.crosspoint/library.new\0";
const BACKUP: &[u8] = b"/.crosspoint/library.bak\0";
const STAGE: &[u8] = b"/.crosspoint/library.stage\0";
const FOLDERS: &[u8] = b"/.crosspoint/library.stage.f\0";
const DIRTY: &[u8] = b"/.crosspoint/library.dirty\0";
const CACHE: &[u8] = b"/.crosspoint\0";
#[repr(C)]
pub struct Services {
    pub ctx: *mut c_void,
    pub file: unsafe extern "C" fn(*mut c_void, u8, u32, *const u8, *mut u8, usize, u64) -> i64,
    pub storage: unsafe extern "C" fn(*mut c_void, u8, *const u8, *const u8) -> u8,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
    pub release: unsafe extern "C" fn(*mut c_void, *mut u8),
    pub clock: unsafe extern "C" fn(*mut c_void, u8) -> u32,
    pub metadata: unsafe extern "C" fn(
        *mut c_void,
        *const u8,
        u8,
        *mut *mut u8,
        *mut usize,
        *mut *const u8,
        *mut usize,
    ) -> u8,
    pub diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32, *const u8)>,
}
struct MetadataLoan<'a>(&'a Services);
impl Drop for MetadataLoan<'_> {
    fn drop(&mut self) {
        unsafe {
            (self.0.metadata)(
                self.0.ctx,
                ptr::null(),
                2,
                ptr::null_mut(),
                ptr::null_mut(),
                ptr::null_mut(),
                ptr::null_mut(),
            );
        }
    }
}
#[derive(Default)]
#[repr(C)]
pub struct Stats {
    pub books: u16,
    pub folders: u16,
    pub duplicates: u16,
    pub unreadable: u16,
    pub walk_ms: u32,
    pub unchanged: u16,
    pub added: u16,
    pub renamed: u16,
    pub removed: u16,
    pub enriched: u16,
    pub parsed: u16,
    pub reused: u16,
    pub replaced: bool,
    pub ranks_degraded: bool,
    pub dedup_degraded: bool,
}
impl Services {
    fn op(&self, op: u8, id: u32, path: &[u8], data: *mut u8, len: usize, arg: u64) -> i64 {
        unsafe { (self.file)(self.ctx, op, id, path.as_ptr(), data, len, arg) }
    }
    fn storage(&self, op: u8, path: &[u8], other: &[u8]) -> bool {
        unsafe { (self.storage)(self.ctx, op, path.as_ptr(), other.as_ptr()) != 0 }
    }
    #[inline(never)]
    fn exists(&self, path: &[u8]) -> bool {
        self.storage(0, path, b"\0")
    }
    #[inline(never)]
    fn remove(&self, path: &[u8]) -> bool {
        self.storage(1, path, b"\0")
    }
    #[inline(never)]
    fn rename(&self, a: &[u8], b: &[u8]) -> bool {
        self.storage(3, a, b)
    }
    fn now(&self) -> u32 {
        unsafe { (self.clock)(self.ctx, 0) }
    }
    fn delay(&self) {
        unsafe {
            (self.clock)(self.ctx, 1);
        }
    }
    #[inline(never)]
    fn service(&self, n: &mut u32) {
        *n = n.wrapping_add(1);
        if *n & 31 == 0 {
            self.delay();
        }
    }
    #[inline(never)]
    fn log(&self, event: u32, a: u32, b: u32, c: u32, d: u32, path: &[u8]) {
        if let Some(f) = self.diagnostic {
            unsafe {
                f(self.ctx, event, a, b, c, d, path.as_ptr());
            }
        }
    }
    #[inline(never)]
    fn event(&self, event: u32) {
        self.log(event, 0, 0, 0, 0, b"\0");
    }
}
struct Buffer<'a, T> {
    p: *mut T,
    n: usize,
    s: &'a Services,
    _marker: PhantomData<T>,
}
impl<'a, T> Buffer<'a, T> {
    fn new(s: &'a Services, n: usize) -> Option<Self> {
        let bytes = n.checked_mul(size_of::<T>())?;
        let p: *mut T = unsafe { (s.allocate)(s.ctx, bytes.max(1)) }.cast();
        if p.is_null() {
            None
        } else {
            Some(Self {
                p,
                n,
                s,
                _marker: PhantomData,
            })
        }
    }
}
impl<T> Deref for Buffer<'_, T> {
    type Target = [T];
    fn deref(&self) -> &[T] {
        unsafe { slice::from_raw_parts(self.p, self.n) }
    }
}
impl<T> DerefMut for Buffer<'_, T> {
    fn deref_mut(&mut self) -> &mut [T] {
        unsafe { slice::from_raw_parts_mut(self.p, self.n) }
    }
}
impl<T> Drop for Buffer<'_, T> {
    fn drop(&mut self) {
        unsafe {
            (self.s.release)(self.s.ctx, self.p.cast());
        }
    }
}
struct File<'a> {
    s: &'a Services,
    id: u32,
}
impl<'a> File<'a> {
    #[inline(never)]
    fn open(s: &'a Services, path: &[u8], mode: u8) -> Self {
        Self {
            s,
            id: s.op(mode, 0, path, ptr::null_mut(), 0, 0) as u32,
        }
    }
    fn valid(&self) -> bool {
        self.id != 0
    }
    #[inline(never)]
    fn close(&mut self) -> bool {
        if self.id == 0 {
            return true;
        }
        let id = self.id;
        self.id = 0;
        self.s.op(4, id, b"\0", ptr::null_mut(), 0, 0) != 0
    }
    #[inline(never)]
    fn read(&self, out: &mut [u8]) -> i64 {
        self.s.op(5, self.id, b"\0", out.as_mut_ptr(), out.len(), 0)
    }
    #[inline(never)]
    fn write(&self, data: &[u8]) -> bool {
        self.s
            .op(6, self.id, b"\0", data.as_ptr().cast_mut(), data.len(), 0)
            == data.len() as i64
    }
    #[inline(never)]
    fn seek(&self, at: u64) -> bool {
        self.s.op(7, self.id, b"\0", ptr::null_mut(), 0, at) != 0
    }
    #[inline(never)]
    fn value(&self, op: u8) -> u64 {
        self.s.op(op, self.id, b"\0", ptr::null_mut(), 0, 0) as u64
    }
    #[inline(never)]
    fn at(&self, at: u64, out: &mut [u8]) -> bool {
        self.seek(at) && self.read(out) == out.len() as i64
    }
    #[inline(never)]
    fn next(&self) -> Self {
        Self {
            s: self.s,
            id: self.value(3) as u32,
        }
    }
}
impl Drop for File<'_> {
    fn drop(&mut self) {
        self.close();
    }
}
fn pod<T>(value: &T) -> &[u8] {
    unsafe { slice::from_raw_parts(ptr::from_ref(value).cast(), size_of::<T>()) }
}
fn pod_mut<T>(value: &mut T) -> &mut [u8] {
    unsafe { slice::from_raw_parts_mut(ptr::from_mut(value).cast(), size_of::<T>()) }
}
struct Writer<'a, 'b> {
    file: &'b File<'a>,
    buffer: Option<Buffer<'a, u8>>,
    fill: usize,
    pos: usize,
    ok: bool,
}
impl<'a, 'b> Writer<'a, 'b> {
    fn new(file: &'b File<'a>) -> Self {
        Self {
            buffer: Buffer::new(file.s, 4096),
            fill: 0,
            pos: file.value(8) as usize,
            ok: true,
            file,
        }
    }
    fn flush(&mut self) -> bool {
        if self.fill > 0 {
            self.ok &= self
                .buffer
                .as_ref()
                .and_then(|buffer| buffer.get(..self.fill))
                .is_some_and(|bytes| self.file.write(bytes));
            self.fill = 0;
        }
        self.ok
    }
    fn put(&mut self, data: &[u8]) {
        self.pos += data.len();
        let cap = self.buffer.as_ref().map_or(0, |b| b.len());
        if self.fill + data.len() > cap {
            self.flush();
        }
        if data.len() >= cap {
            self.ok &= self.file.write(data);
        } else if let Some(b) = self.buffer.as_mut() {
            b[self.fill..self.fill + data.len()].copy_from_slice(data);
            self.fill += data.len();
        }
    }
    fn pad(&mut self, at: u32, units: &mut u32) {
        static ZERO: [u8; 64] = [0; 64];
        while self.pos < (at as usize) {
            self.file.s.service(units);
            self.put(&ZERO[..(at as usize - self.pos).min(64)]);
        }
    }
}
impl Drop for Writer<'_, '_> {
    fn drop(&mut self) {
        self.flush();
    }
}
// C3 short strings plus their optional C terminator stay inline.
// Long paths and metadata use checked, phase-local storage.
enum Text {
    Inline { bytes: [u8; 16], len: u8 },
    Heap(Vec<u8>),
}
impl Text {
    fn new() -> Self {
        Self::Inline {
            bytes: [0; 16],
            len: 0,
        }
    }
    fn try_reserve(&mut self, additional: usize) -> Result<(), ()> {
        let required = self.len().checked_add(additional).ok_or(())?;
        match self {
            Self::Inline { .. } if required <= 16 => Ok(()),
            Self::Inline { bytes, len } => {
                let mut v = Vec::new();
                v.try_reserve_exact(required).map_err(|_| ())?;
                v.extend_from_slice(&bytes[..usize::from(*len)]);
                *self = Self::Heap(v);
                Ok(())
            }
            Self::Heap(v) => v.try_reserve_exact(additional).map_err(|_| ()),
        }
    }
    fn extend_from_slice(&mut self, data: &[u8]) {
        match self {
            Self::Inline { bytes, len } => {
                let start = usize::from(*len);
                bytes[start..start + data.len()].copy_from_slice(data);
                *len += data.len() as u8;
            }
            Self::Heap(v) => v.extend_from_slice(data),
        }
    }
    fn push(&mut self, byte: u8) {
        self.extend_from_slice(&[byte]);
    }
    fn truncate(&mut self, n: usize) {
        match self {
            Self::Inline { len, .. } => *len = usize::from(*len).min(n) as u8,
            Self::Heap(v) => v.truncate(n),
        }
    }
    fn clear(&mut self) {
        self.truncate(0);
    }
    fn resize(&mut self, n: usize, value: u8) {
        match self {
            Self::Inline { bytes, len } => {
                bytes[usize::from(*len)..n].fill(value);
                *len = n as u8;
            }
            Self::Heap(v) => v.resize(n, value),
        }
    }
}
impl Deref for Text {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        match self {
            Self::Inline { bytes, len } => &bytes[..usize::from(*len)],
            Self::Heap(v) => v,
        }
    }
}
impl DerefMut for Text {
    fn deref_mut(&mut self) -> &mut [u8] {
        match self {
            Self::Inline { bytes, len } => &mut bytes[..usize::from(*len)],
            Self::Heap(v) => v,
        }
    }
}
// One iterative heapsort core serves all record types without allocating.
trait SortEntries {
    fn less(&mut self, a: usize, b: usize) -> bool;
    fn swap(&mut self, a: usize, b: usize);
}
#[inline(never)]
fn sort_entries(items: &mut dyn SortEntries, len: usize) {
    fn sift(items: &mut dyn SortEntries, mut root: usize, len: usize) {
        loop {
            let mut child = root * 2 + 1;
            if child >= len {
                return;
            }
            if child + 1 < len && items.less(child, child + 1) {
                child += 1;
            }
            if !items.less(root, child) {
                return;
            }
            items.swap(root, child);
            root = child;
        }
    }
    for root in (0..len / 2).rev() {
        sift(items, root, len);
    }
    for end in (1..len).rev() {
        items.swap(0, end);
        sift(items, 0, end);
    }
}
fn sort_by<T>(items: &mut [T], compare: impl FnMut(&T, &T) -> core::cmp::Ordering) {
    struct Entries<'a, T, F> {
        items: &'a mut [T],
        compare: F,
    }
    impl<T, F: FnMut(&T, &T) -> core::cmp::Ordering> SortEntries for Entries<'_, T, F> {
        fn less(&mut self, a: usize, b: usize) -> bool {
            (self.compare)(&self.items[a], &self.items[b]).is_lt()
        }
        fn swap(&mut self, a: usize, b: usize) {
            self.items.swap(a, b);
        }
    }
    let len = items.len();
    sort_entries(&mut Entries { items, compare }, len);
}
#[inline(never)]
fn copy(data: &[u8]) -> Option<Text> {
    let mut v = Text::new();
    v.try_reserve(data.len()).ok()?;
    v.extend_from_slice(data);
    Some(v)
}
fn path_join(folder: &[u8], name: &[u8]) -> Option<Text> {
    let folder = folder.strip_suffix(&[0]).unwrap_or(folder);
    let mut v = Text::new();
    let separator = usize::from(folder.last() != Some(&b'/'));
    v.try_reserve(folder.len() + name.len() + separator + 1)
        .ok()?;
    v.extend_from_slice(folder);
    if folder.last() != Some(&b'/') {
        v.push(b'/');
    }
    v.extend_from_slice(name);
    v.push(0);
    Some(v)
}
#[inline(never)]
fn transformed(data: &[u8], f: fn(&mut [u8]) -> usize) -> Option<Text> {
    let mut b = copy(data)?;
    let n = f(&mut b);
    b.truncate(n);
    Some(b)
}
fn ext(name: &[u8], suffix: &[u8]) -> bool {
    name.len() >= suffix.len() && name[name.len() - suffix.len()..].eq_ignore_ascii_case(suffix)
}
fn hidden(name: &[u8]) -> bool {
    name.first() == Some(&b'.') || name == b"System Volume Information" || name == b"XTCache"
}
pub fn book(path: &[u8]) -> bool {
    !path.split(|&c| c == b'/').any(|p| p != b"." && hidden(p))
        && [b".epub".as_slice(), b".txt", b".md", b".xtc", b".xtch"]
            .iter()
            .any(|e| ext(path, e))
}
struct Index<'a> {
    file: File<'a>,
    head: Header,
    failed: bool,
    opened: bool,
    output: Text,
}
impl<'a> Index<'a> {
    #[inline(never)]
    fn open(s: &'a Services, path: &[u8], stale: bool) -> Self {
        let file = File::open(s, path, 0);
        let mut v = Self {
            file,
            head: Header::default(),
            failed: false,
            opened: false,
            output: Text::new(),
        };
        if !v.file.valid() {
            v.failed = true;
            return v;
        }
        if v.file.read(pod_mut(&mut v.head)) != 64 {
            v.failed = true;
            v.file.close();
            return v;
        }
        let status = format::validate(&v.head, v.file.value(9), stale);
        if status != 0 {
            s.log(60, u32::from(status), 0, 0, 0, b"\0");
            v.file.close();
            return v;
        }
        v.opened = true;
        v
    }
    fn record(&mut self, n: u16, r: &mut Record) -> bool {
        {
            let h = self.head;
            reader::record(&h, self, n, r)
        }
    }
    fn hash(&mut self, r: &Record) -> Option<u64> {
        {
            let h = self.head;
            reader::hash(&h, self, r)
        }
    }
    fn blob(&mut self, r: &Record, field: u8) -> bool {
        self.output.clear();
        {
            let h = self.head;
            reader::blob(&h, self, r, field)
        }
    }
    #[inline(never)]
    fn close(&mut self) {
        self.file.close();
        self.opened = false;
    }
}
impl Read for Index<'_> {
    #[inline(never)]
    fn read(&mut self, at: u32, out: &mut [u8]) -> bool {
        let ok = self.opened && self.file.at(u64::from(at), out);
        if !ok {
            self.failed = true;
        }
        ok
    }
    fn string(&mut self, at: u32, len: usize, append: bool) -> Option<u8> {
        if !append {
            self.output.clear();
        }
        let start = self.output.len();
        if self.output.try_reserve(len).is_err() {
            self.failed = true;
            return None;
        }
        self.output.resize(start + len, 0);
        if len > 0 && !self.file.at(u64::from(at), &mut self.output[start..]) {
            self.failed = true;
            return None;
        }
        Some(self.output.last().copied().unwrap_or(0))
    }
    fn append(&mut self, b: &[u8]) {
        if self.output.try_reserve(b.len()).is_ok() {
            self.output.extend_from_slice(b);
        }
    }
    fn fail(&mut self) {
        self.failed = true;
    }
}
fn recover(s: &Services) -> bool {
    if !s.exists(BACKUP) {
        return true;
    }
    if !s.exists(INDEX) {
        if s.rename(BACKUP, INDEX) {
            s.event(1);
            return true;
        }
        s.event(2);
        return false;
    }
    let mut candidate = Index::open(s, INDEX, false);
    if candidate.opened {
        candidate.close();
        if s.remove(BACKUP) {
            return true;
        }
        s.event(3);
        return false;
    }
    if candidate.failed {
        s.event(4);
        return false;
    }
    candidate.close();
    candidate = Index::open(s, BACKUP, false);
    if candidate.opened {
        candidate.close();
        if !s.remove(INDEX) || !s.rename(BACKUP, INDEX) {
            s.event(5);
            return false;
        }
        s.event(59);
        return true;
    }
    if candidate.failed {
        s.event(6);
        return false;
    }
    candidate.close();
    if !s.remove(BACKUP) {
        s.event(7);
        return false;
    }
    true
}
fn install(s: &Services) -> bool {
    let previous = s.exists(INDEX);
    if s.exists(BACKUP) && !s.remove(BACKUP) {
        s.event(8);
        s.remove(NEW);
        return false;
    }
    if previous && !s.rename(INDEX, BACKUP) {
        s.event(9);
        s.remove(NEW);
        return false;
    }
    if !s.rename(NEW, INDEX) {
        s.event(10);
        if previous && !s.rename(BACKUP, INDEX) {
            s.event(11);
        }
        s.remove(NEW);
        return false;
    }
    if previous && !s.remove(BACKUP) {
        s.event(12);
    }
    true
}
pub fn dirty(s: &Services, memory: &mut u8, operation: u8) -> bool {
    if operation == 2 {
        clear_dirty(s, memory);
        return true;
    }
    if operation == 0 {
        return *memory != 0 || s.exists(DIRTY);
    }
    if s.exists(DIRTY) {
        return true;
    }
    if !s.exists(CACHE) && !s.storage(2, CACHE, b"\0") {
        s.event(32);
        *memory = 1;
        return true;
    }
    let marker = File::open(s, DIRTY, 1);
    if !marker.valid() {
        s.event(33);
        *memory = 1;
    }
    true
}
fn clear_dirty(s: &Services, memory: &mut u8) {
    *memory = 0;
    if s.exists(DIRTY) && !s.remove(DIRTY) {
        s.event(13);
    }
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Staged {
    record: Record,
    hash: u64,
    name: [u8; 255],
    author_len: u8,
    author: [u8; 128],
    title_len: u8,
    title: [u8; 255],
}
const STAGE_STRIDE: usize = size_of::<Staged>();
const AUTHOR_LEN_OFFSET: usize = core::mem::offset_of!(Staged, author_len);
const AUTHOR_OFFSET: usize = core::mem::offset_of!(Staged, author);
const MTIME_OFFSET: usize = core::mem::offset_of!(Record, mtime);
#[repr(C)]
#[derive(Clone, Copy)]
struct Prior {
    hash: u64,
    size: u32,
    seen: u16,
    ordinal: u16,
}
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
struct Key {
    key: [u8; 12],
    ordinal: u16,
}
#[repr(C)]
struct Spelling {
    text: [u8; 128],
    ordinal: u16,
    count: u16,
    len: u8,
}
const _: () = assert!(
    size_of::<Staged>() == 776
        && size_of::<Prior>() == 16
        && size_of::<Key>() == 14
        && size_of::<Spelling>() == 134
);
struct Walk<'a, 'b> {
    s: &'a Services,
    entry: Buffer<'a, Staged>,
    name: Buffer<'a, u8>,
    dedup: Option<Buffer<'a, u64>>,
    active: usize,
    prior: Option<Buffer<'a, Prior>>,
    previous: Option<Index<'a>>,
    stats: &'b mut Stats,
    books: u16,
    folders: u16,
    folder_bytes: u32,
    next_seen: u16,
    reused: u16,
    enriched: u16,
    duplicates: u16,
    unreadable: u16,
    degraded: bool,
    metadata: bool,
    failed: bool,
    units: u32,
}
impl Walk<'_, '_> {
    #[inline(never)]
    fn load_prior(&mut self, units: &mut u32) -> Option<usize> {
        let s = self.s;
        let mut prior_count = 0;
        if s.exists(INDEX) {
            let index = self.previous.insert(Index::open(s, INDEX, true));
            if index.opened {
                self.next_seen = index.head.next_seen;
                prior_count = index.head.books as usize;
                let Some(mut list) = Buffer::<Prior>::new(s, prior_count.max(1)) else {
                    s.event(37);
                    return None;
                };
                for i in 0..prior_count {
                    s.service(units);
                    let mut r = Record::default();
                    if !index.record(i as u16, &mut r) {
                        s.log(38, i as u32, 0, 0, 0, b"\0");
                        return None;
                    }
                    let Some(hash) = index.hash(&r) else {
                        s.log(38, i as u32, 0, 0, 0, b"\0");
                        return None;
                    };
                    list[i] = Prior {
                        hash,
                        size: r.size,
                        seen: r.seen,
                        ordinal: i as u16,
                    };
                }
                list.n = prior_count;
                sort_by(&mut list[..prior_count], |a, b| {
                    (a.hash, a.ordinal & 0x7fff).cmp(&(b.hash, b.ordinal & 0x7fff))
                });
                self.prior = Some(list);
            } else if index.failed {
                s.event(39);
                return None;
            } else {
                self.previous = None;
            }
        }
        Some(prior_count)
    }
    #[inline(never)]
    fn stage(
        &mut self,
        out: &mut Writer<'_, '_>,
        name: &[u8],
        size: u32,
        folder: u16,
        path: &[u8],
        mtime: u32,
    ) -> Option<()> {
        self.s.service(&mut self.units);
        let entry = self.entry.first_mut()?;
        let hash = xp_core::fnv1a64(&path[..path.len() - 1]);
        let prior = self.prior.as_ref().and_then(|p| {
            let start = p.partition_point(|p| p.hash < hash);
            (start..p.len())
                .take_while(|&i| p[i].hash == hash)
                .find(|&i| p[i].ordinal & 0x8000 == 0)
        });
        let stem = name
            .iter()
            .rposition(|&c| c == b'.')
            .filter(|&i| i != 0)
            .unwrap_or(name.len());
        let mut title = copy(&name[..stem])?;
        let mut author = Text::new();
        let (mut title_book, mut author_book) = (false, false);
        let mut title_staged = false;
        unsafe {
            ptr::write_bytes(ptr::from_mut(entry).cast::<u8>(), 0, size_of::<Staged>());
        }
        entry.hash = hash;
        let xtc = ext(name, b".xtc") || ext(name, b".xtch");
        let expected = self.metadata && (ext(name, b".epub") || xtc);
        let mut reuse = false;
        let mut r = Record::default();
        if let Some(i) = prior {
            let old = self.prior.as_ref()?[i];
            let previous = self.previous.as_mut()?;
            if !previous.record(old.ordinal & 0x7fff, &mut r) {
                return None;
            }
            reuse = old.size == size
                && mtime != 0
                && r.mtime == mtime
                && previous.head.fold == 4
                && previous.head.metadata == u8::from(self.metadata)
                && r.metadata == u8::from(expected);
            if reuse {
                if !previous.blob(&r, 2) {
                    return None;
                }
                author = copy(&previous.output)?;
                let has_title = previous.blob(&r, 1) && !previous.output.is_empty();
                if !has_title && previous.failed {
                    return None;
                }
                entry.record = r;
                author_book = !author.is_empty();
                if has_title {
                    title = copy(&previous.output)?;
                    title_book = true;
                }
                self.stats.reused += 1;
            }
        }
        if !reuse && expected {
            self.stats.parsed += 1;
            let (mut title_ptr, mut author_ptr) = (ptr::null_mut(), ptr::null());
            let (mut title_len, mut author_len) = (0, 0);
            let loaded = unsafe {
                (self.s.metadata)(
                    self.s.ctx,
                    path.as_ptr(),
                    u8::from(xtc),
                    &mut title_ptr,
                    &mut title_len,
                    &mut author_ptr,
                    &mut author_len,
                ) != 0
            };
            let _loan = MetadataLoan(self.s);
            entry.record.metadata = if loaded { 1 } else { 2 };
            if loaded {
                if title_len > 0 {
                    // Fold the loaned title after retaining its on-disk prefix. This
                    // avoids a second allocation proportional to untrusted metadata.
                    let title = unsafe { slice::from_raw_parts_mut(title_ptr, title_len) };
                    let e = &mut *entry;
                    let shown = title.len().min(255);
                    e.title_len = shown as u8;
                    e.title[..shown].copy_from_slice(&title[..shown]);
                    let folded = text::fold(title);
                    let n = xp_core::utf8::safe_truncate(&title[..folded.min(96)]);
                    e.record.fold_len = n as u8;
                    e.record.fold[..n].copy_from_slice(&title[..n]);
                    title_book = true;
                    title_staged = true;
                }
                if author_len > 0 {
                    author = copy(unsafe { slice::from_raw_parts(author_ptr, author_len) })?;
                }
                author_book = !author.is_empty();
            }
            if !title_book && !author_book {
                self.s.log(14, 0, 0, 0, 0, path);
            }
        }
        if !author.is_empty() && &*transformed(&author, text::fold)? == b"unknown" {
            author.clear();
            author_book = false;
        }
        if title_book || author_book {
            self.enriched += 1;
        }
        let e = &mut *entry;
        e.record.size = size;
        e.record.mtime = mtime;
        e.record.folder = folder;
        e.record.name_len = name.len() as u8;
        if let Some(i) = prior {
            let old = &mut self.prior.as_mut()?[i];
            old.ordinal |= 0x8000;
            e.record.seen = old.seen;
            self.reused += 1;
        } else {
            e.record.seen = u16::MAX;
        }
        if title_book && !title_staged {
            let n = title.len().min(255);
            e.title_len = n as u8;
            e.title[..n].copy_from_slice(&title[..n]);
        }
        if !reuse {
            if !title_staged {
                let folded = transformed(&title, text::fold)?;
                let n = xp_core::utf8::safe_truncate(&folded[..folded.len().min(96)]);
                e.record.fold_len = n as u8;
                e.record.fold[..n].copy_from_slice(&folded[..n]);
            }
            let key = transformed(&author, text::author)?;
            e.record.author_len = key.len().min(12) as u8;
            e.record.author[..usize::from(e.record.author_len)]
                .copy_from_slice(&key[..usize::from(e.record.author_len)]);
        }
        e.name[..name.len()].copy_from_slice(name);
        let display = transformed(&author, text::clean)?;
        let n = display.len().min(128);
        e.author_len = n as u8;
        e.author[..n].copy_from_slice(&display[..n]);
        out.put(pod(e));
        self.books += 1;
        Some(())
    }
    fn walk(&mut self, path: &[u8], depth: u8, out: &mut Writer<'_, '_>, folders: &File<'_>) {
        if self.failed || depth > 5 || self.books >= 4096 {
            return;
        }
        let base = self.active;
        self.walk_dir(path, depth, out, folders, base);
        self.active = base;
    }
    fn walk_dir(
        &mut self,
        path: &[u8],
        depth: u8,
        out: &mut Writer<'_, '_>,
        folders: &File<'_>,
        base: usize,
    ) {
        let mut dir = File::open(self.s, path, 2);
        if !dir.valid() || dir.value(11) == 0 {
            return;
        }
        dir.value(12);
        let mut emitted = false;
        let mut folder = 0;
        loop {
            let mut e = dir.next();
            if !e.valid() {
                break;
            }
            self.s.service(&mut self.units);
            if self.failed || self.books >= 4096 {
                break;
            }
            self.name[0] = 0;
            self.s.op(13, e.id, b"\0", self.name.as_mut_ptr(), 512, 0);
            let is_dir = e.value(11) != 0;
            let size = if is_dir { 0 } else { e.value(9) as u32 };
            let mtime = if is_dir { 0 } else { e.value(10) as u32 };
            e.close();
            let len = self.name.iter().position(|&b| b == 0).unwrap_or(512);
            if len == 0 || hidden(&self.name[..len]) {
                continue;
            }
            let Some(name) = copy(&self.name[..len]) else {
                self.failed = true;
                break;
            };
            if is_dir {
                let position = dir.value(8);
                dir.close();
                let Some(child) = path_join(path, &name) else {
                    self.failed = true;
                    return;
                };
                self.walk(&child, depth + 1, out, folders);
                if self.failed || self.books >= 4096 {
                    return;
                }
                dir = File::open(self.s, path, 2);
                if !dir.valid() || dir.value(11) == 0 || !dir.seek(position) {
                    dir.close();
                    self.s.log(15, position as u32, 0, 0, 0, path);
                    self.failed = true;
                    return;
                }
                continue;
            }
            if !book(&name) {
                continue;
            }
            if size == 0 || name.len() > 255 || path.len() - 1 > 255 {
                self.unreadable += 1;
                continue;
            }
            let mut hash = 2166136261u32;
            for &b in name.iter() {
                hash = (hash ^ u32::from(b)).wrapping_mul(16777619);
            }
            let key = (u64::from(hash) << 32) | u64::from(size);
            if let Some(keys) = self.dedup.as_mut() {
                let slot = base + keys[base..self.active].partition_point(|&k| k < key);
                if slot < self.active && keys[slot] == key {
                    self.duplicates += 1;
                    continue;
                }
                if self.active < 1024 {
                    keys.copy_within(slot..self.active, slot + 1);
                    keys[slot] = key;
                    self.active += 1;
                } else if !self.degraded {
                    self.s.log(16, 1024, 0, 0, 0, path);
                    self.degraded = true;
                }
            }
            if !emitted {
                folder = self.folders;
                let n = (path.len() - 1) as u8;
                if !folders.write(&[n]) || !folders.write(&path[..n as usize]) {
                    self.s.log(17, 0, 0, 0, 0, path);
                    self.failed = true;
                    break;
                }
                self.folder_bytes += 1 + u32::from(n);
                self.folders += 1;
                emitted = true;
            }
            let Some(full) = path_join(path, &name) else {
                self.failed = true;
                break;
            };
            if self.stage(out, &name, size, folder, &full, mtime).is_none() {
                self.failed = true;
                break;
            }
        }
    }
}
fn stage_read(
    s: &Services,
    file: &File<'_>,
    failed: &mut bool,
    offset: u64,
    out: &mut [u8],
) -> bool {
    if *failed {
        return false;
    }
    if !file.at(offset, out) || (out.len() == STAGE_STRIDE && out[AUTHOR_LEN_OFFSET] > 128) {
        s.log(19, offset as u32, 0, 0, 0, b"\0");
        *failed = true;
        return false;
    }
    true
}
fn blob_size(e: &Staged, c: &Staged) -> u32 {
    8 + u32::from(e.record.name_len)
        + 1
        + u32::from(c.author_len)
        + 1
        + u32::from(e.title_len)
        + 1
        + u32::from(e.author_len)
}
fn emit(
    s: &Services,
    head: &mut Header,
    order: &[u16],
    seen: &[u16],
    rankable: bool,
    stats: &mut Stats,
) -> bool {
    let n = usize::from(head.books);
    let mut units = 0;
    let Some(mut arrival) = Buffer::<u16>::new(s, n.max(1)) else {
        s.event(18);
        return false;
    };
    let mut stage = File::open(s, STAGE, 0);
    if !stage.valid() {
        return false;
    }
    let mut out = File::open(s, NEW, 1);
    if !out.valid() {
        return false;
    }
    let mut writer = Writer::new(&out);
    let mut failed = false;
    writer.put(pod(head));
    writer.pad(head.folder_start, &mut units);
    {
        let mut folders = File::open(s, FOLDERS, 0);
        if folders.valid() {
            let mut buf = [0; 256];
            let mut copied = 0;
            loop {
                let got = folders.read(&mut buf);
                if got <= 0 {
                    if got < 0 {
                        failed = true;
                    }
                    break;
                }
                s.service(&mut units);
                writer.put(&buf[..got as usize]);
                copied += got as u32;
            }
            folders.close();
            if copied != head.folder_len {
                s.log(20, copied, head.folder_len, 0, 0, b"\0");
                failed = true;
            }
        } else {
            s.event(21);
            failed = true;
        }
    }
    if !failed {
        writer.pad(head.record_start, &mut units);
    }
    if failed {
        s.event(22);
        drop(writer);
        stage.close();
        out.close();
        s.remove(NEW);
        return false;
    }
    if rankable && n > 1 {
        s.log(23, (n * 14) as u32, 0, 0, 0, b"\0");
    }
    let mut authors = if rankable && n > 1 {
        Buffer::<Key>::new(s, n)
    } else {
        None
    };
    if let Some(keys) = authors.as_mut() {
        for i in 0..n {
            s.service(&mut units);
            let mut r = Record::default();
            if !stage_read(
                s,
                &stage,
                &mut failed,
                u64::from(order[i]) * STAGE_STRIDE as u64,
                pod_mut(&mut r),
            ) {
                break;
            }
            keys[i].key = if r.author_len == 0 {
                [255; 12]
            } else {
                let mut k = [0; 12];
                let len = usize::from(r.author_len).min(12);
                k[..len].copy_from_slice(&r.author[..len]);
                k
            };
            keys[i].ordinal = i as u16;
        }
        if !failed {
            s.delay();
            sort_by(&mut keys[..], Ord::cmp);
            s.delay();
        }
    } else if n > 1 {
        stats.ranks_degraded = true;
    }
    let mut canonical = if authors.is_some() && n > 1 {
        Buffer::<u16>::new(s, n)
    } else {
        None
    };
    let mut spelling = None;
    if let Some(c) = canonical.as_mut() {
        for (i, v) in c.iter_mut().enumerate() {
            s.service(&mut units);
            *v = i as u16;
        }
        spelling = Buffer::<Spelling>::new(s, 16);
    } else if authors.is_some() && n > 1 {
        s.event(24);
        stats.ranks_degraded = true;
    }
    if canonical.is_some() && spelling.is_none() {
        s.event(25);
        stats.ranks_degraded = true;
    }
    if let (Some(keys), Some(c), Some(scratch)) =
        (authors.as_ref(), canonical.as_mut(), spelling.as_mut())
    {
        let mut start = 0;
        while !failed && start < n {
            s.service(&mut units);
            let mut end = start + 1;
            while end < n && keys[end].key == keys[start].key {
                s.service(&mut units);
                end += 1;
            }
            if keys[start].key[0] != 255 && end - start > 1 {
                let mut count = 0;
                for key in &keys[start..end] {
                    s.service(&mut units);
                    let ord = key.ordinal;
                    let offset = u64::from(order[ord as usize]) * STAGE_STRIDE as u64;
                    let mut len = [0];
                    if !stage_read(
                        s,
                        &stage,
                        &mut failed,
                        offset + AUTHOR_LEN_OFFSET as u64,
                        &mut len,
                    ) {
                        break;
                    }
                    if len[0] == 0 {
                        continue;
                    }
                    let mut text = [0; 128];
                    let want = usize::from(len[0]).min(128);
                    if !stage_read(
                        s,
                        &stage,
                        &mut failed,
                        offset + AUTHOR_OFFSET as u64,
                        &mut text[..want],
                    ) {
                        break;
                    }
                    if let Some(i) = (0..count).find(|&i| {
                        scratch[i].len as usize == want && scratch[i].text[..want] == text[..want]
                    }) {
                        scratch[i].count += 1;
                    } else if count < 16 {
                        scratch[count].text[..want].copy_from_slice(&text[..want]);
                        scratch[count].ordinal = ord;
                        scratch[count].count = 1;
                        scratch[count].len = want as u8;
                        count += 1;
                    }
                }
                let best = (0..count).min_by(|&a, &b| {
                    scratch[b]
                        .count
                        .cmp(&scratch[a].count)
                        .then(scratch[a].len.cmp(&scratch[b].len))
                        .then(
                            scratch[a].text[..scratch[a].len as usize]
                                .cmp(&scratch[b].text[..scratch[b].len as usize]),
                        )
                });
                let best = best.map_or(keys[start].ordinal, |i| scratch[i].ordinal);
                for key in &keys[start..end] {
                    s.service(&mut units);
                    c[key.ordinal as usize] = best;
                }
            }
            start = end;
        }
    }
    if !failed {
        if let (Some(keys), Some(c)) = (authors.as_mut(), canonical.as_ref()) {
            for i in 0..n {
                s.service(&mut units);
                let offset = u64::from(order[c[i] as usize]) * STAGE_STRIDE as u64;
                let mut len = [0];
                let mut author = [0; 128];
                if !stage_read(
                    s,
                    &stage,
                    &mut failed,
                    offset + AUTHOR_LEN_OFFSET as u64,
                    &mut len,
                ) {
                    break;
                }
                if len[0] > 0
                    && !stage_read(
                        s,
                        &stage,
                        &mut failed,
                        offset + AUTHOR_OFFSET as u64,
                        &mut author[..usize::from(len[0]).min(128)],
                    )
                {
                    break;
                }
                let key = if len[0] == 0 {
                    Some(Text::new())
                } else {
                    transformed(&author[..usize::from(len[0]).min(128)], text::surname)
                };
                let Some(key) = key else {
                    failed = true;
                    break;
                };
                keys[i].key = if key.is_empty() {
                    [255; 12]
                } else {
                    let mut k = [0; 12];
                    let size = key.len().min(12);
                    k[..size].copy_from_slice(&key[..size]);
                    k
                };
                keys[i].ordinal = i as u16;
            }
            if !failed {
                s.delay();
                sort_by(&mut keys[..], Ord::cmp);
                s.delay();
            }
        }
    }
    if rankable {
        for (i, v) in arrival[..n].iter_mut().enumerate() {
            *v = i as u16;
        }
        if n > 1 {
            let mut times = Buffer::<u32>::new(s, n);
            if let Some(t) = times.as_mut() {
                for i in 0..n {
                    s.service(&mut units);
                    if !stage_read(
                        s,
                        &stage,
                        &mut failed,
                        u64::from(order[i]) * STAGE_STRIDE as u64 + MTIME_OFFSET as u64,
                        pod_mut(&mut t[i]),
                    ) {
                        break;
                    }
                }
            } else {
                s.log(26, (n * 4) as u32, 0, 0, 0, b"\0");
            }
            if !failed {
                s.delay();
                sort_by(&mut arrival[..n], |&a, &b| {
                    times
                        .as_ref()
                        .map_or(core::cmp::Ordering::Equal, |t| {
                            t[a as usize].cmp(&t[b as usize])
                        })
                        .then(
                            seen[order[a as usize] as usize].cmp(&seen[order[b as usize] as usize]),
                        )
                        .then(a.cmp(&b))
                });
                s.delay();
            }
        }
    } else {
        for (i, &ord) in order.iter().enumerate() {
            s.service(&mut units);
            arrival[ord as usize] = i as u16;
        }
        s.log(27, n as u32, 0, 0, 0, b"\0");
        stats.ranks_degraded = true;
    }
    if failed {
        s.event(28);
        drop(writer);
        stage.close();
        out.close();
        s.remove(NEW);
        return false;
    }
    let Some(mut staged) = Buffer::<Staged>::new(s, 2) else {
        s.event(29);
        drop(writer);
        out.close();
        s.remove(NEW);
        return false;
    };
    let [entry, canonical_entry] = &mut *staged else {
        return false;
    };
    let mut cursor = 0;
    for (i, &ord) in order.iter().enumerate() {
        s.service(&mut units);
        if !stage_read(
            s,
            &stage,
            &mut failed,
            u64::from(ord) * STAGE_STRIDE as u64,
            pod_mut(entry),
        ) {
            break;
        }
        entry.record.name_off = cursor;
        let from = canonical.as_ref().map_or(i, |c| c[i] as usize);
        if !stage_read(
            s,
            &stage,
            &mut failed,
            u64::from(order[from]) * STAGE_STRIDE as u64,
            pod_mut(canonical_entry),
        ) {
            break;
        }
        cursor += blob_size(entry, canonical_entry);
        entry.record.seen = seen[ord as usize];
        writer.put(pod(&entry.record));
    }
    if !failed {
        writer.pad(head.perm_start, &mut units);
    }
    for k in 0..n {
        s.service(&mut units);
        let ord = authors.as_ref().map_or(k as u16, |a| a[k].ordinal);
        if !failed {
            writer.put(&ord.to_le_bytes());
        }
    }
    for k in 0..n {
        s.service(&mut units);
        if !failed {
            writer.put(&arrival[k].to_le_bytes());
        }
    }
    if !failed {
        writer.pad(head.name_start, &mut units);
    }
    let mut written_blob = 0;
    for (i, &ord) in order.iter().enumerate() {
        s.service(&mut units);
        if !stage_read(
            s,
            &stage,
            &mut failed,
            u64::from(ord) * STAGE_STRIDE as u64,
            pod_mut(entry),
        ) {
            break;
        }
        writer.put(&entry.hash.to_le_bytes());
        writer.put(&entry.name[..entry.record.name_len as usize]);
        let from = canonical.as_ref().map_or(i, |c| c[i] as usize);
        if !stage_read(
            s,
            &stage,
            &mut failed,
            u64::from(order[from]) * STAGE_STRIDE as u64,
            pod_mut(canonical_entry),
        ) {
            break;
        }
        writer.put(&[canonical_entry.author_len]);
        if canonical_entry.author_len > 0 {
            writer.put(&canonical_entry.author[..canonical_entry.author_len as usize]);
        }
        writer.put(&[entry.title_len]);
        if entry.title_len > 0 {
            writer.put(&entry.title[..entry.title_len as usize]);
        }
        writer.put(&[entry.author_len]);
        if entry.author_len > 0 {
            writer.put(&entry.author[..entry.author_len as usize]);
        }
        written_blob += blob_size(entry, canonical_entry);
    }
    head.name_len = written_blob;
    head.size = head.name_start + written_blob;
    stage.close();
    let written = writer.pos as u32;
    if !writer.flush() {
        failed = true;
    }
    drop(writer);
    head.flags = u8::from(stats.ranks_degraded) | (u8::from(stats.dedup_degraded) << 1);
    if !out.seek(0) || !out.write(pod(head)) {
        failed = true;
    }
    let closed = out.close();
    if failed || written != head.size || !closed {
        s.log(
            30,
            u32::from(failed),
            u32::from(closed),
            written,
            head.size,
            b"\0",
        );
        if closed {
            s.remove(NEW);
        }
        return false;
    }
    install(s)
}
fn cleanup(s: &Services) {
    s.remove(STAGE);
    s.remove(FOLDERS);
}
#[repr(u8)]
pub enum BuildResult {
    Failed,
    Unchanged,
    EmitFailed,
    Installed,
}
pub fn build(
    s: &Services,
    root: &[u8],
    stats: &mut Stats,
    metadata: bool,
    memory: &mut u8,
) -> BuildResult {
    let start = s.now();
    let mut units = 0;
    *stats = Stats::default();
    s.storage(2, CACHE, b"\0");
    if !recover(s) {
        return BuildResult::Failed;
    }
    cleanup(s);
    let Some(name) = Buffer::<u8>::new(s, 512) else {
        s.log(34, 512, 0, 0, 0, b"\0");
        return BuildResult::Failed;
    };
    let Some(entry) = Buffer::<Staged>::new(s, 1) else {
        s.log(35, STAGE_STRIDE as u32, 0, 0, 0, b"\0");
        return BuildResult::Failed;
    };
    let dedup = Buffer::<u64>::new(s, 1024);
    if dedup.is_none() {
        s.event(36);
    }
    let mut walk = Walk {
        s,
        entry,
        name,
        degraded: dedup.is_none(),
        dedup,
        active: 0,
        prior: None,
        previous: None,
        stats,
        books: 0,
        folders: 0,
        folder_bytes: 0,
        next_seen: 0,
        reused: 0,
        enriched: 0,
        duplicates: 0,
        unreadable: 0,
        metadata,
        failed: false,
        units: 0,
    };
    let Some(prior_count) = walk.load_prior(&mut units) else {
        return BuildResult::Failed;
    };
    let mut stage = File::open(s, STAGE, 1);
    let mut folders = if stage.valid() {
        File::open(s, FOLDERS, 1)
    } else {
        File { s, id: 0 }
    };
    if !stage.valid() || !folders.valid() {
        s.event(40);
        return BuildResult::Failed;
    }
    s.log(41, s.now().wrapping_sub(start), 0, 0, 0, b"\0");
    let walk_start = s.now();
    let flushed = {
        let mut writer = Writer::new(&stage);
        walk.walk(root, 0, &mut writer, &folders);
        writer.flush()
    };
    let stage_closed = stage.close();
    let folders_closed = folders.close();
    s.log(42, s.now().wrapping_sub(walk_start), 0, 0, 0, b"\0");
    if walk.failed || !flushed || !stage_closed || !folders_closed {
        s.event(43);
        cleanup(s);
        return BuildResult::Failed;
    }
    walk.stats.books = walk.books;
    walk.stats.folders = walk.folders;
    walk.stats.duplicates = walk.duplicates;
    walk.stats.unreadable = walk.unreadable;
    walk.stats.dedup_degraded = walk.degraded;
    walk.stats.unchanged = walk.reused;
    walk.stats.enriched = walk.enriched;
    if walk.previous.is_some()
        && walk.books as usize == prior_count
        && walk.reused as usize == prior_count
        && walk.stats.reused as usize == prior_count
        && walk.unreadable == 0
    {
        if let Some(previous) = walk.previous.as_mut() {
            previous.close();
        }
        cleanup(s);
        walk.stats.walk_ms = s.now().wrapping_sub(start);
        s.log(
            44,
            u32::from(walk.stats.reused),
            u32::from(walk.stats.parsed),
            walk.stats.walk_ms,
            0,
            b"\0",
        );
        clear_dirty(s, memory);
        return BuildResult::Unchanged;
    }
    let reconcile = s.now();
    let n = walk.books as usize;
    let Some(mut seen) = Buffer::<u16>::new(s, n.max(1)) else {
        s.event(45);
        cleanup(s);
        return BuildResult::Failed;
    };
    if n > 0 {
        if let Some(p) = walk.prior.as_mut() {
            sort_by(&mut p[..prior_count], |a, b| {
                (a.size, a.ordinal & 0x7fff).cmp(&(b.size, b.ordinal & 0x7fff))
            });
        }
        let mut read = File::open(s, STAGE, 0);
        if !read.valid() {
            s.event(46);
            cleanup(s);
            return BuildResult::Failed;
        }
        for i in 0..n {
            s.service(&mut units);
            let mut r = Record::default();
            if !read.at((i * STAGE_STRIDE) as u64, pod_mut(&mut r)) {
                s.log(47, i as u32, 0, 0, 0, b"\0");
                read.close();
                cleanup(s);
                return BuildResult::Failed;
            }
            if r.seen != u16::MAX {
                seen[i] = r.seen;
                continue;
            }
            let renamed = walk.prior.as_mut().and_then(|p| {
                let start = p[..prior_count].partition_point(|p| p.size < r.size);
                p[start..prior_count]
                    .iter_mut()
                    .take_while(|p| p.size == r.size)
                    .find(|p| p.ordinal & 0x8000 == 0)
            });
            if let Some(p) = renamed {
                p.ordinal |= 0x8000;
                seen[i] = p.seen;
                walk.stats.renamed += 1;
            } else {
                seen[i] = walk.next_seen;
                walk.next_seen = walk.next_seen.wrapping_add(1);
                walk.stats.added += 1;
            }
        }
        if !read.close() {
            s.event(48);
            cleanup(s);
            return BuildResult::Failed;
        }
        for q in 0..prior_count {
            s.service(&mut units);
            if walk
                .prior
                .as_ref()
                .is_some_and(|p| p[q].ordinal & 0x8000 == 0)
            {
                walk.stats.removed += 1;
            }
        }
    }
    s.log(49, s.now().wrapping_sub(reconcile), 0, 0, 0, b"\0");
    let title_start = s.now();
    let mut head = Header {
        books: walk.books,
        folders: walk.folders,
        next_seen: walk.next_seen,
        metadata: u8::from(metadata),
        ..Header::default()
    };
    format::layout(&mut head, walk.folder_bytes, 0);
    drop(walk);
    let Some(mut order) = Buffer::<u16>::new(s, n.max(1)) else {
        s.log(50, n as u32, 0, 0, 0, b"\0");
        cleanup(s);
        return BuildResult::Failed;
    };
    for (i, v) in order[..n].iter_mut().enumerate() {
        s.service(&mut units);
        *v = i as u16;
    }
    let mut rankable = true;
    if n > 1 {
        s.log(51, (n * 14) as u32, 0, 0, 0, b"\0");
        if let Some(mut keys) = Buffer::<Key>::new(s, n) {
            let mut file = File::open(s, STAGE, 0);
            if !file.valid() {
                s.event(52);
                cleanup(s);
                return BuildResult::Failed;
            }
            for i in 0..n {
                s.service(&mut units);
                let mut r = Record::default();
                if !file.at((i * STAGE_STRIDE) as u64, pod_mut(&mut r)) {
                    s.log(53, (i * STAGE_STRIDE) as u32, 0, 0, 0, b"\0");
                    file.close();
                    cleanup(s);
                    return BuildResult::Failed;
                }
                let len = usize::from(r.fold_len).min(12);
                keys[i].key = [0; 12];
                keys[i].key[..len].copy_from_slice(&r.fold[..len]);
                keys[i].ordinal = i as u16;
            }
            if !file.close() {
                s.event(54);
                cleanup(s);
                return BuildResult::Failed;
            }
            s.delay();
            sort_by(&mut keys[..], Ord::cmp);
            s.delay();
            for i in 0..n {
                s.service(&mut units);
                order[i] = keys[i].ordinal;
            }
        } else {
            stats.ranks_degraded = true;
            rankable = false;
            s.event(55);
        }
    }
    s.log(56, s.now().wrapping_sub(title_start), 0, 0, 0, b"\0");
    let emit_start = s.now();
    let ok = emit(s, &mut head, &order[..n], &seen[..n], rankable, stats);
    s.log(57, s.now().wrapping_sub(emit_start), 0, 0, 0, b"\0");
    cleanup(s);
    stats.walk_ms = s.now().wrapping_sub(start);
    stats.replaced = ok;
    if ok {
        BuildResult::Installed
    } else {
        BuildResult::EmitFailed
    }
}
