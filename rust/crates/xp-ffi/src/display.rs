use core::ffi::c_void;
use core::mem::{align_of, size_of};
use core::sync::atomic::{AtomicU32, Ordering};
use xp_display::constants::*;
use xp_display::Bus;
#[cfg(feature = "display-uc8253")]
use xp_display::X3Config;

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
    fn valid(&self, controller: u32) -> bool {
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
            && (controller == UC8279C_A4 || self.refresh_wait.is_some())
            && self.delay_ms.is_some()
            && self.delay_us.is_some()
            && self.clock_ms.is_some()
            && self.probe.is_some()
    }
}
impl Bus for &DisplayBus {
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

#[cfg(feature = "display-paper-mono")]
mod paper_mono;
#[cfg(any(feature = "display-ssd2677", feature = "display-paper-mono"))]
mod retained;
#[cfg(feature = "display-ssd1677")]
mod ssd1677;
#[cfg(feature = "display-ssd2677")]
mod ssd2677;
#[cfg(any(feature = "display-uc8179", feature = "display-uc8279-x4"))]
mod uc8179;
#[cfg(feature = "display-uc8253")]
mod uc8253;
#[cfg(feature = "display-uc8279-x3")]
mod uc8279_x3;
#[cfg(feature = "display-uc8279-x4")]
mod uc8279_x4;
#[cfg(feature = "display-uc8279c-a4")]
mod uc8279c_a4;

#[cfg(any(
    feature = "display-uc8179",
    feature = "display-uc8279-x3",
    feature = "display-uc8279-x4",
    feature = "display-uc8279c-a4"
))]
mod ultrachip;

#[repr(C)]
pub struct Platform {
    pub abi_version: u32,
    pub struct_size: u32,
    pub ctx: *mut c_void,
    pub alloc: Option<unsafe extern "C" fn(*mut c_void, u32, u32) -> *mut c_void>,
    pub free: Option<unsafe extern "C" fn(*mut c_void, *mut c_void)>,
}
#[repr(C)]
pub struct InitOptions {
    pub abi_version: u32,
    pub struct_size: u32,
    pub controller_config_version: u32,
    pub platform: *const Platform,
}
#[repr(C)]
struct Header {
    controller: u32,
    generation: AtomicU32,
}
impl Header {
    fn new(controller: u32) -> Self {
        Self {
            controller,
            generation: AtomicU32::new(0),
        }
    }
}
// Each controller has a repr(C) state starting with Header. Only the panel field
// is mutably borrowed during execute, so generation remains concurrently readable.
// Some operations are unused when only a subset of controllers is enabled.
#[allow(dead_code)]
enum Action<'a> {
    Size,
    Align,
    Init {
        storage: *mut c_void,
        config: &'a DisplayConfig,
        options: &'a InitOptions,
    },
    Drop(*mut c_void),
    Capabilities {
        storage: *const c_void,
        mode: u32,
    },
    Execute {
        storage: *mut c_void,
        bus: &'a mut &'a DisplayBus,
        request: &'a DisplayRequest,
        result: *mut u32,
    },
    Extra {
        storage: *mut c_void,
        operation: u32,
        payload: *const c_void,
        size: u32,
        version: u32,
    },
}
// Specialize the action at each C entry point instead of copying the action
// descriptor through a shared stack frame. Each port owns its arm and module.
#[inline(always)]
unsafe fn dispatch(controller: u32, action: Action<'_>) -> u32 {
    match controller {
        #[cfg(feature = "display-uc8253")]
        UC8253_X3 => uc8253::dispatch(action),
        #[cfg(feature = "display-ssd1677")]
        SSD1677 => ssd1677::dispatch(action),
        #[cfg(feature = "display-uc8179")]
        UC8179 => uc8179::dispatch(action),
        #[cfg(feature = "display-uc8279-x3")]
        UC8279_X3 => uc8279_x3::dispatch(action),
        #[cfg(feature = "display-uc8279-x4")]
        UC8279_X4 => uc8279_x4::dispatch(action),
        #[cfg(feature = "display-uc8279c-a4")]
        UC8279C_A4 => uc8279c_a4::dispatch(action),
        #[cfg(feature = "display-ssd2677")]
        SSD2677 => ssd2677::dispatch(action),
        #[cfg(feature = "display-paper-mono")]
        PAPER_MONO => paper_mono::dispatch(action),
        _ => match action {
            Action::Size | Action::Align => 0,
            _ => UNSUPPORTED,
        },
    }
}
#[no_mangle]
pub extern "C" fn xp_display_abi_version() -> u32 {
    ABI_VERSION
}
#[no_mangle]
pub extern "C" fn xp_display_state_size(controller: u32) -> u32 {
    unsafe { dispatch(controller, Action::Size) }
}
#[no_mangle]
pub extern "C" fn xp_display_state_align(controller: u32) -> u32 {
    unsafe { dispatch(controller, Action::Align) }
}

