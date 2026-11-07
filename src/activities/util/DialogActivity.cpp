#include "DialogActivity.h"

#include <I18n.h>

#include <cstdio>

#include "activities/OwnedLegacyActivity.h"
#include "activities/reader/RustReaderDrawing.h"

void DialogActivity::layout(XpUiUtilityLayout& out) const { sampleReaderUtilityLayout(renderer, mappedInput, out); }
XpUiMetrics DialogActivity::metrics() const {
  return {
      &renderer,
      [](void* context, int32_t font, const uint8_t* text, uint16_t length, uint8_t style) {
        const auto provider = static_cast<GfxRenderer*>(context)->textProvider();
        return xp_native_width(&provider, font, text, length, style, -1);
      },
      [](void* context, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(context)->getLineHeight(font); }};
}
void DialogActivity::footer(XpUiRect clip, const XpAppText* labels) const {
  drawReaderUtilityFooter(renderer, mappedInput, clip, labels);
}
void DialogActivity::log(Log event, uint32_t bytes) const {
  switch (event) {
    case Log::Enter:
      LOG_DBG("ACT", "Entering activity: %s", name.c_str());
      break;
    case Log::Exit:
      LOG_DBG("ACT", "Exiting activity: %s", name.c_str());
      break;
    case Log::Arena:
      LOG_ERR("RUI", "Cannot allocate utility scene: %u bytes", static_cast<unsigned>(bytes));
      break;
    case Log::Build:
      LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
      break;
    case Log::Paint:
      LOG_ERR("RUI", "Invalid scene in %s", name.c_str());
      break;
    case Log::MessageBuild:
      LOG_ERR("RUI", "Cannot build full-screen message");
      break;
  }
}
namespace {
DialogActivity& dialog(void* object) { return *nativeActivity(object)->dialog(); }
}  // namespace
void xp_dialog_launch(void* object, XpDialogLaunch* out) { *out = dialog(object).launch(); }
void xp_dialog_release_launch(void* object) { dialog(object).releaseLaunch(); }
void xp_dialog_layout(void* object, XpUiUtilityLayout* out) { dialog(object).layout(*out); }
XpUiMetrics xp_dialog_metrics(void* object) { return dialog(object).metrics(); }
void xp_dialog_footer(void* object, XpUiRect clip, const XpAppText* labels) { dialog(object).footer(clip, labels); }
void xp_dialog_format(uint16_t format, int32_t value, uint8_t asUnsigned, uint8_t* out, size_t size) {
  if (asUnsigned)
    snprintf(reinterpret_cast<char*>(out), size, I18N.get(static_cast<StrId>(format)), static_cast<unsigned>(value));
  else
    snprintf(reinterpret_cast<char*>(out), size, "%d", value);
}
void xp_dialog_log(void* object, uint8_t event, uint32_t bytes) {
  dialog(object).log(static_cast<DialogActivity::Log>(event), bytes);
}
void xp_dialog_deliver(const XpAppLegacy* legacy, void* parent, uint8_t kind, uint32_t value) {
  ActivityResult result;
  enum class Kind : uint8_t { Cancelled, Confirmed, Interval, MessageDismissed };
  switch (static_cast<Kind>(kind)) {
    case Kind::Cancelled:
      result.isCancelled = true;
      break;
    case Kind::Interval:
      result.data = IntervalResult{value};
      break;
    case Kind::Confirmed:
    case Kind::MessageDismissed:
      break;
  }
  legacy->deliver_result(parent, &result);
}
