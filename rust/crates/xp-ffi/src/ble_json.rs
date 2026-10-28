//! BLE settings traversal; the document adapter supplies ArduinoJson conversions.
use super::ble::*;
use core::ffi::{c_char, c_void};
#[repr(C)]
pub struct JsonReader {
    pub root: unsafe extern "C" fn(*mut c_void),
    pub key: unsafe extern "C" fn(*mut c_void, *const c_char),
    pub index: unsafe extern "C" fn(*mut c_void, usize),
    pub array_len: unsafe extern "C" fn(*mut c_void) -> usize,
    pub u8_value: unsafe extern "C" fn(*mut c_void, *mut u8) -> bool,
    pub u32_value: unsafe extern "C" fn(*mut c_void, *mut u32) -> bool,
    pub text: unsafe extern "C" fn(*mut c_void) -> *const u8,
    pub is_null: unsafe extern "C" fn(*mut c_void) -> bool,
}
#[repr(C)]
pub struct JsonWriter {
    pub root: unsafe extern "C" fn(*mut c_void),
    pub key: unsafe extern "C" fn(*mut c_void, *const c_char),
    pub index: unsafe extern "C" fn(*mut c_void, usize),
    pub array: unsafe extern "C" fn(*mut c_void) -> bool,
    pub append_object: unsafe extern "C" fn(*mut c_void) -> usize,
    pub number: unsafe extern "C" fn(*mut c_void, u32),
    pub text: unsafe extern "C" fn(*mut c_void, *const u8),
    pub append_number: unsafe extern "C" fn(*mut c_void, u32),
}
const ENABLED: &core::ffi::CStr = c"blePageTurnerEnabled";
const PICK: &core::ffi::CStr = c"blePick";
const ADDR: &core::ffi::CStr = c"blePeerAddr";
const NAME: &core::ffi::CStr = c"blePeerName";
const PREV: &core::ffi::CStr = c"blePrevKeyUsage";
const NEXT: &core::ffi::CStr = c"bleNextKeyUsage";
const REMOTES: &core::ffi::CStr = c"bleRemotes";
unsafe fn read_key(r: &JsonReader, ctx: *mut c_void, key: &core::ffi::CStr) {
    (r.root)(ctx);
    (r.key)(ctx, key.as_ptr());
}
unsafe fn read_byte(r: &JsonReader, ctx: *mut c_void, key: &core::ffi::CStr, default: u8) -> u8 {
    read_key(r, ctx, key);
    let mut value = 0;
    if (r.u8_value)(ctx, &mut value) {
        value
    } else {
        default
    }
}
unsafe fn read_remote(r: &JsonReader, ctx: *mut c_void, index: usize) {
    read_key(r, ctx, REMOTES);
    (r.index)(ctx, index);
}
unsafe fn write_key(w: &JsonWriter, ctx: *mut c_void, key: &core::ffi::CStr) {
    (w.root)(ctx);
    (w.key)(ctx, key.as_ptr());
}
unsafe fn write_remote(w: &JsonWriter, ctx: *mut c_void, index: usize) {
    write_key(w, ctx, REMOTES);
    (w.index)(ctx, index);
}
/// # Safety
/// config must be writable. reader and ctx must support every callback for the call.
/// Document strings may alias Config strings; callbacks must not otherwise mutate Config.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_read_json(
    config: *mut Config,
    reader: *const JsonReader,
    ctx: *mut c_void,
) -> bool {
    let r = &*reader;
    (*config).enabled = u8::from(read_byte(r, ctx, ENABLED, 0) != 0);
    (*config).pick = u8::from(read_byte(r, ctx, PICK, 1) != 0);
    read_key(r, ctx, ADDR);
    let addr = (r.text)(ctx);
    copy_string(&mut (*config).peer_addr, addr);
    read_key(r, ctx, NAME);
    let name = (r.text)(ctx);
    copy_string(&mut (*config).peer_name, name);
    (*config).prev_key_usage = read_byte(r, ctx, PREV, 0);
    (*config).next_key_usage = read_byte(r, ctx, NEXT, 0);
    (*config).remote_count = 0;
    read_key(r, ctx, REMOTES);
    let count = (r.array_len)(ctx);
    for index in 0..count {
        if (*config).remote_count >= 4 {
            break;
        }
        read_remote(r, ctx, index);
        (r.key)(ctx, c"addr".as_ptr());
        let addr = (r.text)(ctx);
        if *addr == 0 {
            continue;
        }
        let mut table = RemoteTable::default();
        copy_string(&mut table.addr, addr);
        read_remote(r, ctx, index);
        (r.key)(ctx, c"binds".as_ptr());
        let slots = (r.array_len)(ctx);
        for slot in 0..slots {
            if table.count >= 8 {
                break;
            }
            read_remote(r, ctx, index);
            (r.key)(ctx, c"binds".as_ptr());
            (r.index)(ctx, slot);
            let mut binding = 0;
            if (r.u32_value)(ctx, &mut binding) && valid(binding) {
                table.bindings[usize::from(table.count)] = binding;
                table.count += 1;
            }
        }
        (*config).remotes[usize::from((*config).remote_count)] = table;
        (*config).remote_count += 1;
    }
    read_key(r, ctx, ENABLED);
    !(r.is_null)(ctx)
}
/// # Safety
/// config must be readable and writer/ctx must support every callback for the call.
/// The document borrows Config strings: Config must remain at its address until the
/// document is destroyed, as with the C++ facade.
#[no_mangle]
pub unsafe extern "C" fn xp_ble_write_json(
    config: *const Config,
    writer: *const JsonWriter,
    ctx: *mut c_void,
) {
    let w = &*writer;
    write_key(w, ctx, ENABLED);
    (w.number)(ctx, (*config).enabled.into());
    write_key(w, ctx, PICK);
    (w.number)(ctx, u32::from((*config).pick != 0));
    write_key(w, ctx, ADDR);
    (w.text)(ctx, (*config).peer_addr.as_ptr());
    write_key(w, ctx, NAME);
    (w.text)(ctx, (*config).peer_name.as_ptr());
    write_key(w, ctx, PREV);
    (w.number)(ctx, (*config).prev_key_usage.into());
    write_key(w, ctx, NEXT);
    (w.number)(ctx, (*config).next_key_usage.into());
    if (*config).remote_count == 0 {
        return;
    }
    write_key(w, ctx, REMOTES);
    if !(w.array)(ctx) {
        return;
    }
    for index in 0..usize::from((*config).remote_count.min(4)) {
        write_key(w, ctx, REMOTES);
        let stored_index = (w.append_object)(ctx);
        (w.key)(ctx, c"addr".as_ptr());
        (w.text)(ctx, (*config).remotes[index].addr.as_ptr());
        write_remote(w, ctx, stored_index);
        (w.key)(ctx, c"binds".as_ptr());
        (w.array)(ctx);
        for slot in 0..usize::from((*config).remotes[index].count.min(8)) {
            (w.append_number)(ctx, (*config).remotes[index].bindings[slot]);
        }
    }
}
