use core::ffi::c_void;
use core::mem::{align_of, size_of};
use xp_display::constants::*;
use xp_display::{Bus, Panel, Uc8253};

#[repr(C)]
pub struct DisplayBus {
    abi_version: u32,
    struct_size: u32,
    ctx: *mut c_void,
    cmd: Option<unsafe extern "C" fn(*mut c_void, u8)>,
    data8: Option<unsafe extern "C" fn(*mut c_void, u8)>,
    data_span: Option<unsafe extern "C" fn(*mut c_void, *const u8, u16)>,
    cmd_data: Option<unsafe extern "C" fn(*mut c_void, u8, *const u8, u16)>,
    txn_begin: Option<unsafe extern "C" fn(*mut c_void)>,
    txn_cmd: Option<unsafe extern "C" fn(*mut c_void, u8)>,
    txn_data: Option<unsafe extern "C" fn(*mut c_void, u8)>,
    txn_write: Option<unsafe extern "C" fn(*mut c_void, *const u8, u16)>,
    txn_end: Option<unsafe extern "C" fn(*mut c_void)>,
    reset: Option<unsafe extern "C" fn(*mut c_void, u16)>,
    reset_level: Option<unsafe extern "C" fn(*mut c_void, u8)>,
    busy_level: Option<unsafe extern "C" fn(*mut c_void) -> u8>,
    busy_wait: Option<unsafe extern "C" fn(*mut c_void, u32, u32)>,
    refresh_wait: Option<unsafe extern "C" fn(*mut c_void, u32)>,
    delay_ms: Option<unsafe extern "C" fn(*mut c_void, u32)>,
    delay_us: Option<unsafe extern "C" fn(*mut c_void, u32)>,
    clock_ms: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    probe: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> u32>,
    diagnostic: Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32, *const u8, u32)>,
}

impl DisplayBus {
    fn valid(&self) -> bool {
        self.abi_version == ABI_VERSION
            && self.struct_size as usize == size_of::<Self>()
            && self.cmd.is_some()
            && self.data8.is_some()
            && self.data_span.is_some()
            && self.cmd_data.is_some()
            && self.txn_begin.is_some()
            && self.txn_cmd.is_some()
            && self.txn_data.is_some()
            && self.txn_write.is_some()
            && self.txn_end.is_some()
            && self.reset.is_some()
            && self.reset_level.is_some()
            && self.busy_level.is_some()
            && self.busy_wait.is_some()
            && self.refresh_wait.is_some()
            && self.delay_ms.is_some()
            && self.delay_us.is_some()
            && self.clock_ms.is_some()
            && self.probe.is_some()
    }
}
impl Bus for DisplayBus {
    fn cmd(&mut self, command: u8) {
        if let Some(callback) = self.cmd {
            unsafe { callback(self.ctx, command) }
        }
    }
    fn data8(&mut self, value: u8) {
        if let Some(callback) = self.data8 {
            unsafe { callback(self.ctx, value) }
        }
    }
    fn data_span(&mut self, data: &[u8]) {
        if let Some(callback) = self.data_span {
            unsafe { callback(self.ctx, data.as_ptr(), data.len() as u16) }
        }
    }
    fn cmd_data(&mut self, command: u8, data: &[u8]) {
        if let Some(callback) = self.cmd_data {
            unsafe { callback(self.ctx, command, data.as_ptr(), data.len() as u16) }
        }
    }
    fn txn_begin(&mut self) {
        if let Some(callback) = self.txn_begin {
            unsafe { callback(self.ctx) }
        }
    }
    fn txn_cmd(&mut self, command: u8) {
        if let Some(callback) = self.txn_cmd {
            unsafe { callback(self.ctx, command) }
        }
    }
    fn txn_data(&mut self, value: u8) {
        if let Some(callback) = self.txn_data {
            unsafe { callback(self.ctx, value) }
        }
    }
    fn txn_write(&mut self, data: &[u8]) {
        if let Some(callback) = self.txn_write {
            unsafe { callback(self.ctx, data.as_ptr(), data.len() as u16) }
        }
    }
    fn txn_end(&mut self) {
        if let Some(callback) = self.txn_end {
            unsafe { callback(self.ctx) }
        }
    }
    fn reset(&mut self, settle_ms: u16) {
        if let Some(callback) = self.reset {
            unsafe { callback(self.ctx, settle_ms) }
        }
    }
    fn reset_level(&mut self, level: u8) {
        if let Some(callback) = self.reset_level {
            unsafe { callback(self.ctx, level) }
        }
    }
    fn busy_level(&mut self) -> u8 {
        if let Some(callback) = self.busy_level {
            unsafe { callback(self.ctx) }
        } else {
            0
        }
    }
    fn busy_wait(&mut self, polarity: u32, tag: u32) {
        if let Some(callback) = self.busy_wait {
            unsafe { callback(self.ctx, polarity, tag) }
        }
    }
    fn refresh_wait(&mut self, tag: u32) {
        if let Some(callback) = self.refresh_wait {
            unsafe { callback(self.ctx, tag) }
        }
    }
    fn delay_ms(&mut self, duration: u32) {
        if let Some(callback) = self.delay_ms {
            unsafe { callback(self.ctx, duration) }
        }
    }
    fn delay_us(&mut self, duration: u32) {
        if let Some(callback) = self.delay_us {
            unsafe { callback(self.ctx, duration) }
        }
    }
    fn clock_ms(&mut self) -> u32 {
        if let Some(callback) = self.clock_ms {
            unsafe { callback(self.ctx) }
        } else {
            0
        }
    }
    fn probe(&mut self, kind: u32, arg: u32) -> u32 {
        if let Some(callback) = self.probe {
            unsafe { callback(self.ctx, kind, arg) }
        } else {
            0
        }
    }
}

