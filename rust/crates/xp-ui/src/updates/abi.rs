use super::state::*;
use super::*;
use core::mem::{align_of, offset_of, size_of};
#[test]
fn wire_layout() {
    let pointer_bytes = size_of::<usize>();
    assert_eq!(size_of::<State>(), 116);
    assert_eq!(align_of::<State>(), 4);
    assert_eq!(offset_of!(State, list), 0);
    assert_eq!(offset_of!(State, groups), 40);
    assert_eq!(offset_of!(State, processed), 80);
    assert_eq!(offset_of!(State, total), 84);
    assert_eq!(offset_of!(State, file_index), 88);
    assert_eq!(offset_of!(State, file_count), 92);
    assert_eq!(offset_of!(State, last_percent), 96);
    assert_eq!(offset_of!(State, family_index), 100);
    assert_eq!(offset_of!(State, kind), 104);
    assert_eq!(offset_of!(State, phase), 105);
    assert_eq!(offset_of!(State, recovery), 106);
    assert_eq!(offset_of!(State, cancel_requested), 107);
    assert_eq!(offset_of!(State, home_requested), 108);
    assert_eq!(offset_of!(State, has_groups), 109);
    assert_eq!(offset_of!(State, popup_selected), 110);
    assert_eq!(offset_of!(State, popup_active), 111);
    assert_eq!(offset_of!(State, touch_active), 112);
    assert_eq!(offset_of!(State, reserved), 113);
    assert_eq!(size_of::<Effect>(), 8);
    assert_eq!(align_of::<Effect>(), 4);
    assert_eq!(offset_of!(Effect, action), 0);
    assert_eq!(offset_of!(Effect, repaint), 1);
    assert_eq!(offset_of!(Effect, immediate), 2);
    assert_eq!(offset_of!(Effect, reserved), 3);
    assert_eq!(Effect::default().reserved, 0);
    assert_eq!(offset_of!(Effect, value), 4);
    assert_eq!(size_of::<Layout>(), 96);
    assert_eq!(align_of::<Layout>(), 4);
    assert_eq!(offset_of!(Layout, list), 0);
    assert_eq!(offset_of!(Layout, progress_height), 80);
    assert_eq!(offset_of!(Layout, popup_padding), 82);
    assert_eq!(offset_of!(Layout, popup_gap), 84);
    assert_eq!(offset_of!(Layout, popup_side_margin), 86);
    assert_eq!(offset_of!(Layout, selection_padding), 88);
    assert_eq!(offset_of!(Layout, popup_radius), 90);
    assert_eq!(offset_of!(Layout, popup_border), 91);
    assert_eq!(offset_of!(Layout, reserved), 92);
    assert_eq!(size_of::<Services>(), 9 * pointer_bytes);
    assert_eq!(align_of::<Services>(), align_of::<usize>());
    assert_eq!(offset_of!(Services, rows), 0);
    assert_eq!(offset_of!(Services, context), 6 * pointer_bytes);
    assert_eq!(offset_of!(Services, text), 7 * pointer_bytes);
    assert_eq!(offset_of!(Services, text_height), 8 * pointer_bytes);
    assert_eq!(size_of::<Feedback>(), 156);
    assert_eq!(align_of::<Feedback>(), 4);
    assert_eq!(offset_of!(Feedback, list), 0);
    assert_eq!(offset_of!(Feedback, rows), 40);
    assert_eq!(offset_of!(Feedback, popup), 124);
    assert_eq!(offset_of!(Feedback, options), 132);
    assert_eq!(offset_of!(Feedback, last_percent), 148);
    assert_eq!(offset_of!(Feedback, rendered), 152);
    assert_eq!(offset_of!(Feedback, reserved), 153);
}

#[test]
fn wire_values() {
    let expected = include_str!("../../../../../test/ui_updates/wire.golden");
    let values = [
        ("XP_UI_UPDATE_OTA", Kind::Ota as u8),
        ("XP_UI_UPDATE_SD", Kind::Sd as u8),
        ("XP_UI_UPDATE_FONTS", Kind::Fonts as u8),
        ("XP_UI_UPDATE_CACHE", Kind::Cache as u8),
        ("XP_UI_UPDATE_WIFI_RESULT", Event::WifiResult as u8),
        ("XP_UI_UPDATE_OTA_CHECKED", Event::OtaChecked as u8),
        ("XP_UI_UPDATE_INSTALL_BEGIN", Event::InstallBegin as u8),
        ("XP_UI_UPDATE_INSTALLED", Event::Installed as u8),
        ("XP_UI_UPDATE_SHUTDOWN", Event::Shutdown as u8),
        ("XP_UI_UPDATE_PICKED", Event::Picked as u8),
        ("XP_UI_UPDATE_VALIDATED", Event::Validated as u8),
        ("XP_UI_UPDATE_CONFIRMED", Event::Confirmed as u8),
        ("XP_UI_UPDATE_PROGRESS", Event::Progress as u8),
        ("XP_UI_UPDATE_MANIFEST", Event::Manifest as u8),
        ("XP_UI_UPDATE_GROUP", Event::Group as u8),
        ("XP_UI_UPDATE_FONT_BEGIN", Event::FontBegin as u8),
        ("XP_UI_UPDATE_FONT_FILE", Event::FontFile as u8),
        ("XP_UI_UPDATE_FONT_FILE_DONE", Event::FontFileDone as u8),
        ("XP_UI_UPDATE_FONT_DONE", Event::FontDone as u8),
        ("XP_UI_UPDATE_FONT_ERROR", Event::FontError as u8),
        ("XP_UI_UPDATE_FONT_ABORTED", Event::FontAborted as u8),
        ("XP_UI_UPDATE_TRANSFER_INPUT", Event::TransferInput as u8),
        ("XP_UI_UPDATE_CACHE_BEGIN", Event::CacheBegin as u8),
        ("XP_UI_UPDATE_CACHE_RESULT", Event::CacheResult as u8),
        ("XP_UI_UPDATE_FONT_BATCH", Event::FontBatch as u8),
        ("XP_UI_UPDATE_SCENE_FAILED", Event::SceneFailed as u8),
        ("XP_UI_UPDATE_NONE", Action::None as u8),
        ("XP_UI_UPDATE_FINISH", Action::Finish as u8),
        ("XP_UI_UPDATE_PICKER", Action::Picker as u8),
        ("XP_UI_UPDATE_INSTALL", Action::Install as u8),
        ("XP_UI_UPDATE_FONT_RETRY", Action::FontRetry as u8),
        ("XP_UI_UPDATE_FONT_ACTIVATE", Action::FontActivate as u8),
        ("XP_UI_UPDATE_ENTER_GROUP", Action::EnterGroup as u8),
        ("XP_UI_UPDATE_CLEAR", Action::Clear as u8),
        ("XP_UI_UPDATE_RESTART", Action::Restart as u8),
        ("XP_UI_UPDATE_HOME", Action::Home as u8),
    ];
    assert_eq!(expected.lines().count(), values.len());
    for (line, (name, value)) in expected.lines().zip(values) {
        let (wire_name, wire_value) = line.split_once(' ').unwrap();
        assert_eq!(wire_name, name);
        assert_eq!(wire_value.parse::<u8>().unwrap(), value);
    }
}