/// # Safety
/// Storage is exclusive writable memory of size bytes. Config and all retained
/// controller resources remain valid until drop. No concurrent init/drop calls.
#[no_mangle]
pub unsafe extern "C" fn xp_display_init(
    storage: *mut c_void,
    size: u32,
    config: *const DisplayConfig,
) -> u32 {
    xp_display_init_v2(storage, size, config, core::ptr::null())
}
/// # Safety
/// Same as xp_display_init. Options and platform are valid; the platform table
/// and its context remain valid until drop. Allocations are owned by the panel.
#[no_mangle]
pub unsafe extern "C" fn xp_display_init_v2(
    storage: *mut c_void,
    size: u32,
    config: *const DisplayConfig,
    options: *const InitOptions,
) -> u32 {
    if storage.is_null() || config.is_null() {
        return INVALID;
    }
    let config = &*config;
    if config.abi_version != ABI_VERSION
        || config.struct_size as usize != size_of::<DisplayConfig>()
    {
        return INVALID;
    }
    let needed = xp_display_state_size(config.controller);
    let align = xp_display_state_align(config.controller);
    if needed == 0 || align == 0 {
        return UNSUPPORTED;
    }
    if size < needed || !(storage as usize).is_multiple_of(align as usize) {
        return INVALID;
    }
    let default = InitOptions {
        abi_version: 1,
        struct_size: size_of::<InitOptions>() as u32,
        controller_config_version: 1,
        platform: core::ptr::null(),
    };
    let options = if options.is_null() {
        &default
    } else {
        &*options
    };
    if options.abi_version != 1 || options.struct_size as usize != size_of::<InitOptions>() {
        return INVALID;
    }
    if !options.platform.is_null() {
        let platform = &*options.platform;
        if platform.abi_version != 1
            || platform.struct_size as usize != size_of::<Platform>()
            || platform.alloc.is_none()
            || platform.free.is_none()
        {
            return INVALID;
        }
    }
    dispatch(
        config.controller,
        Action::Init {
            storage,
            config,
            options,
        },
    )
}
/// # Safety
/// Storage is initialized and no other call, including abort, overlaps drop.
#[no_mangle]
pub unsafe extern "C" fn xp_display_drop(storage: *mut c_void) {
    if !storage.is_null() {
        dispatch(
            (*storage.cast::<Header>()).controller,
            Action::Drop(storage),
        );
    }
}
/// # Safety
/// Storage is initialized and no mutable controller call overlaps this query.
#[no_mangle]
pub unsafe extern "C" fn xp_display_capabilities(storage: *const c_void, mode: u32) -> u32 {
    if storage.is_null() {
        return 0;
    }
    dispatch(
        (*storage.cast::<Header>()).controller,
        Action::Capabilities { storage, mode },
    )
}
/// # Safety
/// Storage is initialized. May overlap execute but never init/drop. This only
/// increments generation; a controller decides where optional work may stop.
#[no_mangle]
pub unsafe extern "C" fn xp_display_abort(storage: *mut c_void) -> u32 {
    if storage.is_null() {
        return 0;
    }
    let generation = &(*storage.cast::<Header>()).generation;
    #[cfg(target_has_atomic = "32")]
    {
        generation.fetch_add(1, Ordering::AcqRel).wrapping_add(1)
    }
    #[cfg(not(target_has_atomic = "32"))]
    {
        // ESP32-C3 lacks atomic RMW instructions. The SDK uses the platform's
        // compiler atomic runtime, as its C++ atomic counters already do.
        extern "C" {
            fn xp_display_platform_increment(value: *mut u32) -> u32;
        }
        xp_display_platform_increment(generation.as_ptr())
    }
}
/// # Safety
/// Same lifetime and concurrency contract as xp_display_abort.
#[no_mangle]
pub unsafe extern "C" fn xp_display_generation(storage: *const c_void) -> u32 {
    if storage.is_null() {
        return 0;
    }
    (*storage.cast::<Header>())
        .generation
        .load(Ordering::Acquire)
}
/// # Safety
/// Storage is initialized and exclusive except for atomic generation accesses.
/// Descriptors and all buffers cover their declared sizes; callbacks cannot
/// reenter Rust or retain borrows. The bus table itself is borrowed for the call.
#[no_mangle]
pub unsafe extern "C" fn xp_display_execute(
    storage: *mut c_void,
    bus: *const DisplayBus,
    request: *const DisplayRequest,
    result: *mut u32,
) -> u32 {
    if !result.is_null() {
        *result = 0;
    }
    if storage.is_null() || bus.is_null() || request.is_null() {
        return INVALID;
    }
    let request = &*request;
    let controller = (*storage.cast::<Header>()).controller;
    if request.struct_size as usize != size_of::<DisplayRequest>() || !(&*bus).valid(controller) {
        return INVALID;
    }
    let mut bus = &*bus;
    dispatch(
        controller,
        Action::Execute {
            storage,
            bus: &mut bus,
            request,
            result,
        },
    )
}
/// # Safety
/// Storage is initialized and exclusive. Payload covers size readable bytes
/// and follows the controller-specific versioned operation contract.
#[no_mangle]
pub unsafe extern "C" fn xp_display_extra(
    storage: *mut c_void,
    operation: u32,
    payload: *const c_void,
    size: u32,
    version: u32,
) -> u32 {
    if storage.is_null() || (payload.is_null() && size != 0) {
        return INVALID;
    }
    dispatch(
        (*storage.cast::<Header>()).controller,
        Action::Extra {
            storage,
            operation,
            payload,
            size,
            version,
        },
    )
}
/// # Safety
/// Config and direct point to writable storage for one X3Config and five LUT
/// pointers respectively. Config borrows direct until its controller is dropped.
#[cfg(feature = "display-uc8253")]
#[no_mangle]
pub unsafe extern "C" fn xp_display_x3_default_config(
    config: *mut X3Config,
    direct: *mut *const u8,
) {
    if config.is_null() || direct.is_null() {
        return;
    }
    let pointers = xp_display::direct_luts();
    core::ptr::copy_nonoverlapping(pointers.as_ptr(), direct, 5);
    let mut value = xp_display::default_config();
    value.direct = direct;
    config.write(value);
}

