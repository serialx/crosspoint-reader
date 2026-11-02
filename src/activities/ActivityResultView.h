#pragma once

#include <xp_app.h>

#include "ActivityResult.h"

enum class ActivityResultKind : uint8_t {
  None = XP_APP_RESULT_NONE,
  Wifi = XP_APP_RESULT_WIFI,
  Keyboard = XP_APP_RESULT_KEYBOARD,
  Menu = XP_APP_RESULT_MENU,
  Chapter = XP_APP_RESULT_CHAPTER,
  Percent = XP_APP_RESULT_PERCENT,
  Interval = XP_APP_RESULT_INTERVAL,
  Page = XP_APP_RESULT_PAGE,
  Clipping = XP_APP_RESULT_CLIPPING,
  Progress = XP_APP_RESULT_PROGRESS,
  NetworkMode = XP_APP_RESULT_NETWORK_MODE,
  Footnote = XP_APP_RESULT_FOOTNOTE,
  FilePath = XP_APP_RESULT_FILE_PATH,
};

// The moved result outlives child destruction and synchronous parent delivery.
inline XpAppResultView activityResultView(const ActivityResult& result) {
  static_assert(std::variant_size_v<ResultVariant> == XP_APP_RESULT_COUNT);
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_NONE, ResultVariant>, std::monostate>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_WIFI, ResultVariant>, WifiResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_KEYBOARD, ResultVariant>, KeyboardResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_MENU, ResultVariant>, MenuResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_CHAPTER, ResultVariant>, ChapterResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_PERCENT, ResultVariant>, PercentResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_INTERVAL, ResultVariant>, IntervalResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_PAGE, ResultVariant>, PageResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_CLIPPING, ResultVariant>, ClippingResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_PROGRESS, ResultVariant>, ProgressChangeResult>,
                "Activity result tag order changed");
  static_assert(
      std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_NETWORK_MODE, ResultVariant>, NetworkModeResult>,
      "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_FOOTNOTE, ResultVariant>, FootnoteResult>,
                "Activity result tag order changed");
  static_assert(std::is_same_v<std::variant_alternative_t<XP_APP_RESULT_FILE_PATH, ResultVariant>, FilePathResult>,
                "Activity result tag order changed");
  const auto text = [](const std::string& value) -> XpAppText {
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
  };
  XpAppResultView view{};
  view.original = &result;
  view.kind = static_cast<uint8_t>(result.data.index());
  view.cancelled = result.isCancelled;
  switch (static_cast<ActivityResultKind>(view.kind)) {
    case ActivityResultKind::None:
      break;
    case ActivityResultKind::Wifi: {
      const auto& value = std::get<WifiResult>(result.data);
      view.value.wifi = {text(value.ssid), text(value.ip), value.connected};
      break;
    }
    case ActivityResultKind::Keyboard:
      view.value.text = text(std::get<KeyboardResult>(result.data).text);
      break;
    case ActivityResultKind::Menu: {
      const auto& value = std::get<MenuResult>(result.data);
      view.value.menu = {value.action, value.orientation, value.pageTurnOption};
      break;
    }
    case ActivityResultKind::Chapter: {
      const auto& value = std::get<ChapterResult>(result.data);
      view.value.chapter = {value.spineIndex, text(value.anchor)};
      break;
    }
    case ActivityResultKind::Percent:
      view.value.signed_value = std::get<PercentResult>(result.data).percent;
      break;
    case ActivityResultKind::Interval:
      view.value.unsigned_value = std::get<IntervalResult>(result.data).value;
      break;
    case ActivityResultKind::Page:
      view.value.unsigned_value = std::get<PageResult>(result.data).page;
      break;
    case ActivityResultKind::Clipping: {
      const auto& value = std::get<ClippingResult>(result.data);
      view.value.clipping = {{text(value.text), &value.text},
                             value.startPageOffset,
                             value.endPageOffset,
                             value.startWordIndex,
                             value.endWordIndex,
                             value.wordCount,
                             value.startOffset,
                             value.endOffset,
                             static_cast<uint8_t>(value.action)};
      break;
    }
    case ActivityResultKind::Progress: {
      const auto& value = std::get<ProgressChangeResult>(result.data);
      view.value.progress = {value.spineIndex,       value.page,
                             value.totalPages,       text(value.xpath),
                             value.percentage,       value.visibleTextOffset,
                             value.hasSavedProgress, value.hasVisibleTextOffset};
      break;
    }
    case ActivityResultKind::NetworkMode:
      view.value.signed_value = static_cast<int32_t>(std::get<NetworkModeResult>(result.data).mode);
      break;
    case ActivityResultKind::Footnote:
      view.value.text = text(std::get<FootnoteResult>(result.data).href);
      break;
    case ActivityResultKind::FilePath:
      view.value.text = text(std::get<FilePathResult>(result.data).path);
      break;
  }
  return view;
}