#[repr(C)]
pub struct DisplayConfig {
    abi_version: u32,
    struct_size: u32,
    controller: u32,
    variant: u32,
    width: u16,
    height: u16,
    flags: u32,
    controller_config: *const c_void,
    controller_config_size: u32,
}
#[repr(C)]
pub struct DisplayRequest {
    struct_size: u32,
    operation: u32,
    mode: u32,
    flags: u32,
    arg: u32,
    x: u16,
    y: u16,
    width: u16,
    height: u16,
    data: *const u8,
    data_len: u32,
    previous: *const u8,
    previous_len: u32,
    lut: *const u8,
    lut_len: u32,
}

#[no_mangle]
pub extern "C" fn xp_display_abi_version() -> u32 {
    ABI_VERSION
}
#[no_mangle]
pub extern "C" fn xp_display_state_size(controller: u32) -> u32 {
    if controller == UC8253_X3 {
        size_of::<Uc8253>() as u32
    } else {
        0
    }
}
#[no_mangle]
pub extern "C" fn xp_display_state_align(controller: u32) -> u32 {
    if controller == UC8253_X3 {
        align_of::<Uc8253>() as u32
    } else {
        0
    }
}

/// # Safety
/// Storage is exclusive, aligned writable memory of size bytes. Config and any
/// controller-specific immutable LUT storage remain valid through drop.
#[no_mangle]
pub unsafe extern "C" fn xp_display_init(
    storage: *mut c_void,
    size: u32,
    config: *const DisplayConfig,
) -> u32 {
    if storage.is_null()
        || config.is_null()
        || !(storage as usize).is_multiple_of(align_of::<Uc8253>())
        || size < size_of::<Uc8253>() as u32
    {
        return INVALID;
    }
    let config = &*config;
    if config.abi_version != ABI_VERSION
        || config.struct_size as usize != size_of::<DisplayConfig>()
        || config.width == 0
        || !config.width.is_multiple_of(8)
        || config.height == 0
        || config.variant != 0
        || config.flags != 0
    {
        return INVALID;
    }
    if config.controller != UC8253_X3 || !config.controller_config.is_null() {
        return UNSUPPORTED;
    }
    storage.cast::<Uc8253>().write(Uc8253 {
        width: config.width,
        height: config.height,
    });
    OK
}

/// # Safety
/// Storage came from successful init and has not been dropped.
#[no_mangle]
pub unsafe extern "C" fn xp_display_drop(storage: *mut c_void) {
    if !storage.is_null() {
        core::ptr::drop_in_place(storage.cast::<Uc8253>());
    }
}

/// # Safety
/// Storage is a live initialized controller and no mutable call is concurrent.
#[no_mangle]
pub unsafe extern "C" fn xp_display_capabilities(storage: *const c_void, gray_mode: u32) -> u32 {
    if storage.is_null() {
        return 0;
    }
    (&*storage.cast::<Uc8253>()).capabilities(gray_mode)
}

/// # Safety
/// Storage is initialized and exclusive. All descriptors and nonnull buffers
/// cover their declared sizes. Callbacks do not reenter or retain Rust borrows.
#[no_mangle]
pub unsafe extern "C" fn xp_display_execute(
    storage: *mut c_void,
    bus: *const DisplayBus,
    request: *const DisplayRequest,
    result: *mut u32,
) -> u32 {
    if storage.is_null() || request.is_null() || bus.is_null() {
        return INVALID;
    }
    let request = &*request;
    if request.struct_size as usize != size_of::<DisplayRequest>() || !(&*bus).valid() {
        return INVALID;
    }
    // The descriptor is copied; C owns ctx and its transaction for this call.
    let mut bus = core::ptr::read(bus);
    let panel = &mut *storage.cast::<Uc8253>();
    if !result.is_null() {
        *result = RESULT_COMMITTED;
    }
    match request.operation {
        OP_BEGIN => panel.begin(&mut bus),
        OP_QUERY => {}
        _ => return UNSUPPORTED,
    }
    OK
}

const _: () = assert!(size_of::<Uc8253>() <= STATE_BYTES as usize);
const _: () = assert!(align_of::<Uc8253>() <= STATE_ALIGN as usize);
