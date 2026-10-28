use super::{Action, UNSUPPORTED};
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    match action {
        Action::Size | Action::Align => 0,
        _ => UNSUPPORTED,
    }
}
