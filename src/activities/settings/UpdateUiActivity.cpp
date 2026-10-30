#include "UpdateUiActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
namespace fui = freeink::ui;
UpdateUiActivity::ResourceLock::ResourceLock(UpdateUiActivity& owner) : mutex(owner.resourceMutex) {
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}
UpdateUiActivity::ResourceLock::~ResourceLock() { xSemaphoreGiveRecursive(mutex); }
UpdateUiActivity::~UpdateUiActivity() { vSemaphoreDelete(resourceMutex); }
void UpdateUiActivity::render(RenderLock&&) {
  XpUiScene scene{};
  bool painted = false;
  {
    ResourceLock resources(*this);
    if (buildRustScene(scene)) {
      const auto paint = rustResources();
      painted = renderer.drawUiScene(scene, &paint);
      if (!painted) LOG_ERR("RUI", "Invalid scene in %s", name.c_str());
    } else {
      LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
    }
    releaseText();
    if (painted)
      publishRustScene(scene);
    else
      rejectRustScene();
  }
  if (!painted) return;
  switch (static_cast<XpUiRefresh>(scene.refresh)) {
    case XP_UI_NONE:
      break;
    case XP_UI_FAST:
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
    case XP_UI_HALF:
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      break;
    case XP_UI_FULL:
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
      break;
  }
}
UpdateUiActivity::UpdateUiActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                                   XpUiUpdateKind kind, bool recovery)
    : RustActivityAdapter(name, renderer, input), textTarget(makeUiTarget(renderer)) {
  xp_ui_updates_init(&model, kind, recovery);
}
uint8_t UpdateUiActivity::phase() const {
  ModelLock lock(*const_cast<UpdateUiActivity*>(this));
  return model.phase;
}
int UpdateUiActivity::selectedIndex() const {
  ModelLock lock(*const_cast<UpdateUiActivity*>(this));
  return model.list.selected;
}
void UpdateUiActivity::notify(XpUiUpdateEvent event, uint32_t a, uint32_t b) {
  ModelLock lock(*this);
  xp_ui_updates_notify(&model, event, a, b);
}
uint8_t UpdateUiActivity::serviceResult(XpUiUpdateEvent event, uint32_t a, uint32_t b) {
  ModelLock lock(*this);
  return xp_ui_updates_result(&model, event, a, b);
}
void UpdateUiActivity::closeRouting() {
  ModelLock lock(*this);
  publication.ready = false;
}
bool UpdateUiActivity::preventAutoSleep() {
  ModelLock lock(*this);
  return xp_ui_updates_sleep(&model);
}
bool UpdateUiActivity::skipLoopDelay() {
  ModelLock lock(*this);
  return xp_ui_updates_delay(&model);
}
bool UpdateUiActivity::reserve(uint16_t count) {
  if (count <= capacity) return true;
  if (count > 128) return false;
  auto replacement = makeUniqueNoThrow<XpUiCommand[]>(count);
  if (!replacement) {
    LOG_ERR("RUI", "Cannot allocate %u update command bytes", unsigned(count * sizeof(XpUiCommand)));
    return false;
  }
  commands = std::move(replacement);
  capacity = count;
  return true;
}
void UpdateUiActivity::onEnter() { Activity::onEnter(); }
void UpdateUiActivity::onExit() {
  closeRouting();
  commands.reset();
  capacity = 0;
  Activity::onExit();
}
void UpdateUiActivity::loop() {
  {
    ResourceLock resources(*this);
    inputRowCount = rowCount();
    inputFamilyCount = familyCount();
  }
  RustActivityAdapter::loop();
}
XpUiInterest UpdateUiActivity::rustInterest(uint32_t now) const { return xp_ui_updates_interest(&model, now); }
XpUiEffect UpdateUiActivity::updateRust(const XpUiInput& input) {
  const auto oldPhase = model.phase;
  const bool popupActive = model.popup_active != 0;
  const bool list = model.kind == XP_UI_UPDATE_FONTS && (model.phase == 2 || model.phase == 3);
  const auto& nav = model.phase == 2 ? model.groups : model.list;
  const int drawn = nav.drawn > 0 ? nav.drawn : 1;
  pending = xp_ui_updates_input(&model, &input, &feedback, publication.ready, inputRowCount, inputFamilyCount);
  if (oldPhase != model.phase) {
    publication.ready = false;
  }
  pendingHaptic = (input.touch_flags & XP_UI_TOUCH_RELEASED) && publication.ready &&
                  ((popupActive && !model.popup_active) || pending.action == XP_UI_UPDATE_FONT_ACTIVATE ||
                   pending.action == XP_UI_UPDATE_ENTER_GROUP);
  if (list && pending.action == XP_UI_UPDATE_NONE && !(input.released & (XP_UI_BACK | XP_UI_CONFIRM)) &&
      (input.swipe == XP_UI_SWIPE_UP || input.swipe == XP_UI_SWIPE_DOWN)) {
    XpUiEffect effect{};
    effect.kind = XP_UI_SCROLL;
    effect.value = input.swipe == XP_UI_SWIPE_UP ? drawn : -drawn;
    return effect;
  }
  return {};
}
void UpdateUiActivity::applyRustEffect(const XpUiEffect& uiEffect) {
  // Service callbacks can repaint before they return; preserve their ordering.
  XpUiUpdateEffect effect{};
  bool haptic = false;
  {
    ModelLock lock(*this);
    effect = pending;
    haptic = pendingHaptic;
    pending = {};
    pendingHaptic = false;
  }
  if (uiEffect.kind == XP_UI_SCROLL)
    LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), static_cast<int>(uiEffect.value), inputRowCount);
  if (effect.repaint == 2) requestUpdate(effect.immediate);
  if (effect.action != XP_UI_UPDATE_NONE) performAction(static_cast<XpUiUpdateAction>(effect.action), effect.value);
  if (haptic) haptic_feedback::touchAction(false);
  if (effect.repaint == 1) requestUpdate(effect.immediate);
}
const char* UpdateUiActivity::text(uint32_t handle) const {
  if (handle > 0 && handle < 0x40000000u) {
    XpUiSettingsRow value{};
    if (!row((handle - 1) / 4, value)) return nullptr;
    switch ((handle - 1) % 4) {
      case 0:
        return value.label;
      case 1:
        return value.subtitle;
      case 2:
        return value.value;
      default:
        return nullptr;
    }
  }
  switch (handle) {
    case 0x40000001:
      return tr(STR_CHECKING_UPDATE);
    case 0x40000002:
      return tr(STR_UPDATING);
    case 0x40000003:
      return tr(STR_NO_UPDATE);
    case 0x40000004:
      return tr(STR_UPDATE_FAILED);
    case 0x40000005:
      return tr(STR_UPDATE_COMPLETE);
    case 0x40000006:
      return tr(STR_POWER_ON_HINT);
    case 0x40000007:
      return tr(STR_VALIDATING_FIRMWARE);
    case 0x40000008:
      return tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF);
    case 0x40000009:
      return tr(STR_RESTARTING_HINT);
    case 0x4000000a:
      return tr(STR_RECOVERY_MODE_HINT);
    case 0x4000000b:
      return tr(STR_LOADING_FONT_LIST);
    case 0x4000000c:
      return tr(STR_NO_FONTS_AVAILABLE);
    case 0x4000000d:
      return tr(STR_FONT_INSTALLED);
    case 0x4000000e:
      return tr(STR_FONT_INSTALL_FAILED);
    case 0x4000000f:
      return tr(STR_CLEARING_CACHE);
    case 0x40000010:
      return tr(STR_CACHE_CLEARED);
    case 0x40000011:
      return tr(STR_CLEAR_CACHE_FAILED);
    case 0x40000012:
      return tr(STR_CHECK_SERIAL_OUTPUT);
    case 0x40000019:
      return percentText;
    case 0x4000001a:
      return painting.kind == XP_UI_UPDATE_OTA ? tr(STR_NEW_UPDATE) : tr(STR_CLEAR_READING_CACHE);
    case 0x4000001b:
      return tr(STR_CLEAR_CACHE_WARNING_2);
    case 0x4000001c:
      return tr(STR_CANCEL);
    case 0x4000001d:
      return painting.kind == XP_UI_UPDATE_OTA ? tr(STR_UPDATE) : tr(STR_CLEAR_BUTTON);
    default:
      return dynamicText(handle);
  }
}
XpUiUpdateResources UpdateUiActivity::resources() {
  XpUiUpdateResources out{};
  out.rows = {
      this,
      [](void* p, uint16_t index, XpUiSettingsRow* out) -> uint8_t {
        return static_cast<UpdateUiActivity*>(p)->row(index, *out);
      },
      [](void* p, const char* value, int32_t font, uint8_t bold) -> int32_t {
        return static_cast<UpdateUiActivity*>(p)->renderer.getTextWidth(font, value,
                                                                        static_cast<EpdFontFamily::Style>(bold));
      },
      [](void* p, const char* value, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
        auto& self = *static_cast<UpdateUiActivity*>(p);
        self.textTarget.setFont(0, font);
        fui::TextStyle style;
        style.font = 0;
        style.bold = bold;
        style.maxLines = lines;
        return fui::measureWrappedText(self.textTarget, value, style, width).height;
      },
      [](void* p, int32_t font) -> int32_t { return static_cast<UpdateUiActivity*>(p)->renderer.getLineHeight(font); },
      static_cast<uint16_t>(paintedRowCount()),
      0};
  out.context = this;
  out.text = [](void* p, uint32_t handle) -> const char* { return static_cast<UpdateUiActivity*>(p)->text(handle); };
  out.text_height = [](void* p, uint32_t handle, int32_t font, int16_t width, uint8_t bold, uint8_t lines) -> int16_t {
    auto& self = *static_cast<UpdateUiActivity*>(p);
    self.textTarget.setFont(0, font);
    fui::TextStyle style;
    style.font = 0;
    style.bold = bold;
    style.maxLines = lines;
    return fui::measureWrappedText(self.textTarget, self.text(handle), style, width).height;
  };
  return out;
}
bool UpdateUiActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    painting = model;
  }
  prepareRows();
  prepareText();
  const auto percentage = xp_ui_updates_percent(&painting);
  snprintf(percentText, sizeof(percentText), "%d%%", static_cast<int32_t>(percentage));
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = textTarget.deviceContext();
  XpUiUpdateLayout layout{};
  auto& l = layout.list;
  auto& style = l.list;
  style.screen = {0, 0, int16_t(renderer.getScreenWidth()), int16_t(renderer.getScreenHeight())};
  style.area = {device.safeArea.left, device.safeArea.top,
                int16_t(device.width - device.safeArea.left - device.safeArea.right),
                int16_t(device.height - device.safeArea.top - device.safeArea.bottom)};
  style.font = fonts.bodyFontId;
  style.row_height = mappedInput.hasTouch() ? 0 : m.listRowHeight;
  style.row_gap = m.listRowGap;
  style.inset = m.listInset;
  style.padding = m.listSidePadding;
  style.scroll_width = m.listScrollWidth;
  style.radius = m.listRowRadius;
  style.selection = m.listSelectionStyle;
  style.scroll_left = m.listScrollSide;
  style.bold = m.listTitleBold;
  l.small_font = fonts.smallFontId;
  l.help_font = UI_10_FONT_ID;
  l.top_padding = m.topPadding;
  l.header_height = m.headerHeight;
  l.hints_height = m.buttonHintsHeight;
  l.spacing = m.verticalSpacing;
  l.side_padding = m.contentSidePadding;
  l.touch = mappedInput.hasTouch();
  layout.progress_height = m.progressBarHeight;
  layout.popup_padding = m.optionPopupInnerPadding;
  layout.popup_gap = m.optionPopupItemSpacing;
  layout.popup_side_margin = m.optionPopupDialogSideMargin;
  layout.selection_padding = m.optionPopupSelectionVPadding;
  layout.popup_radius = m.popupCornerRadius;
  layout.popup_border = m.popupFrameThickness;
  const auto services = resources();
  // The dense C3 font matrix uses at most 71 commands. Reserve on the first frame
  // so the list does not need overlapping old and new command arenas.
  const uint16_t initial = painting.kind == XP_UI_UPDATE_FONTS ? 71 : 20;
  if (!reserve(initial)) return false;
  for (;;) {
    const XpUiBuffer buffer{commands.get(), nullptr, capacity, 0};
    auto result = xp_ui_updates_view(&painting, &layout, &services, &buffer, &next, &scene);
    if (result == 1) return true;
    if (result != 2 || !reserve(capacity + 16)) return false;
  }
}
void UpdateUiActivity::rustSceneFailed() {
  xp_ui_updates_notify(&model, XP_UI_UPDATE_SCENE_FAILED, 0, 0);
  pending = {};
  pendingHaptic = false;
}
void UpdateUiActivity::rustPublished() {
  xp_ui_updates_publish(&model, &painting, &next);
  feedback = next;
}
void UpdateUiActivity::hints(uint8_t kind) const {
  const char* back = tr(STR_BACK);
  const char* confirm = "";
  const char* up = "";
  const char* down = "";
  switch (kind) {
    case 0:
      break;
    case 1:
      back = tr(STR_CANCEL);
      break;
    case 2:
      confirm = tr(STR_SELECT);
      up = tr(STR_DIR_UP);
      down = tr(STR_DIR_DOWN);
      break;
    case 3:
      confirm = tr(STR_OPEN);
      up = tr(STR_DIR_UP);
      down = tr(STR_DIR_DOWN);
      break;
    case 4:
      confirm = fontConfirmLabel();
      if (paintedRowCount()) {
        up = tr(STR_DIR_UP);
        down = tr(STR_DIR_DOWN);
      }
      break;
    case 5:
      confirm = tr(STR_RETRY);
      break;
  }
  const auto labels = mappedInput.mapLabels(back, confirm, up, down);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
XpUiPaint UpdateUiActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t h, uint32_t g) -> uint8_t {
            return g == 1 && (h == 0x80000001 || h == 0x80000002 || h == 0x80000003 ||
                              static_cast<UpdateUiActivity*>(p)->text(h) != nullptr);
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
            auto& self = *static_cast<UpdateUiActivity*>(p);
            const auto r = c->rect;
            if (c->resource == 0x80000001) {
              const char* title = "";
              switch (self.painting.kind) {
                case XP_UI_UPDATE_OTA:
                  title = tr(STR_UPDATE);
                  break;
                case XP_UI_UPDATE_SD:
                  title = self.painting.recovery ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE);
                  break;
                case XP_UI_UPDATE_FONTS:
                  title = tr(STR_FONT_BROWSER);
                  break;
                case XP_UI_UPDATE_CACHE:
                  title = tr(STR_CLEAR_READING_CACHE);
                  break;
              }
              GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, title, self.headerSubtitle());
              self.afterHeader();
            } else if (c->resource == 0x80000002)
              self.hints(c->font);
            else if (c->resource == 0x80000003)
              LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", unsigned(c->font), 100,
                      unsigned(c->font));
            else
              self.renderer.drawUiText(*c, clip, self.text(c->resource));
          }};
}

void UpdateUiActivity::cancelRejectedInput(const XpUiInput& input) {
  XpUiInput back{};
  back.pressed = input.pressed & XP_UI_BACK;
  back.released = input.released & XP_UI_BACK;
  back.now_ms = input.now_ms;
  {
    ModelLock lock(*this);
    updateRust(back);
  }
  applyRustEffect({});
}

bool UpdateUiActivity::rustSceneCurrent() const {
  return model.kind == painting.kind && model.phase == painting.phase && model.popup_active == painting.popup_active;
}
