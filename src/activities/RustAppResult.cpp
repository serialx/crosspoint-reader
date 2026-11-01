#include "RustAppResult.h"

namespace {
XpAppText text(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
std::string owned(XpAppText value) {
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
}  // namespace

XpAppResultView rustAppResultView(const ActivityResult& result) {
  XpAppResultView view{};
  view.cancelled = result.isCancelled;
  std::visit(
      [&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, WifiResult>) {
          view.kind = XP_APP_RESULT_WIFI;
          view.value.wifi = {text(data.ssid), text(data.ip), data.connected};
        } else if constexpr (std::is_same_v<T, KeyboardResult>) {
          view.kind = XP_APP_RESULT_KEYBOARD;
          view.value.keyboard = text(data.text);
        } else if constexpr (std::is_same_v<T, MenuResult>) {
          view.kind = XP_APP_RESULT_MENU;
          view.value.menu = {data.action, data.orientation, data.pageTurnOption};
        } else if constexpr (std::is_same_v<T, ChapterResult>) {
          view.kind = XP_APP_RESULT_CHAPTER;
          view.value.chapter = {text(data.anchor), data.spineIndex};
        } else if constexpr (std::is_same_v<T, PercentResult>) {
          view.kind = XP_APP_RESULT_PERCENT;
          view.value.percent = data.percent;
        } else if constexpr (std::is_same_v<T, IntervalResult>) {
          view.kind = XP_APP_RESULT_INTERVAL;
          view.value.interval = data.value;
        } else if constexpr (std::is_same_v<T, PageResult>) {
          view.kind = XP_APP_RESULT_PAGE;
          view.value.page = data.page;
        } else if constexpr (std::is_same_v<T, ClippingResult>) {
          view.kind = XP_APP_RESULT_CLIPPING;
          view.value.clipping = {text(data.text),      data.startOffset,   data.endOffset,
                                 data.startPageOffset, data.endPageOffset, data.startWordIndex,
                                 data.endWordIndex,    data.wordCount,     static_cast<uint8_t>(data.action)};
        } else if constexpr (std::is_same_v<T, ProgressChangeResult>) {
          view.kind = XP_APP_RESULT_PROGRESS;
          view.value.progress = {text(data.xpath),      data.spineIndex,          data.page,
                                 data.totalPages,       data.percentage,          data.visibleTextOffset,
                                 data.hasSavedProgress, data.hasVisibleTextOffset};
        } else if constexpr (std::is_same_v<T, NetworkModeResult>) {
          view.kind = XP_APP_RESULT_NETWORK_MODE;
          view.value.network_mode = static_cast<int32_t>(data.mode);
        } else if constexpr (std::is_same_v<T, FootnoteResult>) {
          view.kind = XP_APP_RESULT_FOOTNOTE;
          view.value.footnote = text(data.href);
        } else if constexpr (std::is_same_v<T, FilePathResult>) {
          view.kind = XP_APP_RESULT_FILE_PATH;
          view.value.file_path = text(data.path);
        } else {
          static_assert(std::is_same_v<T, std::monostate>);
          view.kind = XP_APP_RESULT_NONE;
        }
      },
      result.data);
  return view;
}

ActivityResult nativeAppResult(const XpAppResultView& view) {
  ActivityResult result;
  result.isCancelled = view.cancelled != 0;
  switch (view.kind) {
    case XP_APP_RESULT_NONE:
      break;
    case XP_APP_RESULT_WIFI:
      result.data = WifiResult{view.value.wifi.connected != 0, owned(view.value.wifi.ssid), owned(view.value.wifi.ip)};
      break;
    case XP_APP_RESULT_KEYBOARD:
      result.data = KeyboardResult{owned(view.value.keyboard)};
      break;
    case XP_APP_RESULT_MENU:
      result.data = MenuResult{view.value.menu.action, view.value.menu.orientation, view.value.menu.page_turn};
      break;
    case XP_APP_RESULT_CHAPTER:
      result.data = ChapterResult{view.value.chapter.spine, owned(view.value.chapter.anchor)};
      break;
    case XP_APP_RESULT_PERCENT:
      result.data = PercentResult{view.value.percent};
      break;
    case XP_APP_RESULT_INTERVAL:
      result.data = IntervalResult{view.value.interval};
      break;
    case XP_APP_RESULT_PAGE:
      result.data = PageResult{view.value.page};
      break;
    case XP_APP_RESULT_CLIPPING: {
      const auto& c = view.value.clipping;
      result.data = ClippingResult{static_cast<ClippingResult::Action>(c.action),
                                   owned(c.text),
                                   c.start_page,
                                   c.end_page,
                                   c.start_word,
                                   c.end_word,
                                   c.word_count,
                                   c.start_offset,
                                   c.end_offset};
      break;
    }
    case XP_APP_RESULT_PROGRESS: {
      const auto& p = view.value.progress;
      result.data = ProgressChangeResult{
          p.spine,         p.page, p.total_pages, owned(p.xpath), p.percentage, p.saved != 0, p.has_visible_offset != 0,
          p.visible_offset};
      break;
    }
    case XP_APP_RESULT_NETWORK_MODE:
      result.data = NetworkModeResult{static_cast<NetworkMode>(view.value.network_mode)};
      break;
    case XP_APP_RESULT_FOOTNOTE:
      result.data = FootnoteResult{owned(view.value.footnote)};
      break;
    case XP_APP_RESULT_FILE_PATH:
      result.data = FilePathResult{owned(view.value.file_path)};
      break;
    case XP_APP_RESULT_TRANSFER_FAILED:
    default:
      result.isCancelled = true;
      break;
  }
  return result;
}
