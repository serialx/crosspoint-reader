#include "UtilityActivity.h"

#include <I18n.h>

#include <cstddef>

#include "components/UiAppHelpers.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
namespace fui = freeink::ui;
static_assert(sizeof(XpUiUtilityState) == 56 && alignof(XpUiUtilityState) == 4);
static_assert(offsetof(XpUiUtilityState, kind) == 36 && offsetof(XpUiUtilityState, last_drag_x) == 52);
static_assert(sizeof(XpUiUtilityEffect) == 12 && alignof(XpUiUtilityEffect) == 4 &&
              offsetof(XpUiUtilityEffect, writes) == 8);
static_assert(sizeof(XpUiUtilityLayout) == 80 && alignof(XpUiUtilityLayout) == 4 &&
              offsetof(XpUiUtilityLayout, popup_radius) == 62);
static_assert(sizeof(XpUiUtilityTexts) == 3 * sizeof(void*) && alignof(XpUiUtilityTexts) == alignof(void*) &&
              offsetof(XpUiUtilityTexts, text_height) == 2 * sizeof(void*));
static_assert(sizeof(XpUiUtilityFeedback) == 24 && alignof(XpUiUtilityFeedback) == 4 &&
              offsetof(XpUiUtilityFeedback, clean_revision) == 20);

