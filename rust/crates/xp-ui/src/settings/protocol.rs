// Private settings ABI identities, mirrored by xp_ui_settings.h.
pub const SMALL_LABELS_ON_TOUCH: u8 = 4;
const _: () = assert!(SMALL_LABELS_ON_TOUCH == 4);
pub const ROOT: u8 = 0;
pub const SECTION: u8 = 1;
pub const ABOUT: u8 = 2;
pub const BLE: u8 = 3;
pub const REMAP: u8 = 4;
pub const CLOCK: u8 = 5;
pub const SYNC: u8 = 6;
pub const HOME_BUTTON: u8 = 7;
pub const LAYOUTS: u8 = 8;
pub const LANGUAGE: u8 = 9;
pub const STATUS_BAR: u8 = 10;
pub const TIMEZONE: u8 = 11;
pub const RESET: u8 = 4;
pub const COMMIT: u8 = 5;
pub const SAVE_VALUE: u8 = 6;
pub const POPUP: u8 = 7;
pub const OPEN: u8 = 8;
pub const ACTION_NONE: u8 = 0;
pub const ACTION_REMAP_FRONT_BUTTONS: u8 = 1;
pub const ACTION_CUSTOMISE_STATUS_BAR: u8 = 2;
pub const ACTION_CLOCK_SETTINGS: u8 = 3;
pub const ACTION_KOREADER_SYNC: u8 = 4;
pub const ACTION_OPDS_BROWSER: u8 = 5;
pub const ACTION_NETWORK: u8 = 6;
pub const ACTION_BLUETOOTH: u8 = 7;
pub const ACTION_CLEAR_CACHE: u8 = 8;
pub const ACTION_CHECK_FOR_UPDATES: u8 = 9;
pub const ACTION_SD_FIRMWARE_UPDATE: u8 = 10;
pub const ACTION_LANGUAGE: u8 = 11;
pub const ACTION_DOWNLOAD_FONTS: u8 = 12;
pub const ACTION_TEXT_SETTINGS: u8 = 13;
pub const ACTION_PLUGINS: u8 = 14;
pub const ACTION_KEYBOARD_LAYOUTS: u8 = 15;
pub const ACTION_HOME_BUTTON: u8 = 16;
pub const ACTION_ABOUT: u8 = 17;
pub const CLOCK_TIMEZONE: u8 = 0;
pub const CLOCK_DST: u8 = 1;
pub const CLOCK_FORMAT: u8 = 2;
pub const CLOCK_SHOW_ON_HOME: u8 = 3;
pub const CLOCK_SYNC: u8 = 4;
pub const STATUS_CHAPTER_PAGE_COUNT: u8 = 0;
pub const STATUS_BOOK_PROGRESS_PERCENTAGE: u8 = 1;
pub const STATUS_PROGRESS_BAR: u8 = 2;
pub const STATUS_PROGRESS_BAR_THICKNESS: u8 = 3;
pub const STATUS_TITLE: u8 = 4;
pub const STATUS_BATTERY: u8 = 5;
pub const STATUS_XTC_STATUS_BAR: u8 = 6;
pub const STATUS_CLOCK: u8 = 7;
pub const BLE_VIEW_MENU: u8 = 0;
pub const BLE_VIEW_SCAN: u8 = 1;
pub const BLE_VIEW_PAIRED: u8 = 2;
pub const BLE_VIEW_MAPPING: u8 = 3;
pub const BLE_EVENT_DISCONNECTED: u8 = 0;
pub const BLE_EVENT_PRESS: u8 = 1;
pub const BLE_EVENT_RELEASE: u8 = 2;
pub const BLE_EVENT_REST: u8 = 3;
pub const BLE_EVENT_TICK: u8 = 4;
pub const BLE_EVENT_SAVE_FAILED: u8 = 5;
pub const BLE_EVENT_SAVED: u8 = 6;
pub const BLE_EFFECT_TOGGLE: u8 = 10;
pub const BLE_EFFECT_SCAN: u8 = 11;
pub const BLE_EFFECT_SHOW: u8 = 12;
pub const BLE_EFFECT_PAIR: u8 = 13;
pub const BLE_EFFECT_FORGET: u8 = 14;
pub const BLE_EFFECT_LEARN: u8 = 15;
pub const SECTION_GENERAL: u8 = 0;
pub const SECTION_DISPLAY: u8 = 1;
pub const SECTION_READER: u8 = 2;
pub const SECTION_CONTROLS: u8 = 3;
pub const SECTION_LIBRARY: u8 = 4;
pub const SECTION_NETWORK: u8 = 5;
pub const SECTION_COUNT: u8 = 6;
pub const SHARED_ROWS: u8 = 255;

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn c_header_identities() {
        let header = include_str!("../../../xp-ffi/include/xp_ui_settings.h");
        let identities = [
            ("XP_UI_SETTINGS_ROOT", ROOT),
            ("XP_UI_SETTINGS_SECTION", SECTION),
            ("XP_UI_SETTINGS_ABOUT", ABOUT),
            ("XP_UI_SETTINGS_BLE", BLE),
            ("XP_UI_SETTINGS_REMAP", REMAP),
            ("XP_UI_SETTINGS_CLOCK", CLOCK),
            ("XP_UI_SETTINGS_SYNC", SYNC),
            ("XP_UI_SETTINGS_HOME_BUTTON", HOME_BUTTON),
            ("XP_UI_SETTINGS_LAYOUTS", LAYOUTS),
            ("XP_UI_SETTINGS_LANGUAGE", LANGUAGE),
            ("XP_UI_SETTINGS_STATUS_BAR", STATUS_BAR),
            ("XP_UI_SETTINGS_TIMEZONE", TIMEZONE),
            ("XP_UI_SETTINGS_RESET", RESET),
            ("XP_UI_SETTINGS_COMMIT", COMMIT),
            ("XP_UI_SETTINGS_SAVE_VALUE", SAVE_VALUE),
            ("XP_UI_SETTINGS_POPUP", POPUP),
            ("XP_UI_SETTINGS_OPEN", OPEN),
            ("XP_UI_SETTINGS_ACTION_NONE", ACTION_NONE),
            (
                "XP_UI_SETTINGS_ACTION_REMAP_FRONT_BUTTONS",
                ACTION_REMAP_FRONT_BUTTONS,
            ),
            (
                "XP_UI_SETTINGS_ACTION_CUSTOMISE_STATUS_BAR",
                ACTION_CUSTOMISE_STATUS_BAR,
            ),
            (
                "XP_UI_SETTINGS_ACTION_CLOCK_SETTINGS",
                ACTION_CLOCK_SETTINGS,
            ),
            ("XP_UI_SETTINGS_ACTION_KOREADER_SYNC", ACTION_KOREADER_SYNC),
            ("XP_UI_SETTINGS_ACTION_OPDS_BROWSER", ACTION_OPDS_BROWSER),
            ("XP_UI_SETTINGS_ACTION_NETWORK", ACTION_NETWORK),
            ("XP_UI_SETTINGS_ACTION_BLUETOOTH", ACTION_BLUETOOTH),
            ("XP_UI_SETTINGS_ACTION_CLEAR_CACHE", ACTION_CLEAR_CACHE),
            (
                "XP_UI_SETTINGS_ACTION_CHECK_FOR_UPDATES",
                ACTION_CHECK_FOR_UPDATES,
            ),
            (
                "XP_UI_SETTINGS_ACTION_SD_FIRMWARE_UPDATE",
                ACTION_SD_FIRMWARE_UPDATE,
            ),
            ("XP_UI_SETTINGS_ACTION_LANGUAGE", ACTION_LANGUAGE),
            (
                "XP_UI_SETTINGS_ACTION_DOWNLOAD_FONTS",
                ACTION_DOWNLOAD_FONTS,
            ),
            ("XP_UI_SETTINGS_ACTION_TEXT_SETTINGS", ACTION_TEXT_SETTINGS),
            ("XP_UI_SETTINGS_ACTION_PLUGINS", ACTION_PLUGINS),
            (
                "XP_UI_SETTINGS_ACTION_KEYBOARD_LAYOUTS",
                ACTION_KEYBOARD_LAYOUTS,
            ),
            ("XP_UI_SETTINGS_ACTION_HOME_BUTTON", ACTION_HOME_BUTTON),
            ("XP_UI_SETTINGS_ACTION_ABOUT", ACTION_ABOUT),
            ("XP_UI_SETTINGS_CLOCK_TIMEZONE", CLOCK_TIMEZONE),
            ("XP_UI_SETTINGS_CLOCK_DST", CLOCK_DST),
            ("XP_UI_SETTINGS_CLOCK_FORMAT", CLOCK_FORMAT),
            ("XP_UI_SETTINGS_CLOCK_SHOW_ON_HOME", CLOCK_SHOW_ON_HOME),
            ("XP_UI_SETTINGS_CLOCK_SYNC", CLOCK_SYNC),
            (
                "XP_UI_SETTINGS_STATUS_CHAPTER_PAGE_COUNT",
                STATUS_CHAPTER_PAGE_COUNT,
            ),
            (
                "XP_UI_SETTINGS_STATUS_BOOK_PROGRESS_PERCENTAGE",
                STATUS_BOOK_PROGRESS_PERCENTAGE,
            ),
            ("XP_UI_SETTINGS_STATUS_PROGRESS_BAR", STATUS_PROGRESS_BAR),
            (
                "XP_UI_SETTINGS_STATUS_PROGRESS_BAR_THICKNESS",
                STATUS_PROGRESS_BAR_THICKNESS,
            ),
            ("XP_UI_SETTINGS_STATUS_TITLE", STATUS_TITLE),
            ("XP_UI_SETTINGS_STATUS_BATTERY", STATUS_BATTERY),
            (
                "XP_UI_SETTINGS_STATUS_XTC_STATUS_BAR",
                STATUS_XTC_STATUS_BAR,
            ),
            ("XP_UI_SETTINGS_STATUS_CLOCK", STATUS_CLOCK),
            ("XP_UI_SETTINGS_BLE_VIEW_MENU", BLE_VIEW_MENU),
            ("XP_UI_SETTINGS_BLE_VIEW_SCAN", BLE_VIEW_SCAN),
            ("XP_UI_SETTINGS_BLE_VIEW_PAIRED", BLE_VIEW_PAIRED),
            ("XP_UI_SETTINGS_BLE_VIEW_MAPPING", BLE_VIEW_MAPPING),
            (
                "XP_UI_SETTINGS_BLE_EVENT_DISCONNECTED",
                BLE_EVENT_DISCONNECTED,
            ),
            ("XP_UI_SETTINGS_BLE_EVENT_PRESS", BLE_EVENT_PRESS),
            ("XP_UI_SETTINGS_BLE_EVENT_RELEASE", BLE_EVENT_RELEASE),
            ("XP_UI_SETTINGS_BLE_EVENT_REST", BLE_EVENT_REST),
            ("XP_UI_SETTINGS_BLE_EVENT_TICK", BLE_EVENT_TICK),
            (
                "XP_UI_SETTINGS_BLE_EVENT_SAVE_FAILED",
                BLE_EVENT_SAVE_FAILED,
            ),
            ("XP_UI_SETTINGS_BLE_EVENT_SAVED", BLE_EVENT_SAVED),
            ("XP_UI_SETTINGS_BLE_EFFECT_TOGGLE", BLE_EFFECT_TOGGLE),
            ("XP_UI_SETTINGS_BLE_EFFECT_SCAN", BLE_EFFECT_SCAN),
            ("XP_UI_SETTINGS_BLE_EFFECT_SHOW", BLE_EFFECT_SHOW),
            ("XP_UI_SETTINGS_BLE_EFFECT_PAIR", BLE_EFFECT_PAIR),
            ("XP_UI_SETTINGS_BLE_EFFECT_FORGET", BLE_EFFECT_FORGET),
            ("XP_UI_SETTINGS_BLE_EFFECT_LEARN", BLE_EFFECT_LEARN),
            ("XP_UI_SETTINGS_SECTION_GENERAL", SECTION_GENERAL),
            ("XP_UI_SETTINGS_SECTION_DISPLAY", SECTION_DISPLAY),
            ("XP_UI_SETTINGS_SECTION_READER", SECTION_READER),
            ("XP_UI_SETTINGS_SECTION_CONTROLS", SECTION_CONTROLS),
            ("XP_UI_SETTINGS_SECTION_LIBRARY", SECTION_LIBRARY),
            ("XP_UI_SETTINGS_SECTION_NETWORK", SECTION_NETWORK),
            ("XP_UI_SETTINGS_SECTION_COUNT", SECTION_COUNT),
            ("XP_UI_SETTINGS_SHARED_ROWS", SHARED_ROWS),
        ];
        for (name, value) in identities {
            let tail = header.split(name).nth(1).unwrap().trim_start();
            let number = tail
                .strip_prefix('=')
                .unwrap()
                .trim_start()
                .split(|c: char| !c.is_ascii_digit())
                .next()
                .unwrap();
            assert_eq!(number.parse::<u8>().unwrap(), value, "{name}");
        }
    }
}