#[cfg(feature = "reader-strips")]
#[repr(C)]
pub struct DisplaySource {
    pub struct_size: u32,
    pub ctx: *mut c_void,
    pub scratch: *mut u8,
    pub capacity: u32,
    pub rows: u16,
    pub width: u16,
    pub paint: Option<unsafe extern "C" fn(*mut c_void, *mut u8, u16, u16) -> u32>,
}
#[cfg(feature = "reader-strips")]
impl xp_display::frame::Source for DisplaySource {
    fn rows(&self) -> u16 {
        self.rows
    }
    fn paint(&mut self, y: u16, rows: u16) -> Result<&[u8], u32> {
        let n = usize::from(self.width / 8) * usize::from(rows);
        if n > self.capacity as usize {
            return Err(INVALID);
        }
        let callback = self.paint.ok_or(INVALID)?;
        let status = unsafe { callback(self.ctx, self.scratch, y, rows) };
        if status != 0 {
            return Err(status);
        }
        Ok(unsafe { core::slice::from_raw_parts(self.scratch, n) })
    }
}
/// # Safety
/// Same initialized storage/bus contract as xp_display_execute. Source owns exclusive
/// scratch and a frozen scene through this call. Paint may use disjoint raster state,
/// but must not reenter this display controller, retain scratch or unwind.
#[cfg(feature = "reader-strips")]
#[no_mangle]
pub unsafe extern "C" fn xp_display_execute_source(
    storage: *mut c_void,
    bus: *const DisplayBus,
    request: *const DisplayRequest,
    source: *mut DisplaySource,
    result: *mut u32,
) -> u32 {
    if !result.is_null() {
        *result = 0;
    }
    if storage.is_null() || bus.is_null() || request.is_null() || source.is_null() {
        return INVALID;
    }
    let request = &*request;
    let source = &mut *source;
    if request.struct_size as usize != size_of::<DisplayRequest>()
        || !(&*bus).valid((*storage.cast::<Header>()).controller)
        || source.struct_size as usize != size_of::<DisplaySource>()
        || source.scratch.is_null()
        || source.paint.is_none()
        || source.rows == 0
        || source.width == 0
        || !source.width.is_multiple_of(8)
        || u32::from(source.width / 8) * u32::from(source.rows) > source.capacity
    {
        return INVALID;
    }
    let mut bus = &*bus;
    let status = match (*storage.cast::<Header>()).controller {
        #[cfg(feature = "display-uc8253")]
        UC8253_X3 => uc8253::execute_source(storage, &mut bus, request, source),
        #[cfg(feature = "display-ssd1677")]
        SSD1677 => ssd1677::execute_source(storage, &mut bus, request, source),
        _ => Err(UNSUPPORTED),
    };
    match status {
        Ok(value) => {
            if !result.is_null() {
                *result = value;
            }
            OK
        }
        Err(status) => status,
    }
}
