mod abi;
mod text;
mod transfer;
mod url;
use abi::*;

#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_wifi_guard_enter_retire_n7_s1_s2_w3b(
    radio: *const RadioLeaves,
    log: *const Diagnostics,
) -> GuardToken {
    let mut previous_mode = 1;
    if ((*radio).get_ps)(&mut previous_mode) != 0 {
        previous_mode = 1;
    }
    let error = ((*radio).set_ps)(0);
    if error != 0 {
        ((*log).emit)(
            Diagnostic::GuardEnterFailed,
            Bytes::EMPTY,
            Bytes::EMPTY,
            error as usize,
            0,
        );
    }
    GuardToken { previous_mode }
}

#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_wifi_guard_exit_retire_n7_s1_s2_w3b(
    token: GuardToken,
    radio: *const RadioLeaves,
    log: *const Diagnostics,
) {
    if token.previous_mode == 0 {
        return;
    }
    let error = ((*radio).set_ps)(token.previous_mode);
    if error != 0 {
        ((*log).emit)(
            Diagnostic::GuardRestoreFailed,
            Bytes::EMPTY,
            Bytes::EMPTY,
            error as usize,
            0,
        );
    }
}
