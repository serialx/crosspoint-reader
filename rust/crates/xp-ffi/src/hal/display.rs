use core::ffi::c_void;
#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Op {
    X3,
    Begin,
    Skip,
    Wake,
    Resync,
    Buffer,
    Async,
    Refresh,
    Gray,
    GrayMode,
}
type Call = unsafe extern "C" fn(*mut c_void, Op, u32, bool, u32) -> u32;
#[no_mangle]
pub unsafe extern "C" fn xp_hal_display_begin(
    ctx: *mut c_void,
    call: Call,
    x3: bool,
    seamless: bool,
) {
    if x3 {
        call(ctx, Op::X3, 0, false, 0);
    }
    call(ctx, Op::Begin, 0, false, 0);
    if seamless {
        call(ctx, Op::Skip, 0, false, 0);
        return;
    }
    if matches!(call(ctx, Op::Wake, 0, false, 0), 0 | 1 | 3) {
        call(ctx, Op::Resync, 0, false, 0);
    }
}
#[no_mangle]
pub unsafe extern "C" fn xp_hal_display_refresh(
    ctx: *mut c_void,
    call: Call,
    x3: bool,
    op: u32,
    refresh: u32,
    off: bool,
    gray: u32,
) -> bool {
    let op = match op {
        5 => Op::Buffer,
        6 => Op::Async,
        7 => Op::Refresh,
        8 => Op::Gray,
        9 => Op::GrayMode,
        _ => return false,
    };
    if x3 && refresh == 1 {
        // The mode-aware gray overload uses the SDK default (zero passes).
        let passes = u32::from(!matches!(op, Op::GrayMode));
        call(ctx, Op::Resync, passes, false, 0);
    }
    let refresh = if matches!(op, Op::GrayMode) {
        refresh
    } else {
        refresh.min(2)
    };
    call(ctx, op, refresh, off, gray) != 0
}
