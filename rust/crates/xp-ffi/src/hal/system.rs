use core::ffi::c_void;
#[repr(u32)]
pub enum HapticOp {
    Enable,
    Intensity,
    Begin,
    Pulse,
    Pattern,
}
type HapticCall = unsafe extern "C" fn(HapticOp, u8) -> bool;
#[no_mangle]
pub unsafe extern "C" fn xp_hal_haptic(
    call: HapticCall,
    long_press: bool,
    enabled: bool,
    pressed: bool,
    intensity: u8,
) {
    let gain = match intensity {
        0 => 153,
        1 => 191,
        _ => 224,
    };
    if long_press {
        if !enabled {
            return;
        }
        call(HapticOp::Enable, 1);
        call(HapticOp::Intensity, gain);
        if call(HapticOp::Begin, 0) {
            call(HapticOp::Pattern, 0);
        }
    } else {
        call(HapticOp::Enable, u8::from(enabled));
        if enabled && pressed && call(HapticOp::Begin, 0) {
            call(HapticOp::Intensity, gain);
            call(HapticOp::Pulse, 0);
        }
    }
}
#[repr(u32)]
pub enum Op {
    Panic,
    Clear,
    Sanitize,
    ClearLogs,
    Info,
    Open,
    Write,
    Close,
    Consume,
    Size,
}
type Call = unsafe extern "C" fn(*mut c_void, Op) -> usize;
#[no_mangle]
pub unsafe extern "C" fn xp_hal_system_begin(ctx: *mut c_void, call: Call) {
    if call(ctx, Op::Panic) == 0 {
        call(ctx, Op::Clear);
    } else if call(ctx, Op::Sanitize) != 0 {
        call(ctx, Op::ClearLogs);
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_system_save(
    ctx: *mut c_void,
    call: Call,
    written: *mut usize,
) -> u32 {
    let Some(written) = written.as_mut() else {
        return 0;
    };
    if call(ctx, Op::Panic) == 0 {
        return 0;
    }
    call(ctx, Op::Info);
    if call(ctx, Op::Open) == 0 {
        return 3;
    }
    *written = call(ctx, Op::Write);
    call(ctx, Op::Close);
    if *written == call(ctx, Op::Size) {
        call(ctx, Op::Consume);
        1
    } else {
        2
    }
}
#[no_mangle]
pub extern "C" fn xp_hal_system_is_panic(panic: bool, watchdog: bool, marker: u32) -> bool {
    panic || (watchdog && marker == 0x50414e49)
}