UtilityActivity::UtilityActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                                 const XpUiUtilityKind kind, const uint16_t commands, const uint16_t hits)
    : RustActivityAdapter(name, renderer, input), commandCapacity(commands), hitCapacity(hits) {
  state.kind = kind;
}
void UtilityActivity::onEnter() {
  Activity::onEnter();
  publication.ready = false;
  const size_t bytes = commandCapacity * sizeof(XpUiCommand) + 2 * hitCapacity * sizeof(XpUiInteraction);
  arena = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!arena || !xp_ui_utility_init(&state)) {
    LOG_ERR("RUI", "Cannot allocate utility scene: %u bytes", static_cast<unsigned>(bytes));
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }
  requestUpdate(state.kind == XP_UI_UTILITY_CONFIRMATION);
}
void UtilityActivity::onExit() {
  publication.ready = false;
  arena.reset();
  Activity::onExit();
}
const char* UtilityActivity::utilityText(const uint32_t handle) const {
  switch (handle) {
    case Cancel:
      return tr(STR_CANCEL);
    case Confirm:
      return tr(STR_CONFIRM);
    case BrightLabel:
      return tr(STR_BRIGHTNESS);
    case WarmLabel:
      return tr(STR_WARMTH);
    case On:
      return tr(STR_STATE_ON);
    case Off:
      return tr(STR_STATE_OFF);
    case MoreLabel:
      return tr(STR_DETAILS);
    case Minus:
      return "-";
    case Plus:
      return "+";
    default:
      return handle >= Title && handle <= Plus ? "" : nullptr;
  }
}
bool UtilityActivity::utilityResourceValid(const uint32_t handle) const {
  return handle >= Title && handle <= Plus ? utilityText(handle) != nullptr : handle == Header || handle == Footer;
}
void UtilityActivity::utilityResource(const XpUiCommand& c) {
  const Rect r{c.rect.x, c.rect.y, c.rect.w, c.rect.h};
  if (c.kind == XP_UI_TEXT_HANDLE) {
    renderer.drawUiText(
        c, {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
        utilityText(c.resource));
    return;
  }
  switch (c.resource) {
    case Header:
      GUI.drawHeader(renderer, r, utilityText(Title), nullptr);
      break;
    case Footer:
      utilityHints();
      break;
  }
}
XpUiPaint UtilityActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t h, uint32_t g) -> uint8_t {
            return g == 1 && static_cast<UtilityActivity*>(p)->utilityResourceValid(h);
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
            auto& self = *static_cast<UtilityActivity*>(p);
            const auto old = self.renderer.getClipRect();
            // Button hints draw in portrait coordinates even over a rotated reader.
            if (c->resource == Footer) {
              const int w = self.renderer.getScreenWidth(), h = self.renderer.getScreenHeight();
              switch (self.renderer.getOrientation()) {
                case GfxRenderer::Portrait:
                  break;
                case GfxRenderer::LandscapeClockwise:
                  clip = {clip.y, static_cast<int16_t>(w - clip.x - clip.w), clip.h, clip.w};
                  break;
                case GfxRenderer::PortraitInverted:
                  clip = {static_cast<int16_t>(w - clip.x - clip.w), static_cast<int16_t>(h - clip.y - clip.h), clip.w,
                          clip.h};
                  break;
                case GfxRenderer::LandscapeCounterClockwise:
                  clip = {static_cast<int16_t>(h - clip.y - clip.h), clip.x, clip.h, clip.w};
                  break;
              }
            }
            self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
            self.utilityResource(*c);
            self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
          }};
}
bool UtilityActivity::buildRustScene(XpUiScene& scene) {
  if (!arena) return false;
  {
    ModelLock lock(*this);
    renderingState = state;
    buildingFeedback = feedback;
  }
  prepareUtilityView();
  const auto& m = UITheme::getInstance().getMetrics();
  auto target = makeUiTarget(renderer);
  const auto& t = refreshSharedUiThemeTokens(target);
  const auto device = target.deviceContext();
  const auto safe = device.safeRect();
  const auto scale = uiScaleSpec();
  XpUiUtilityLayout l{};
  l.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())};
  l.safe = {safe.x, safe.y, safe.width, safe.height};
  l.body_font = scale.bodyFontId;
  l.small_font = scale.smallFontId;
  l.title_font = scale.titleFontId;
  l.readout_font = NOTOSANS_18_FONT_ID;
  l.top_padding = m.topPadding;
  l.header_height = m.headerHeight;
  l.spacing = m.verticalSpacing;
  l.hints_height = m.buttonHintsHeight;
  l.home_top_padding = m.homeTopPadding;
  l.battery_height = m.batteryHeight;
  l.popup_padding = m.optionPopupInnerPadding;
  l.popup_gap = m.optionPopupItemSpacing;
  l.popup_side_margin = m.optionPopupDialogSideMargin;
  l.selection_padding = m.optionPopupSelectionVPadding;
  l.space_sm = t.spaceSm;
  l.space_md = t.spaceMd;
  l.space_lg = t.spaceLg;
  l.row_height = t.rowHeight;
  l.min_touch = t.minTouchSize;
  l.popup_radius = m.popupCornerRadius;
  l.popup_border = m.popupFrameThickness;
  l.row_radius = m.listRowRadius;
  l.selection = m.listSelectionStyle;
  l.control_radius = m.controlRadius;
  l.capsule_radius = m.capsuleRadius;
  l.sheet_radius = m.sheetRadius;
  l.body_bold = m.listTitleBold;
  l.touch = mappedInput.hasTouch();
  utilityLayout(l);
  const auto metrics = rustMetrics();
  const XpUiUtilityTexts texts{
      this, [](void* p, uint32_t h) { return static_cast<UtilityActivity*>(p)->utilityText(h); },
      [](void* p, uint32_t h, int32_t font, int16_t width, uint8_t bold, uint8_t lines) -> int16_t {
        auto& self = *static_cast<UtilityActivity*>(p);
        const char* value = self.utilityText(h);
        if (!value) return -1;
        fui::GfxRendererTarget textTarget(self.renderer);
        textTarget.setFont(0, font);
        fui::TextStyle style;
        style.bold = bold;
        style.maxLines = lines;
        return fui::measureWrappedText(textTarget, value, style, width).height;
      }};
  auto* commands = reinterpret_cast<XpUiCommand*>(arena.get());
  auto* hits =
      reinterpret_cast<XpUiInteraction*>(arena.get() + commandCapacity * sizeof(XpUiCommand)) + buildHits * hitCapacity;
  const XpUiBuffer buffer{commands, hits, commandCapacity, hitCapacity};
  return utilityView()(&renderingState, &l, &metrics, &texts, &buffer, &buildingFeedback, &scene) != 0;
}
void UtilityActivity::rustPublished() {
  xp_ui_utility_publish(&state, &renderingState);
  feedback = buildingFeedback;
  buildHits ^= 1;
}
XpUiEffect UtilityActivity::updateRust(const XpUiInput& input) {
  pendingEffect = xp_ui_utility_update(&state, &input, publication.ready ? &publication.scene : nullptr, &feedback);
  return pendingEffect.navigation;
}
void UtilityActivity::applyUtilityEffect(const XpUiUtilityEffect& effect) {
  if (effect.navigation.reserved[0] && effect.navigation.kind != XP_UI_ACTIVATE) haptic_feedback::touchAction();
  switch (static_cast<XpUiEffectKind>(effect.navigation.kind)) {
    case XP_UI_CANCEL: {
      if (state.kind == XP_UI_UTILITY_QR || state.kind == XP_UI_UTILITY_FRONTLIGHT) {
        finish();
        break;
      }
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_ACTIVATE:
      setResult(ActivityResult{});
      finish();
      break;
    case XP_UI_EFFECT_NONE:
    case XP_UI_SCROLL:
      break;
  }
}
