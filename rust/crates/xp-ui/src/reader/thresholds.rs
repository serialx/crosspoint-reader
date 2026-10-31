//! Reader policy thresholds, shared by input and build/render consumers.
pub(super) const GO_HOME_MS: u32 = 1000;
pub(super) const SKIP_HOLD_MS: u32 = 700;
pub(super) const BACKGROUND_BUILD_MIN_FREE_HEAP: u32 = 32 * 1024;
pub(super) const BACKGROUND_BUILD_MIN_MAX_ALLOC: u32 = 16 * 1024;
pub(super) const BUILD_ROOM_WAIT_MS: u32 = 5000;
pub(super) const BUILD_ROOM_STEP_MS: u32 = 50;
pub(super) const RENDER_MIN_FREE_HEAP: u32 = 24 * 1024;
pub(super) const PARTIAL_REBUILD_START_MARGIN: i32 = 15;
pub(super) const BUILD_POPUP_PAGE_THRESHOLD: i32 = 20;
pub(super) const BUILD_POPUP_BYTE_THRESHOLD: u32 = 96 * 1024;
pub(super) const BUILD_POPUP_DEADLINE_MS: u32 = 1000;

#[cfg(test)]
mod tests {
    #[test]
    fn native_back_threshold_matches_rust() {
        let header = include_str!("../../../../../src/activities/reader/ReaderUtils.h");
        let definition = header
            .lines()
            .find_map(|line| line.strip_prefix("constexpr unsigned long GO_HOME_MS = "))
            .expect("native Back threshold");
        let native: u32 = definition.trim_end_matches(';').parse().unwrap();
        assert_eq!(native, super::GO_HOME_MS);
        assert!(header.contains("constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;"));
    }
}
