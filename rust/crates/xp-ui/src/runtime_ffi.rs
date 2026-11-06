//! Temporary native callers use the same policy as typed Rust owners.
use crate::{
    model::Input,
    runtime::{next_capacity, Publication, SceneGate},
};

/// # Safety
/// A non-null gate is aligned, initialized and exclusively borrowed for this call.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_scene_gate_reject(gate: *mut SceneGate) {
    if let Some(gate) = unsafe { gate.as_mut() } {
        gate.reject();
    }
}

/// # Safety
/// Same gate loan as `xp_ui_scene_gate_reject`.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_scene_gate_publish(gate: *mut SceneGate, current: u8) -> u8 {
    let Some(gate) = (unsafe { gate.as_mut() }) else {
        return 0;
    };
    if current > 1 {
        return 0;
    }
    u8::from(gate.publish(current != 0) == Publication::Current)
}

/// # Safety
/// The gate is exclusively borrowed; non-null input is aligned, initialized and
/// readable for this call, and does not overlap the gate.
#[no_mangle]
pub unsafe extern "C" fn xp_ui_scene_gate_input(gate: *mut SceneGate, input: *const Input) -> u8 {
    let (Some(gate), Some(input)) = (unsafe { gate.as_mut() }, unsafe { input.as_ref() }) else {
        return 3;
    };
    let admission = gate.input(input);
    u8::from(admission.consume) | (u8::from(admission.retry) << 1)
}

#[no_mangle]
pub extern "C" fn xp_ui_command_next_capacity(
    current: u16,
    minimum: u16,
    step: u16,
    limit: u16,
) -> u16 {
    next_capacity(current, minimum, step, limit).unwrap_or(0)
}
