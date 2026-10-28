//! BLE application policy. All storage is caller-owned or fixed boot-lifetime state.
//! The radio/clock/host leaves and diagnostic formatting remain in the C++ adapter.

use core::ffi::c_char;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RemoteTable {
    pub addr: [u8; 18],
    pub count: u8,
    pub bindings: [u32; 8],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Config {
    pub enabled: u8,
    pub pick: u8,
    pub peer_addr: [u8; 18],
    pub peer_name: [u8; 32],
    pub prev_key_usage: u8,
    pub next_key_usage: u8,
    pub remotes: [RemoteTable; 4],
    pub remote_count: u8,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Heap {
    pub free_bytes: usize,
    pub largest_block: usize,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Profile {
    pub stack_bytes: usize,
    pub start_free_bytes: usize,
    pub minimum_largest: usize,
    pub home_start_largest: usize,
}
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Why {
    Ok,
    Off,
    NotReading,
    NoBond,
    PageNotShown,
    Indexing,
    BuildNeedsHeap,
    StorageBusy,
    IdleNoLink,
    HeapLow,
    HeapInPieces,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RadioInputs {
    pub phase: u8,
    pub enabled: bool,
    pub location: u8,
    pub page_shown: bool,
    pub book_indexing: bool,
    pub storage_busy: bool,
    pub held_for_build: bool,
    pub idle_stopped: bool,
    pub linked: bool,
    pub idle_ms: u32,
    pub heap: Heap,
    pub connection_busy: bool,
    pub has_bond: bool,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct NoteInputs {
    pub entered: bool,
    pub reading: bool,
    pub enabled: bool,
    pub idle_stopped: bool,
    pub linked: bool,
    pub refused: bool,
    pub running_ms: u32,
    pub acknowledged: bool,
    pub has_bond: bool,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct HoldWait {
    pub code: u32,
    pub at_ms: u32,
    pub tap: u8,
    pub hold: u8,
    pub active: bool,
}

pub const HOLD_MS: u32 = 700;
pub const IDLE_MS: u32 = 30_000;
pub const RESTART_MAGIC: u32 = 0x42485231;
const CODE_MASK: u32 = 0xffffff;
const HOLD_BIT: u32 = 1 << 24;
const ANY_REPORT: u32 = 1 << 25;
pub const READER: u8 = 1;
pub const HOME: u8 = 3;

pub fn verdict(i: &RadioInputs, p: &Profile) -> Why {
    use Why::*;
    let fits = i.heap.free_bytes >= p.start_free_bytes && i.heap.largest_block >= p.minimum_largest;
    match i.phase {
        0 => {
            if !i.enabled {
                return Off;
            }
            if i.storage_busy {
                return StorageBusy;
            }
            if i.location == 2 {
                return Ok;
            }
            if i.location == HOME {
                if p.home_start_largest == 0 || !i.has_bond {
                    return NotReading;
                }
                if !i.page_shown {
                    return PageNotShown;
                }
                return if i.idle_stopped { IdleNoLink } else { Ok };
            }
            if i.location != READER {
                return NotReading;
            }
            if !i.has_bond {
                return NoBond;
            }
            if !i.page_shown {
                return PageNotShown;
            }
            if i.held_for_build {
                return BuildNeedsHeap;
            }
            if i.idle_stopped {
                return IdleNoLink;
            }
            if i.book_indexing {
                return Indexing;
            }
            Ok
        }
        1 => {
            if i.storage_busy {
                return StorageBusy;
            }
            if i.location == HOME {
                return if fits && i.heap.largest_block >= p.home_start_largest {
                    Ok
                } else {
                    HeapLow
                };
            }
            if fits {
                Ok
            } else if i.heap.free_bytes >= 65536 {
                HeapInPieces
            } else {
                HeapLow
            }
        }
        2 => {
            if i.storage_busy {
                StorageBusy
            } else if i.heap.largest_block >= p.minimum_largest {
                Ok
            } else {
                HeapInPieces
            }
        }
        3 => {
            if !i.enabled {
                return Off;
            }
            if i.storage_busy {
                return StorageBusy;
            }
            if !i.linked && !i.connection_busy && i.idle_ms >= IDLE_MS {
                IdleNoLink
            } else {
                Ok
            }
        }
        _ => Off,
    }
}
pub fn next_note(note: u8, i: &NoteInputs) -> u8 {
    if !i.enabled || !i.reading {
        return 0;
    }
    if i.entered {
        return u8::from(!i.linked && !i.idle_stopped && i.has_bond);
    }
    match note {
        1 => {
            if i.linked {
                0
            } else if i.refused || !i.has_bond || i.running_ms >= 20000 {
                2
            } else {
                1
            }
        }
        2..=4 => {
            if i.acknowledged {
                0
            } else {
                note
            }
        }
        _ => 0,
    }
}
pub fn page_action(c: &Config, usage: u8, mods: u8) -> u8 {
    if c.enabled == 0 || usage == 0 || mods != 0 {
        return 0;
    }
    if c.prev_key_usage != 0 && usage == c.prev_key_usage {
        return 2;
    }
    if c.next_key_usage != 0 && usage == c.next_key_usage {
        return 1;
    }
    if c.prev_key_usage == 0 && matches!(usage, 0x50 | 0x4b | 0x52 | 0x2a | 0xea | 0xb6) {
        return 2;
    }
    if c.next_key_usage == 0 && matches!(usage, 0x4f | 0x4e | 0x51 | 0x2c | 0x28 | 0xe9 | 0xb5) {
        return 1;
    }
    0
}
pub fn valid(b: u32) -> bool {
    b & !(CODE_MASK | HOLD_BIT | ANY_REPORT | 0xf0000000) == 0
        && b & 255 != 0
        && (1..=8).contains(&(b >> 28))
}
pub fn matches(b: u32, code: u32, hold: bool) -> bool {
    let mask = if b & ANY_REPORT != 0 {
        0x0100ffff
    } else {
        0x01ffffff
    };
    (b ^ (code | if hold { HOLD_BIT } else { 0 })) & mask == 0
}
pub fn lookup(t: &RemoteTable, code: u32, hold: bool) -> u8 {
    t.bindings
        .iter()
        .take(t.count as usize)
        .find(|&&b| matches(b, code, hold))
        .map_or(0, |b| (b >> 28) as u8)
}
pub fn clear(t: &mut RemoteTable, action: u8) -> bool {
    let mut kept = 0;
    for index in 0..usize::from(t.count.min(8)) {
        let b = t.bindings[index];
        if (b >> 28) as u8 != action {
            t.bindings[kept] = b;
            kept += 1;
        }
    }
    let changed = kept != t.count as usize;
    t.count = kept as u8;
    changed
}
pub fn learn(t: &mut RemoteTable, action: u8, code: u32, hold: bool) -> bool {
    clear(t, action);
    let mut kept = 0;
    for index in 0..usize::from(t.count.min(8)) {
        let b = t.bindings[index];
        if !matches(b, code, hold) {
            t.bindings[kept] = b;
            kept += 1;
        }
    }
    t.count = kept as u8;
    if kept >= 8 {
        return false;
    }
    t.bindings[kept] =
        (code & CODE_MASK) | if hold { HOLD_BIT } else { 0 } | u32::from(action) << 28;
    t.count += 1;
    true
}
pub fn raw_edge(
    t: &RemoteTable,
    code: u32,
    pressed: bool,
    at_ms: u32,
    legacy: u8,
    w: &mut HoldWait,
) -> u8 {
    if pressed {
        let tap = lookup(t, code, false);
        let now = if tap != 0 { tap } else { legacy };
        let hold = lookup(t, code, true);
        if hold == 0 {
            return now;
        }
        *w = HoldWait {
            code,
            at_ms,
            tap: now,
            hold,
            active: true,
        };
        return 0;
    }
    if !w.active || w.code != code {
        return 0;
    }
    w.active = false;
    if at_ms.wrapping_sub(w.at_ms) < HOLD_MS {
        w.tap
    } else {
        w.hold
    }
}
pub fn poll_hold(w: &mut HoldWait, now: u32) -> u8 {
    if !w.active || now.wrapping_sub(w.at_ms) < HOLD_MS {
        return 0;
    }
    w.active = false;
    w.hold
}

// Strings are NUL-terminated C strings, borrowed for the call. Comparison stops at
// the fixed address width, including for an overlong name loaded from settings.
/// # Safety
/// Both pointers must be readable through len bytes or their first NUL, whichever comes first.
pub unsafe fn str_equal(a: *const u8, b: *const u8, len: usize) -> bool {
    for i in 0..len {
        let x = *a.add(i);
        let y = *b.add(i);
        if x != y {
            return false;
        }
        if x == 0 {
            return true;
        }
    }
    true
}
/// # Safety
/// N must be nonzero. src must be readable through N - 1 bytes or its first NUL.
/// It may equal dst exactly; other overlap is not supported.
pub unsafe fn copy_string<const N: usize>(dst: &mut [u8; N], src: *const u8) {
    if dst.as_ptr() == src {
        dst[N - 1] = 0;
        return;
    }
    let mut end = false;
    for (i, byte) in dst.iter_mut().take(N - 1).enumerate() {
        let value = if end { 0 } else { *src.add(i) };
        *byte = value;
        end |= value == 0;
    }
    dst[N - 1] = 0;
}
pub type DefaultTable = unsafe extern "C" fn(*const c_char) -> *const RemoteTable;
/// # Safety
/// tables must contain count initialized entries (count <= 4). addr may be null;
/// otherwise strings must be readable C strings. default_table must return a stable table.
pub unsafe fn table_for(
    tables: *const RemoteTable,
    count: u8,
    addr: *const u8,
    name: *const c_char,
    default_table: DefaultTable,
) -> *const RemoteTable {
    if addr.is_null() || *addr == 0 {
        return core::ptr::null();
    }
    for i in 0..usize::from(count.min(4)) {
        let t = tables.add(i);
        if str_equal((*t).addr.as_ptr(), addr, 18) {
            return t;
        }
    }
    default_table(name)
}
/// # Safety
/// tables must contain count initialized entries (count <= 4) and be exclusively
/// borrowed. addr must be a readable C string and must not alias overwritten table storage.
pub unsafe fn forget_remote(tables: *mut RemoteTable, count: &mut u8, addr: *const u8) -> bool {
    for i in 0..usize::from((*count).min(4)) {
        if !str_equal((*tables.add(i)).addr.as_ptr(), addr, 18) {
            continue;
        }
        for j in i + 1..usize::from((*count).min(4)) {
            *tables.add(j - 1) = *tables.add(j);
        }
        *count -= 1;
        return true;
    }
    false
}

/// # Safety
/// i and p must point to initialized, readable ABI structures.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_verdict(i: *const RadioInputs, p: *const Profile) -> u8 {
    verdict(&*i, &*p) as u8
}
/// # Safety
/// i must point to an initialized, readable NoteInputs structure.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_next_note(note: u8, i: *const NoteInputs) -> u8 {
    next_note(note, &*i)
}
/// # Safety
/// c must point to an initialized, readable Config.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_page_action(c: *const Config, usage: u8, mods: u8) -> u8 {
    page_action(&*c, usage, mods)
}
#[no_mangle]
pub extern "C" fn xp_ble_valid(b: u32) -> bool {
    valid(b)
}
#[no_mangle]
pub extern "C" fn xp_ble_matches(b: u32, code: u32, hold: bool) -> bool {
    matches(b, code, hold)
}
/// # Safety
/// t must point to an initialized, readable table with count <= 8.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_lookup(t: *const RemoteTable, code: u32, hold: bool) -> u8 {
    lookup(&*t, code, hold)
}
/// # Safety
/// t must be a readable table with count <= 8; out must be writable and must not alias t.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_find(t: *const RemoteTable, action: u8, out: *mut u32) -> bool {
    for &b in (*t).bindings.iter().take((*t).count as usize) {
        if (b >> 28) as u8 == action {
            *out = b;
            return true;
        }
    }
    false
}
/// # Safety
/// t must be an initialized table with count <= 8, exclusively borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_clear(t: *mut RemoteTable, action: u8) -> bool {
    clear(&mut *t, action)
}
/// # Safety
/// t must be an initialized table with count <= 8, exclusively borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_learn(
    t: *mut RemoteTable,
    action: u8,
    code: u32,
    hold: bool,
) -> bool {
    learn(&mut *t, action, code, hold)
}
/// # Safety
/// t must be readable with count <= 8; w must be initialized, writable, and must not alias t.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_raw_edge(
    t: *const RemoteTable,
    code: u32,
    pressed: bool,
    at_ms: u32,
    legacy: u8,
    w: *mut HoldWait,
) -> u8 {
    raw_edge(&*t, code, pressed, at_ms, legacy, &mut *w)
}
/// # Safety
/// w must be initialized and exclusively borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_poll_hold(w: *mut HoldWait, now: u32) -> u8 {
    poll_hold(&mut *w, now)
}
/// # Safety
/// The same pointer, string and callback lifetime requirements as table_for apply.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_table_for(
    t: *const RemoteTable,
    count: u8,
    addr: *const u8,
    name: *const c_char,
    default_table: DefaultTable,
) -> *const RemoteTable {
    table_for(t, count, addr, name, default_table)
}
/// # Safety
/// tables must provide four writable slots; count must be writable and <= 4.
/// addr may be null; otherwise strings must be readable and must not alias a new
/// slot being written. default_table must return a stable readable table or null.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_editable_table(
    tables: *mut RemoteTable,
    count: *mut u8,
    addr: *const u8,
    name: *const c_char,
    default_table: DefaultTable,
) -> *mut RemoteTable {
    if addr.is_null() || *addr == 0 {
        return core::ptr::null_mut();
    }
    for i in 0..usize::from((*count).min(4)) {
        let t = tables.add(i);
        if str_equal((*t).addr.as_ptr(), addr, 18) {
            return t;
        }
    }
    if *count >= 4 {
        return core::ptr::null_mut();
    }
    let t = tables.add(*count as usize);
    *count += 1;
    let seed = default_table(name);
    *t = if seed.is_null() {
        RemoteTable::default()
    } else {
        *seed
    };
    copy_string(&mut (*t).addr, addr);
    t
}
/// # Safety
/// The same requirements as forget_remote apply; count must also be writable and non-null.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_forget_remote(
    t: *mut RemoteTable,
    count: *mut u8,
    addr: *const u8,
) -> bool {
    forget_remote(t, &mut *count, addr)
}
#[no_mangle]
pub extern "C" fn xp_ble_rearm(idle: bool, entered: bool, requested: bool, painted: bool) -> bool {
    entered || requested || (painted && !idle)
}
#[no_mangle]
pub extern "C" fn xp_ble_should_restart(why: u8, count: u8, spent: bool) -> bool {
    !spent && why == Why::HeapInPieces as u8 && count >= 3
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Router {
    pub table: *const RemoteTable,
    pub chosen: bool,
    pub linked: bool,
    pub wait: HoldWait,
}
impl Router {
    pub const EMPTY: Self = Self {
        table: core::ptr::null(),
        chosen: false,
        linked: false,
        wait: HoldWait {
            code: 0,
            at_ms: 0,
            tap: 0,
            hold: 0,
            active: false,
        },
    };
}
/// # Safety
/// router must be initialized and exclusively borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_follow(router: *mut Router, linked: bool) {
    if (*router).linked != linked {
        *router = Router {
            linked,
            ..Router::EMPTY
        };
    }
}
/// # Safety
/// count and pending must be writable, initialized, non-overlapping outputs.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_tracker_refused(
    why: u8,
    count: *mut u8,
    magic: u32,
    pending: *mut bool,
) -> bool {
    if why != Why::HeapInPieces as u8 {
        *count = 0;
        *pending = false;
    } else {
        *count = (*count).saturating_add(1);
    }
    let restart = xp_ble_should_restart(why, *count, magic == RESTART_MAGIC);
    if restart {
        *pending = true;
    }
    restart
}
