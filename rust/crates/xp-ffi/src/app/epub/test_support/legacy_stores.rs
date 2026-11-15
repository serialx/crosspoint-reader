use super::{abi::XpEpubLeaves, app_abi};
pub(super) unsafe fn save_state(leaves: &XpEpubLeaves) {
    unsafe { (leaves.state_save)() };
}
pub(super) unsafe fn add(leaves: &XpEpubLeaves, _: &app_abi::Resources, book: &app_abi::Book) {
    unsafe { (leaves.recents_add)(book) };
}
pub(super) unsafe fn remove(leaves: &XpEpubLeaves, path: app_abi::Text) -> bool {
    unsafe { (leaves.recents_remove)(path) != 0 }
}
pub(super) unsafe fn update_path(
    leaves: &XpEpubLeaves,
    from: app_abi::Text,
    to: app_abi::Text,
    old_cache: app_abi::Text,
    new_cache: app_abi::Text,
) {
    unsafe { (leaves.recents_update_path)(from, to, old_cache, new_cache) };
}
