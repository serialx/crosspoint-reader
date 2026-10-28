use super::*;
pub(super) unsafe fn dispatch(action: Action<'_>) -> u32 {
    super::uc8179::dispatch_x4(action, UC8279_X4)
}
