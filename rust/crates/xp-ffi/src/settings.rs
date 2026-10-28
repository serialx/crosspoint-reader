//! Persistence policy. All storage, codec and container leaves are synchronous C callbacks.
use core::ffi::c_void;
use core::ptr;

#[repr(u32)]
#[derive(Clone, Copy)]
enum JsonQuery {
    Null = 0,
    ArraySize = 1,
    U8 = 2,
    U16 = 3,
    U32 = 4,
    Usize = 5,
    IsUsize = 6,
    IsU32 = 7,
    IsInt = 8,
    Int = 9,
    IsU8 = 10,
    Bool = 11,
    IsString = 12,
    IsU16 = 13,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum JsonWrite {
    Array = 0,
    Number = 1,
    Bool = 2,
    String = 3,
    BorrowedString = 4,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum RowEdit {
    Clear = 0,
    Reserve = 1,
    ResetScratch = 2,
    AppendScratch = 3,
    Erase = 4,
    MoveToFront = 5,
    InsertFront = 7,
    Resize = 8,
    AppendMove = 10,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum StoreKind {
    Recent = 0,
    Wifi = 1,
    Opds = 2,
    Tokens = 3,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum RowEvent {
    InvalidLength = 1,
    OversizeLength = 2,
    OversizePassword = 3,
    LengthMismatch = 4,
    ChecksumMismatch = 5,
    InvalidChecksum = 6,
    Loaded = 7,
    Resave = 8,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum PersistLeaf {
    Exists = 0,
    Read = 1,
    Parse = 2,
    Mkdir = 3,
    Serialize = 4,
    Write = 5,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum Mutation {
    RecentAdd = 0,
    RecentUpdate = 1,
    Remove = 2,
    WifiUpsert = 3,
    TokenPut = 4,
    WifiRemove = 5,
}
#[repr(u32)]
#[derive(Clone, Copy)]
enum MutationResult {
    Rejected = 0,
    Unchanged = 1,
    Updated = 2,
    Inserted = 3,
    Removed = 4,
}

#[repr(C)]
pub struct FileOps {
    ctx: *mut c_void,
    read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    write: unsafe extern "C" fn(*mut c_void, *const u8, usize) -> usize,
    seek: unsafe extern "C" fn(*mut c_void, usize) -> bool,
}

#[repr(C)]
pub struct Writer {
    fill: usize,
    pos: usize,
    ok: bool,
}

#[repr(C)]
pub struct Reader {
    fill: usize,
    off: usize,
    start: usize,
}

unsafe fn flush(writer: &mut Writer, buffer: *const u8, ops: &FileOps) {
    if writer.fill != 0 {
        writer.ok &= (ops.write)(ops.ctx, buffer, writer.fill) == writer.fill;
        writer.fill = 0;
    }
}

/// # Safety
/// State, buffer and callbacks must be valid for the call; the file is exclusively borrowed.
#[no_mangle]
pub unsafe extern "C" fn xp_store_buffer_write(
    state: *mut Writer,
    buffer: *mut u8,
    cap: usize,
    ops: *const FileOps,
    src: *const u8,
    len: usize,
) {
    let (state, ops) = (&mut *state, &*ops);
    state.pos = state.pos.wrapping_add(len);
    if state.fill.wrapping_add(len) > cap {
        flush(state, buffer, ops);
    }
    if len >= cap {
        state.ok &= (ops.write)(ops.ctx, src, len) == len;
    } else {
        if len != 0 {
            ptr::copy_nonoverlapping(src, buffer.add(state.fill), len);
        }
        state.fill += len;
    }
}

/// # Safety
/// Same contract as xp_store_buffer_write.
#[no_mangle]
pub unsafe extern "C" fn xp_store_buffer_flush(
    state: *mut Writer,
    buffer: *const u8,
    ops: *const FileOps,
) -> bool {
    let state = &mut *state;
    flush(state, buffer, &*ops);
    state.ok
}

/// # Safety
/// State, buffer and destination are valid and disjoint for their capacities.
#[no_mangle]
pub unsafe extern "C" fn xp_store_buffer_read(
    state: *mut Reader,
    buffer: *mut u8,
    cap: usize,
    ops: *const FileOps,
    dst: *mut u8,
    mut len: usize,
) -> usize {
    let (state, ops) = (&mut *state, &*ops);
    if cap == 0 {
        let got = (ops.read)(ops.ctx, dst, len).max(0) as usize;
        state.start = state.start.wrapping_add(got);
        return got;
    }
    let mut total = 0;
    while len != 0 {
        if state.off == state.fill {
            state.start = state.start.wrapping_add(state.fill);
            state.off = 0;
            state.fill = (ops.read)(ops.ctx, buffer, cap).max(0) as usize;
            if state.fill == 0 {
                break;
            }
        }
        let chunk = len.min(state.fill - state.off);
        ptr::copy_nonoverlapping(buffer.add(state.off), dst.add(total), chunk);
        state.off += chunk;
        len -= chunk;
        total += chunk;
    }
    total
}

/// # Safety
/// The state and callbacks are valid and exclusively borrowed.
#[no_mangle]
pub unsafe extern "C" fn xp_store_buffer_seek(
    state: *mut Reader,
    cap: usize,
    ops: *const FileOps,
    target: usize,
) -> bool {
    let (state, ops) = (&mut *state, &*ops);
    if cap != 0 && target >= state.start && target < state.start.wrapping_add(state.fill) {
        state.off = target - state.start;
        return true;
    }
    if !(ops.seek)(ops.ctx, target) {
        return false;
    }
    state.start = target;
    state.fill = 0;
    state.off = 0;
    true
}

/// # Safety
/// Data covers len writable bytes; key covers key_len bytes and does not alias data.
#[no_mangle]
pub unsafe extern "C" fn xp_store_xor(data: *mut u8, len: usize, key: *const u8, key_len: usize) {
    if key.is_null() || key_len == 0 {
        return;
    }
    for i in 0..len {
        *data.add(i) ^= *key.add(i % key_len);
    }
}

/// # Safety
/// Images has sixteen elements; pos/fill point to valid ring state.
#[no_mangle]
pub unsafe extern "C" fn xp_store_recent_push(
    images: *mut u16,
    pos: *mut u8,
    fill: *mut u8,
    index: u16,
) {
    *images.add(*pos as usize) = index;
    *pos = (*pos + 1) % 16;
    if *fill < 16 {
        *fill += 1;
    }
}

/// # Safety
/// Images has sixteen elements; pos and fill are valid ring state.
#[no_mangle]
pub unsafe extern "C" fn xp_store_recent_contains(
    images: *const u16,
    pos: u8,
    fill: u8,
    index: u16,
    count: u8,
) -> bool {
    for i in 0..count.min(fill) {
        let slot = (pos as usize + 16 - 1 - i as usize) % 16;
        if *images.add(slot) == index {
            return true;
        }
    }
    false
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bytes {
    data: *const u8,
    len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct JsonPath {
    key: *const core::ffi::c_char,
    index: usize,
    field: *const core::ffi::c_char,
    subindex: usize,
    subfield: *const core::ffi::c_char,
    static_keys: bool,
}
impl JsonPath {
    fn root(key: &'static core::ffi::CStr) -> Self {
        Self {
            key: key.as_ptr(),
            index: usize::MAX,
            field: ptr::null(),
            subindex: usize::MAX,
            subfield: ptr::null(),
            static_keys: true,
        }
    }
    fn field(mut self, index: usize, field: &'static core::ffi::CStr) -> Self {
        self.index = index;
        self.field = field.as_ptr();
        self
    }
}

#[repr(C)]
pub struct JsonOps {
    ctx: *mut c_void,
    number: unsafe extern "C" fn(*mut c_void, *const JsonPath, u32, u64) -> u64,
    text: unsafe extern "C" fn(*mut c_void, *const JsonPath) -> Bytes,
    put: unsafe extern "C" fn(*mut c_void, *const JsonPath, u32, u64, Bytes),
}
impl JsonOps {
    #[inline(never)]
    unsafe fn root_num(
        &self,
        key: &'static core::ffi::CStr,
        kind: JsonQuery,
        fallback: u64,
    ) -> u64 {
        self.num(&JsonPath::root(key), kind, fallback)
    }
    #[inline(never)]
    unsafe fn root_put(&self, key: &'static core::ffi::CStr, kind: JsonWrite, value: u64) {
        self.put(&JsonPath::root(key), kind, value);
    }
    #[inline(never)]
    unsafe fn root_text(&self, key: &'static core::ffi::CStr) -> Bytes {
        self.text(&JsonPath::root(key))
    }
    unsafe fn num(&self, p: &JsonPath, kind: JsonQuery, fallback: u64) -> u64 {
        (self.number)(self.ctx, p, kind as u32, fallback)
    }
    unsafe fn text(&self, p: &JsonPath) -> Bytes {
        (self.text)(self.ctx, p)
    }
    unsafe fn put(&self, p: &JsonPath, kind: JsonWrite, number: u64) {
        (self.put)(
            self.ctx,
            p,
            kind as u32,
            number,
            Bytes {
                data: ptr::null(),
                len: 0,
            },
        );
    }
}

#[repr(C)]
pub struct Rows {
    ctx: *mut c_void,
    count: unsafe extern "C" fn(*mut c_void) -> usize,
    // 0 clear, 1 reserve, 2 reset scratch, 3 append scratch, 4 erase, 5 rotate to front.
    edit: unsafe extern "C" fn(*mut c_void, u32, usize),
    get: unsafe extern "C" fn(*mut c_void, usize, u32) -> Bytes,
    set: unsafe extern "C" fn(*mut c_void, usize, u32, Bytes),
    decode: unsafe extern "C" fn(*mut c_void, u32, Bytes, usize) -> u32,
    emit: unsafe extern "C" fn(*mut c_void, usize, u32, *const JsonOps, *const JsonPath, bool),
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, usize, usize, usize)>,
}
impl Rows {
    unsafe fn count(&self) -> usize {
        (self.count)(self.ctx)
    }
    unsafe fn edit(&self, op: RowEdit, index: usize) {
        (self.edit)(self.ctx, op as u32, index)
    }
    unsafe fn get(&self, row: usize, field: u32) -> Bytes {
        (self.get)(self.ctx, row, field)
    }
    unsafe fn set(&self, row: usize, field: u32, bytes: Bytes) {
        (self.set)(self.ctx, row, field, bytes)
    }
    unsafe fn diagnostic(&self, event: RowEvent, a: usize, b: usize, c: usize) {
        if let Some(f) = self.diagnostic {
            f(self.ctx, event as u32, a, b, c);
        }
    }
    unsafe fn load(&self, json: &JsonOps, path: &JsonPath, field: u32) {
        self.set(usize::MAX, field, json.text(path));
    }
    unsafe fn decode(&self, json: &JsonOps, path: &JsonPath, field: u32, max: usize) -> u32 {
        (self.decode)(self.ctx, field, json.text(path), max)
    }
    unsafe fn emit(
        &self,
        json: &JsonOps,
        path: &JsonPath,
        row: usize,
        field: u32,
        obfuscated: bool,
    ) {
        (self.emit)(self.ctx, row, field, json, path, obfuscated);
    }
}

unsafe fn password(
    rows: &Rows,
    json: &JsonOps,
    root: JsonPath,
    index: usize,
    field: u32,
    max: usize,
    resave: &mut bool,
) -> bool {
    let status = rows.decode(json, &root.field(index, c"password_obf"), field, max);
    if status & 2 != 0 {
        return false;
    }
    if status & 1 == 0 {
        let text = json.text(&root.field(index, c"password"));
        if text.len > max {
            return false;
        }
        rows.set(usize::MAX, field, text);
        *resave |= text.len != 0;
    }
    true
}

// Store kinds: recent books, Wi-Fi credentials, OPDS servers, OPDS tokens.
fn collection(kind: StoreKind) -> JsonPath {
    JsonPath::root(match kind {
        StoreKind::Recent => c"books",
        StoreKind::Wifi => c"credentials",
        StoreKind::Opds => c"servers",
        StoreKind::Tokens => c"tokens",
    })
}
fn fields(kind: StoreKind) -> [&'static core::ffi::CStr; 4] {
    match kind {
        StoreKind::Recent => [c"path", c"title", c"author", c"coverBmpPath"],
        StoreKind::Wifi => [c"ssid", c"password_obf", c"", c""],
        StoreKind::Opds => [c"name", c"url", c"username", c"password_obf"],
        StoreKind::Tokens => [c"url", c"access_obf", c"refresh_obf", c"refresh_url"],
    }
}

/// # Safety
/// Services and their borrowed state must remain valid throughout the call.
/// Return value requests the existing post-unlock resave.
#[no_mangle]
pub unsafe extern "C" fn xp_store_rows_load(
    kind: u32,
    json: *const JsonOps,
    rows: *const Rows,
) -> bool {
    let kind = StoreKind::from_abi(kind);
    let (json, rows) = (&*json, &*rows);
    let root = collection(kind);
    let limit = if matches!(kind, StoreKind::Recent) {
        10
    } else {
        8
    };
    if matches!(kind, StoreKind::Wifi) {
        rows.set(usize::MAX, 8, json.root_text(c"lastConnectedSsid"));
    }
    rows.edit(RowEdit::Clear, 0);
    let size = json.num(&root, JsonQuery::ArraySize, 0) as usize;
    rows.edit(RowEdit::Reserve, size.min(limit));
    let mut resave = false;
    for index in 0..size {
        if rows.count() >= limit {
            break;
        }
        rows.edit(RowEdit::ResetScratch, 0);
        let keys = fields(kind);
        rows.load(json, &root.field(index, keys[0]), 0);
        match kind {
            StoreKind::Recent => {
                for (field, key) in keys.iter().enumerate().skip(1) {
                    rows.load(json, &root.field(index, key), field as u32);
                }
            }
            StoreKind::Wifi => {
                let length = root.field(index, c"password_len");
                let has_length = json.num(&length, JsonQuery::Null, 0) == 0;
                let expected = json.num(&length, JsonQuery::Usize, 0) as usize;
                if has_length {
                    if json.num(&length, JsonQuery::IsUsize, 0) == 0 {
                        rows.diagnostic(RowEvent::InvalidLength, 0, 0, 0);
                        resave = true;
                        continue;
                    }
                    if expected > 64 {
                        rows.diagnostic(RowEvent::OversizeLength, expected, 0, 0);
                        resave = true;
                        continue;
                    }
                }
                if !password(rows, json, root, index, 1, 64, &mut resave) {
                    rows.diagnostic(RowEvent::OversizePassword, 0, 0, 0);
                    resave = true;
                    continue;
                }
                let decoded = rows.get(usize::MAX, 1);
                let mut valid = true;
                if has_length {
                    if decoded.len != expected {
                        rows.diagnostic(RowEvent::LengthMismatch, expected, decoded.len, 0);
                        valid = false;
                    }
                } else {
                    resave = true;
                }
                let checksum = root.field(index, c"password_crc32");
                if json.num(&checksum, JsonQuery::IsU32, 0) != 0 {
                    if crate::xp_crc32(decoded.data, decoded.len)
                        != json.num(&checksum, JsonQuery::U32, 0) as u32
                    {
                        rows.diagnostic(RowEvent::ChecksumMismatch, 0, 0, 0);
                        valid = false;
                    }
                } else if json.num(&checksum, JsonQuery::Null, 0) != 0 {
                    resave = true;
                } else {
                    rows.diagnostic(RowEvent::InvalidChecksum, 0, 0, 0);
                    valid = false;
                }
                if !valid {
                    resave = true;
                    continue;
                }
            }
            StoreKind::Opds => {
                rows.load(json, &root.field(index, keys[1]), 1);
                rows.load(json, &root.field(index, keys[2]), 2);
                password(rows, json, root, index, 3, usize::MAX, &mut resave);
                let mut path = root.field(index, c"headers");
                let headers = (json.num(&path, JsonQuery::ArraySize, 0) as usize).min(2);
                for h in 0..headers {
                    path.subindex = h;
                    path.subfield = c"name".as_ptr();
                    rows.load(json, &path, 4 + h as u32 * 2);
                    path.subfield = c"value_obf".as_ptr();
                    rows.decode(json, &path, 5 + h as u32 * 2, usize::MAX);
                }
            }
            StoreKind::Tokens => {
                if rows.get(usize::MAX, 0).len == 0 {
                    continue;
                }
                rows.decode(json, &root.field(index, keys[1]), 1, 2048);
                rows.decode(json, &root.field(index, keys[2]), 2, 2048);
                rows.load(json, &root.field(index, keys[3]), 3);
            }
        }
        rows.edit(RowEdit::AppendScratch, 0);
    }
    rows.edit(RowEdit::ResetScratch, 0);
    rows.diagnostic(RowEvent::Loaded, rows.count(), 0, 0);
    if resave {
        rows.diagnostic(RowEvent::Resave, 0, 0, 0);
    }
    resave
}

/// # Safety
/// Services and their borrowed state must remain valid throughout the call.
#[no_mangle]
pub unsafe extern "C" fn xp_store_rows_save(kind: u32, json: *const JsonOps, rows: *const Rows) {
    let kind = StoreKind::from_abi(kind);
    let (json, rows) = (&*json, &*rows);
    let root = collection(kind);
    if matches!(kind, StoreKind::Wifi) {
        rows.emit(
            json,
            &JsonPath::root(c"lastConnectedSsid"),
            usize::MAX,
            8,
            false,
        );
    }
    json.put(&root, JsonWrite::Array, 0);
    for index in 0..rows.count() {
        for (field, key) in fields(kind)
            .iter()
            .take(if matches!(kind, StoreKind::Wifi) {
                2
            } else {
                4
            })
            .enumerate()
        {
            let obf = match kind {
                StoreKind::Wifi => field == 1,
                StoreKind::Opds => field == 3,
                StoreKind::Tokens => field == 1 || field == 2,
                StoreKind::Recent => false,
            };
            rows.emit(json, &root.field(index, key), index, field as u32, obf);
        }
        if matches!(kind, StoreKind::Wifi) {
            let pass = rows.get(index, 1);
            json.put(
                &root.field(index, c"password_len"),
                JsonWrite::Number,
                pass.len as u64,
            );
            json.put(
                &root.field(index, c"password_crc32"),
                JsonWrite::Number,
                crate::xp_crc32(pass.data, pass.len) as u64,
            );
        }
        if matches!(kind, StoreKind::Opds) {
            let mut path = root.field(index, c"headers");
            json.put(&path, JsonWrite::Array, 0);
            for h in 0..2 {
                path.subindex = h;
                path.subfield = c"name".as_ptr();
                rows.emit(json, &path, index, 4 + h as u32 * 2, false);
                path.subfield = c"value_obf".as_ptr();
                rows.emit(json, &path, index, 5 + h as u32 * 2, true);
            }
        }
    }
}

#[repr(C)]
pub struct State {
    ctx: *mut c_void,
    path_get: unsafe extern "C" fn(*mut c_void) -> Bytes,
    path_set: unsafe extern "C" fn(*mut c_void, Bytes),
    images: *mut u16,
    pos: *mut u8,
    fill: *mut u8,
    overlay: *mut u16,
    overlay_pos: *mut u8,
    overlay_fill: *mut u8,
    load_count: *mut u8,
    from_reader: *mut bool,
    boot: *mut bool,
}

unsafe fn load_ring(
    json: &JsonOps,
    images: *mut u16,
    pos: *mut u8,
    fill: *mut u8,
    keys: [&'static core::ffi::CStr; 3],
) {
    let mut root = JsonPath::root(keys[0]);
    let count = json.num(&root, JsonQuery::ArraySize, 0).min(16) as usize;
    ptr::write_bytes(images, 0, 16);
    for i in 0..count {
        root.index = i;
        *images.add(i) = json.num(&root, JsonQuery::U16, 0) as u16;
    }
    *pos = json.num(&JsonPath::root(keys[1]), JsonQuery::U8, 0) as u8;
    if *pos >= 16 {
        *pos = if count > 0 { *pos % 16 } else { 0 };
    }
    *fill = (json.num(&JsonPath::root(keys[2]), JsonQuery::U8, 0) as u8).min(count as u8);
}
unsafe fn save_ring(
    json: &JsonOps,
    images: *const u16,
    pos: u8,
    fill: u8,
    keys: [&'static core::ffi::CStr; 3],
) {
    let mut root = JsonPath::root(keys[0]);
    json.put(&root, JsonWrite::Array, 0);
    for i in 0..16 {
        root.index = i;
        json.put(&root, JsonWrite::Number, *images.add(i) as u64);
    }
    json.put(&JsonPath::root(keys[1]), JsonWrite::Number, pos as u64);
    json.put(&JsonPath::root(keys[2]), JsonWrite::Number, fill as u64);
}
const SLEEP_KEYS: [&core::ffi::CStr; 3] =
    [c"recentSleepImages", c"recentSleepPos", c"recentSleepFill"];
const OVERLAY_KEYS: [&core::ffi::CStr; 3] = [
    c"recentOverlaySleepImages",
    c"recentOverlaySleepPos",
    c"recentOverlaySleepFill",
];

/// # Safety
/// State fields are valid, nonoverlapping and exclusively borrowed for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_store_state_load(state: *const State, json: *const JsonOps) {
    let (state, json) = (&*state, &*json);
    (state.path_set)(state.ctx, json.root_text(c"openEpubPath"));
    load_ring(json, state.images, state.pos, state.fill, SLEEP_KEYS);
    load_ring(
        json,
        state.overlay,
        state.overlay_pos,
        state.overlay_fill,
        OVERLAY_KEYS,
    );
    let legacy = JsonPath::root(c"lastSleepImage");
    if *state.fill == 0 && json.num(&legacy, JsonQuery::Null, 0) == 0 {
        let value = json.num(&legacy, JsonQuery::U8, 255) as u8;
        if value != 255 {
            xp_store_recent_push(state.images, state.pos, state.fill, value as u16);
        }
    }
    *state.load_count = json.root_num(c"readerActivityLoadCount", JsonQuery::U8, 0) as u8;
    *state.from_reader = json.root_num(c"lastSleepFromReader", JsonQuery::Bool, 0) != 0;
    *state.boot = json.root_num(c"showBootScreen", JsonQuery::Bool, 1) != 0;
}

/// # Safety
/// State and its fields remain valid for the call; save does not mutate the state.
#[no_mangle]
pub unsafe extern "C" fn xp_store_state_save(state: *const State, json: *const JsonOps) {
    let (state, json) = (&*state, &*json);
    (json.put)(
        json.ctx,
        &JsonPath::root(c"openEpubPath"),
        3,
        0,
        (state.path_get)(state.ctx),
    );
    save_ring(json, state.images, *state.pos, *state.fill, SLEEP_KEYS);
    save_ring(
        json,
        state.overlay,
        *state.overlay_pos,
        *state.overlay_fill,
        OVERLAY_KEYS,
    );
    json.root_put(
        c"readerActivityLoadCount",
        JsonWrite::Number,
        *state.load_count as u64,
    );
    json.root_put(
        c"lastSleepFromReader",
        JsonWrite::Bool,
        *state.from_reader as u64,
    );
    json.root_put(c"showBootScreen", JsonWrite::Bool, *state.boot as u64);
}

#[repr(C)]
pub struct PersistOps {
    ctx: *mut c_void,
    // Platform/third-party leaves: exists, read, parse, mkdir, serialize, write.
    call: unsafe extern "C" fn(*mut c_void, u32) -> bool,
}
/// # Safety
/// The context and callbacks remain valid until return.
/// Status: 0 success, 1 missing, 2 empty read, 3 parse error, 4 write failure.
#[no_mangle]
pub unsafe extern "C" fn xp_store_document(ops: *const PersistOps, save: bool) -> u32 {
    let ops = &*ops;
    if save {
        (ops.call)(ops.ctx, PersistLeaf::Mkdir as u32);
        (ops.call)(ops.ctx, PersistLeaf::Serialize as u32);
        if (ops.call)(ops.ctx, PersistLeaf::Write as u32) {
            0
        } else {
            4
        }
    } else if !(ops.call)(ops.ctx, PersistLeaf::Exists as u32) {
        1
    } else if !(ops.call)(ops.ctx, PersistLeaf::Read as u32) {
        2
    } else if !(ops.call)(ops.ctx, PersistLeaf::Parse as u32) {
        3
    } else {
        0
    }
}

/// Password fallback policy; size/codec work remains in the codec leaf.
#[no_mangle]
pub extern "C" fn xp_store_password_action(
    decoded_ok: bool,
    too_long: bool,
    legacy_len: usize,
    max: usize,
) -> u32 {
    if too_long {
        0
    } else if decoded_ok {
        1
    } else if legacy_len > max {
        0
    } else {
        2
    }
}

/// # Safety
/// Callbacks are synchronous; load returns (success | resave << 1) after releasing the lock.
#[no_mangle]
pub unsafe extern "C" fn xp_store_load(
    ctx: *mut c_void,
    load: unsafe extern "C" fn(*mut c_void) -> u32,
    save: unsafe extern "C" fn(*mut c_void) -> bool,
    failed_resave: unsafe extern "C" fn(*mut c_void),
) -> bool {
    let flags = load(ctx);
    let ok = flags & 1 != 0;
    if ok && flags & 2 != 0 && !save(ctx) {
        failed_resave(ctx);
    }
    ok
}

#[repr(C)]
pub struct DecodeOps {
    ctx: *mut c_void,
    key: unsafe extern "C" fn() -> *const u8,
    resize: unsafe extern "C" fn(*mut c_void, usize) -> *mut u8,
    decode: unsafe extern "C" fn(*mut u8, usize, *mut usize, *const u8, usize) -> i32,
}
/// # Safety
/// Encoded input is valid for len bytes; callbacks borrow their context synchronously.
/// status: 0 success, 1 absent, 2 size query error, 3 oversized, 4 decode error.
#[no_mangle]
pub unsafe extern "C" fn xp_store_decode(
    encoded: *const u8,
    len: usize,
    limit: usize,
    ops: *const DecodeOps,
    error: *mut i32,
) -> u32 {
    if encoded.is_null() || len == 0 {
        return 1;
    }
    let ops = &*ops;
    let mut size = 0;
    *error = (ops.decode)(ptr::null_mut(), 0, &mut size, encoded, len);
    if *error != 0 && *error != -0x002a {
        return 2;
    }
    if size > limit {
        return 3;
    }
    let result = (ops.resize)(ops.ctx, size);
    *error = (ops.decode)(result, size, &mut size, encoded, len);
    if *error != 0 {
        (ops.resize)(ops.ctx, 0);
        return 4;
    }
    let result = (ops.resize)(ops.ctx, size);
    xp_store_xor(result, size, (ops.key)(), 6);
    0
}

#[repr(C)]
pub struct SettingField {
    key: *const core::ffi::c_char,
    value: *mut u8,
    text: *mut u8,
    capacity: usize,
    kind: u32,
    min: u8,
    max: u8,
    obfuscated: bool,
}
#[repr(C)]
pub struct FieldCodec {
    ctx: *mut c_void,
    decode: unsafe extern "C" fn(*mut c_void, Bytes, usize, *mut u32) -> Bytes,
    encode: unsafe extern "C" fn(*mut c_void, *const u8) -> Bytes,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, *const core::ffi::c_char)>,
}
unsafe fn cbytes(data: *const u8) -> Bytes {
    let mut len = 0;
    while *data.add(len) != 0 {
        len += 1;
    }
    Bytes { data, len }
}
unsafe fn same(a: Bytes, b: Bytes) -> bool {
    a.len == b.len && (0..a.len).all(|i| *a.data.add(i) == *b.data.add(i))
}
unsafe fn copy_field(dst: *mut u8, capacity: usize, src: Bytes) {
    if capacity == 0 {
        return;
    }
    let mut len = 0;
    while len < src.len.min(capacity - 1) && *src.data.add(len) != 0 {
        len += 1;
    }
    if dst != src.data.cast_mut() && len != 0 {
        ptr::copy(src.data, dst, len);
    }
    ptr::write_bytes(dst.add(len), 0, capacity - len);
}
fn raw_path(key: *const core::ffi::c_char) -> JsonPath {
    JsonPath {
        key,
        index: usize::MAX,
        field: ptr::null(),
        subindex: usize::MAX,
        subfield: ptr::null(),
        static_keys: false,
    }
}
/// # Safety
/// Field metadata describes valid borrowed storage; strings are NUL terminated.
#[no_mangle]
pub unsafe extern "C" fn xp_setting_field(
    field: *const SettingField,
    json: *const JsonOps,
    codec: *const FieldCodec,
    save: bool,
) -> bool {
    let (f, json, codec) = (&*field, &*json, &*codec);
    if f.key.is_null() || (f.value.is_null() && f.text.is_null()) {
        return false;
    }
    let path = raw_path(f.key);
    if f.text.is_null() {
        if save {
            json.put(&path, JsonWrite::Number, *f.value as u64);
            return false;
        }
        let default = *f.value;
        let mut value = json.num(&path, JsonQuery::U8, default as u64) as u8;
        match f.kind {
            0 | 1 => {
                if value >= f.max {
                    value = default;
                }
            }
            3 => {
                if value < f.min {
                    value = f.min;
                } else if value > f.max {
                    value = f.max;
                }
            }
            _ => {}
        }
        *f.value = value;
        return false;
    }
    let mut key = [0u8; 64];
    let mut obf_path = path;
    if f.obfuscated {
        let raw = cbytes(f.key.cast());
        let n = raw.len.min(63);
        ptr::copy_nonoverlapping(raw.data, key.as_mut_ptr(), n);
        let suffix = b"_obf";
        for (dst, byte) in key.iter_mut().take(63).skip(n).zip(suffix) {
            *dst = *byte;
        }
        obf_path.key = key.as_ptr().cast();
    }
    if save {
        let text = if f.obfuscated {
            (codec.encode)(codec.ctx, f.text)
        } else {
            cbytes(f.text)
        };
        (json.put)(json.ctx, &obf_path, JsonWrite::String as u32, 0, text);
        return false;
    }
    if f.capacity == 0 {
        if let Some(log) = codec.diagnostic {
            log(codec.ctx, 1, f.key);
        }
        *f.text = 0;
        return true;
    }
    let mut resave = false;
    if f.obfuscated {
        let mut status = 0;
        let decoded = (codec.decode)(codec.ctx, json.text(&obf_path), f.capacity - 1, &mut status);
        if status & 2 != 0 {
            if let Some(log) = codec.diagnostic {
                log(codec.ctx, 2, f.key);
            }
            resave = true;
        }
        if status & 1 != 0 && decoded.len != 0 {
            copy_field(f.text, f.capacity, decoded);
            return resave;
        }
    }
    if json.num(&path, JsonQuery::IsString, 0) != 0 {
        let raw = json.text(&path);
        resave |= f.obfuscated && !same(raw, cbytes(f.text));
        copy_field(f.text, f.capacity, raw);
    }
    resave
}

#[repr(C)]
pub struct Settings {
    front: [*mut u8; 4],
    font_family: *mut u8,
    font_size: *mut u8,
    long_press: *mut u8,
    short_power: *mut u8,
    language: *mut u8,
    layouts: *mut u16,
    plugins: *mut u8,
    indent: *mut u8,
    extra_spacing: *mut u8,
    touch: *mut u8,
    next: *mut u8,
    previous: *mut u8,
    sleep: *mut u8,
    home: *mut u8,
    sd_font: *mut u8,
    sd_capacity: usize,
    dictionary: *mut u8,
    dictionary_capacity: usize,
    plugins_installed: unsafe extern "C" fn() -> bool,
    language_code: unsafe extern "C" fn(u8) -> Bytes,
    language_from_code: unsafe extern "C" fn(*const u8) -> u8,
    has_home: bool,
    long_press_count: u8,
    short_power_count: u8,
}
const FRONT_KEYS: [&core::ffi::CStr; 4] = [
    c"frontButtonBack",
    c"frontButtonConfirm",
    c"frontButtonLeft",
    c"frontButtonRight",
];

#[no_mangle]
pub extern "C" fn xp_setting_sleep_minutes(legacy: u8) -> u8 {
    match legacy {
        0 => 1,
        1 => 5,
        3 => 15,
        4 => 30,
        _ => 10,
    }
}
/// # Safety
/// Each front pointer points to one distinct writable byte.
#[no_mangle]
pub unsafe extern "C" fn xp_setting_front(front: *const *mut u8) {
    for i in 0..4 {
        for j in i + 1..4 {
            if **front.add(i) == **front.add(j) {
                for k in 0..4 {
                    **front.add(k) = k as u8;
                }
                return;
            }
        }
    }
}
fn clamp(value: u8, limit: u8, default: u8) -> u8 {
    if value < limit {
        value
    } else {
        default
    }
}

/// # Safety
/// Fields and callbacks are valid for the call; metadata comes from the selected board.
#[no_mangle]
pub unsafe extern "C" fn xp_settings_tail(
    settings: *const Settings,
    json: *const JsonOps,
    save: bool,
) -> bool {
    let (s, json) = (&*settings, &*json);
    if save {
        for (field, key) in s.front.iter().zip(FRONT_KEYS) {
            json.root_put(key, JsonWrite::Number, **field as u64);
        }
        json.root_put(c"fontFamily", JsonWrite::Number, 1);
        json.root_put(c"fontSize", JsonWrite::Number, *s.font_size as u64);
        for (data, key) in [
            (s.sd_font, c"sdFontFamilyName"),
            (s.dictionary, c"dictionaryName"),
        ] {
            if *data != 0 {
                (json.put)(
                    json.ctx,
                    &JsonPath::root(key),
                    JsonWrite::BorrowedString as u32,
                    0,
                    cbytes(data),
                );
            }
        }
        json.root_put(
            c"longPressMenuFunction",
            JsonWrite::Number,
            *s.long_press as u64,
        );
        json.root_put(c"shortPwrBtn", JsonWrite::Number, *s.short_power as u64);
        (json.put)(
            json.ctx,
            &JsonPath::root(c"language"),
            3,
            0,
            (s.language_code)(*s.language),
        );
        if *s.layouts != 0 {
            json.root_put(c"keyboardLayouts", JsonWrite::Number, *s.layouts as u64);
        }
        return false;
    }
    let mut resave = false;
    if json.root_num(c"pluginsEnabled", JsonQuery::Null, 0) != 0 {
        *s.plugins = (s.plugins_installed)() as u8;
        resave = true;
    }
    let has_width = json.root_num(c"paragraphIndentSpaces", JsonQuery::IsInt, 0) != 0;
    let width = json.root_num(c"paragraphIndentSpaces", JsonQuery::Int, 0) as i32;
    *s.indent = if has_width {
        width.clamp(0, 5) as u8
    } else if *s.extra_spacing != 0 {
        0
    } else {
        2
    };
    resave |= !has_width || !(0..=5).contains(&width);
    if json.root_num(c"pageTurnGesture", JsonQuery::Null, 0) != 0
        && json.root_num(c"previousPageGesture", JsonQuery::Null, 0) != 0
        && json.root_num(c"touchReaderControls", JsonQuery::IsU8, 0) != 0
    {
        let mode = json.root_num(c"touchReaderControls", JsonQuery::U8, 0) as u8;
        if (1..=3).contains(&mode) {
            *s.touch = 1;
            *s.next = match mode {
                1 => 1,
                2 => 2,
                _ => 3,
            };
            *s.previous = *s.next;
            resave = true;
        }
    }
    if json.root_num(c"sleepTimeoutMinutes", JsonQuery::Null, 0) != 0
        && json.root_num(c"sleepTimeout", JsonQuery::Null, 0) == 0
    {
        *s.sleep = xp_setting_sleep_minutes(clamp(
            json.root_num(c"sleepTimeout", JsonQuery::U8, 2) as u8,
            5,
            2,
        ));
        resave = true;
    }
    for (i, (field, key)) in s.front.iter().zip(FRONT_KEYS).enumerate() {
        **field = clamp(
            json.root_num(key, JsonQuery::U8, i as u64) as u8,
            4,
            i as u8,
        );
    }
    xp_setting_front(s.front.as_ptr());
    *s.font_size = json.root_num(c"fontSize", JsonQuery::U8, 14) as u8;
    if *s.font_size <= 3 {
        *s.font_size = 12 + *s.font_size * 2;
        resave = true;
    }
    let family = json.root_num(c"fontFamily", JsonQuery::U8, 1) as u8;
    *s.font_family = 0;
    *s.long_press = clamp(
        json.root_num(c"longPressMenuFunction", JsonQuery::U8, 1) as u8,
        s.long_press_count,
        1,
    );
    *s.short_power = clamp(
        json.root_num(c"shortPwrBtn", JsonQuery::U8, 0) as u8,
        s.short_power_count,
        0,
    );
    if s.has_home
        && json.root_num(c"homeButtonLongPressAction", JsonQuery::Null, 0) != 0
        && json.root_num(c"longPressMenuFunction", JsonQuery::Null, 0) == 0
    {
        let legacy = [6, 1, 7, 8, 9, 11];
        if let Some(action) = legacy.get(*s.long_press as usize) {
            *s.home = *action;
            resave = true;
        }
    }
    copy_field(
        s.sd_font,
        s.sd_capacity,
        json.root_text(c"sdFontFamilyName"),
    );
    if *s.sd_font == 0 {
        let legacy = match family {
            0 => Some(c"NotoSerif"),
            2 => Some(c"OpenDyslexic"),
            _ => None,
        };
        if let Some(name) = legacy {
            copy_field(s.sd_font, s.sd_capacity, cbytes(name.as_ptr().cast()));
        }
    }
    resave |= family != 1;
    copy_field(
        s.dictionary,
        s.dictionary_capacity,
        json.root_text(c"dictionaryName"),
    );
    if json.root_num(c"language", JsonQuery::IsString, 0) != 0 {
        *s.language = (s.language_from_code)(json.root_text(c"language").data);
    }
    if json.root_num(c"keyboardLayouts", JsonQuery::IsU16, 0) != 0 {
        *s.layouts = json.root_num(c"keyboardLayouts", JsonQuery::U16, 0) as u16;
    }
    resave
}

#[no_mangle]
pub extern "C" fn xp_setting_line_compression(sd: bool, spacing: u8) -> f32 {
    if sd {
        match spacing {
            0 => 0.95,
            2 => 1.1,
            3 => 1.2,
            _ => 1.0,
        }
    } else {
        match spacing {
            0 => 0.90,
            2 => 1.0,
            3 => 1.05,
            _ => 0.95,
        }
    }
}
#[no_mangle]
pub extern "C" fn xp_setting_sleep_ms(minutes: u8) -> u32 {
    if minutes >= 31 {
        0
    } else {
        minutes.clamp(1, 30) as u32 * 60000
    }
}
#[no_mangle]
pub extern "C" fn xp_setting_refresh(value: u8) -> i32 {
    match value {
        0 => 1,
        1 => 5,
        2 => 10,
        4 => 30,
        5 => i32::MAX,
        _ => 15,
    }
}

/// # Safety
/// Rows and key are valid borrowed views for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_store_find(rows: *const Rows, field: u32, key: Bytes) -> usize {
    let rows = &*rows;
    (0..rows.count())
        .find(|&index| same(rows.get(index, field), key))
        .unwrap_or(usize::MAX)
}

/// # Safety
/// args contains four borrowed strings. The facade owns all mutation allocations.
/// Actions: 0 recent add, 1 recent update, 2 remove by key, 3 Wi-Fi upsert, 4 token put.
/// Result: 0 missing/full, 1 unchanged, 2 updated, 3 inserted, 4 removed.
#[no_mangle]
pub unsafe extern "C" fn xp_store_mutate(
    rows: *const Rows,
    action: u32,
    args: *const Bytes,
) -> u32 {
    let r = &*rows;
    let index = xp_store_find(rows, 0, *args);
    match Mutation::from_abi(action) {
        Mutation::RecentAdd => {
            if index != usize::MAX {
                r.edit(RowEdit::MoveToFront, index);
                for field in 1..4 {
                    let value = *args.add(field);
                    if !same(r.get(0, field as u32), value) {
                        r.set(0, field as u32, value);
                    }
                }
                MutationResult::Updated as u32
            } else {
                for field in 0..4 {
                    r.set(usize::MAX, field as u32, *args.add(field));
                }
                r.edit(RowEdit::InsertFront, 0);
                if r.count() > 10 {
                    r.edit(RowEdit::Resize, 10);
                }
                MutationResult::Inserted as u32
            }
        }
        Mutation::RecentUpdate => {
            if index == usize::MAX {
                return MutationResult::Rejected as u32;
            }
            for field in 1..4 {
                r.set(index, field as u32, *args.add(field));
            }
            MutationResult::Updated as u32
        }
        Mutation::Remove | Mutation::WifiRemove => {
            if index == usize::MAX {
                return MutationResult::Rejected as u32;
            }
            r.edit(RowEdit::Erase, index);
            if action == Mutation::WifiRemove as u32 && same(r.get(usize::MAX, 8), *args) {
                r.set(
                    usize::MAX,
                    8,
                    Bytes {
                        data: c"".as_ptr().cast(),
                        len: 0,
                    },
                );
            }
            MutationResult::Removed as u32
        }
        Mutation::WifiUpsert => {
            if index != usize::MAX {
                r.set(index, 1, *args.add(1));
                MutationResult::Updated as u32
            } else {
                if r.count() >= 8 {
                    return MutationResult::Rejected as u32;
                }
                r.set(usize::MAX, 0, *args);
                r.set(usize::MAX, 1, *args.add(1));
                r.edit(RowEdit::AppendMove, 0);
                MutationResult::Inserted as u32
            }
        }
        Mutation::TokenPut => {
            if (*args.add(1)).len == 0 {
                if index == usize::MAX {
                    return MutationResult::Unchanged as u32;
                }
                r.edit(RowEdit::Erase, index);
                return MutationResult::Removed as u32;
            }
            if index != usize::MAX {
                if (1..4).all(|f| same(r.get(index, f as u32), *args.add(f))) {
                    return MutationResult::Unchanged as u32;
                }
                for f in 1..4 {
                    r.set(index, f as u32, *args.add(f));
                }
                MutationResult::Updated as u32
            } else {
                if r.count() >= 8 {
                    r.edit(RowEdit::Erase, 0);
                }
                for f in 0..4 {
                    r.set(usize::MAX, f as u32, *args.add(f));
                }
                r.edit(RowEdit::AppendMove, 0);
                MutationResult::Inserted as u32
            }
        }
    }
}

/// # Safety
/// Callbacks operate on the same rows; exists does not mutate them.
#[no_mangle]
pub unsafe extern "C" fn xp_store_prune(
    rows: *const Rows,
    exists: unsafe extern "C" fn(*mut c_void, usize) -> bool,
    move_row: unsafe extern "C" fn(*mut c_void, usize, usize),
) -> bool {
    let r = &*rows;
    let before = r.count();
    let mut kept = 0;
    for index in 0..before {
        if exists(r.ctx, index) {
            if index != kept {
                move_row(r.ctx, kept, index);
            }
            kept += 1;
        }
    }
    r.edit(RowEdit::Resize, kept);
    before != kept
}

/// # Safety
/// Bytes are valid for their lengths.
#[no_mangle]
pub unsafe extern "C" fn xp_store_prefix(value: Bytes, prefix: Bytes) -> bool {
    prefix.len != 0
        && value.len != 0
        && value.len >= prefix.len
        && (0..prefix.len).all(|i| *value.data.add(i) == *prefix.data.add(i))
}

#[no_mangle]
pub extern "C" fn xp_store_index_action(count: usize, index: usize, add: bool) -> bool {
    if add {
        count < 8
    } else {
        index < count
    }
}

impl StoreKind {
    fn from_abi(value: u32) -> Self {
        match value {
            0 => Self::Recent,
            1 => Self::Wifi,
            2 => Self::Opds,
            _ => Self::Tokens,
        }
    }
}
impl Mutation {
    fn from_abi(value: u32) -> Self {
        match value {
            0 => Self::RecentAdd,
            1 => Self::RecentUpdate,
            2 => Self::Remove,
            3 => Self::WifiUpsert,
            5 => Self::WifiRemove,
            _ => Self::TokenPut,
        }
    }
}

/// # Safety
/// Rows contains the Wi-Fi facade's extra string, and value is valid for its length.
#[no_mangle]
pub unsafe extern "C" fn xp_store_last_connected(rows: *const Rows, value: Bytes) -> bool {
    let rows = &*rows;
    if same(rows.get(usize::MAX, 8), value) {
        return false;
    }
    rows.set(usize::MAX, 8, value);
    true
}
#[no_mangle]
pub extern "C" fn xp_store_string_length(
    len: u32,
    max_length: usize,
    string_max: usize,
    remaining: i32,
) -> bool {
    len as usize <= max_length
        && len as usize <= string_max
        && len <= i32::MAX as u32
        && remaining >= 0
        && len <= remaining as u32
}
