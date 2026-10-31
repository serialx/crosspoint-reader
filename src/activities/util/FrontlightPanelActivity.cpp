#include "FrontlightPanelActivity.h"

#include <HalFrontlight.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "components/UiAppHelpers.h"
#include "components/icons/readerToolbarIcons.h"
#include "util/SlideSwitch.h"
namespace fui = freeink::ui;
void FrontlightPanelActivity::onEnter() {
  state.brightness = Frontlight.brightness();
  state.warmth = Frontlight.warmth();
  state.light_on = Frontlight.isOn();
  state.inverted = SETTINGS.screenInverted;
  state.orientation = SETTINGS.orientation;
  state.touch_mode = SETTINGS.touchReaderControls;
  state.switch_owns_light = slideSwitchOwnsLight();
  UtilityActivity::onEnter();
}
void FrontlightPanelActivity::onExit() {
  const bool changed = SETTINGS.frontlightBrightness != state.brightness || SETTINGS.frontlightWarmth != state.warmth ||
                       (state.light_changed && SETTINGS.frontlightOn != state.light_on);
  if (changed) {
    SETTINGS.frontlightBrightness = state.brightness;
    SETTINGS.frontlightWarmth = state.warmth;
    if (state.light_changed) SETTINGS.frontlightOn = state.light_on;
    SETTINGS.saveToFile();
  }
  UtilityActivity::onExit();
}
void FrontlightPanelActivity::prepareUtilityView() {
  snprintf(brightnessText, sizeof(brightnessText), "%u%%", renderingState.brightness);
  snprintf(warmthText, sizeof(warmthText), "%u%%", renderingState.warmth);
}
const char* FrontlightPanelActivity::utilityText(const uint32_t h) const {
  switch (h) {
    case BrightValue:
      return brightnessText;
    case WarmValue:
      return warmthText;
    default:
      return UtilityActivity::utilityText(h);
  }
}
void FrontlightPanelActivity::utilityLayout(XpUiUtilityLayout& l) const {
  l.light_present = Frontlight.present();
  l.has_warmth = Frontlight.hasColorTemperature();
}
void FrontlightPanelActivity::applyUtilityEffect(const XpUiUtilityEffect& e) {
  if (e.writes & XP_UI_UTILITY_BRIGHTNESS) Frontlight.setBrightness(state.brightness);
  if (e.writes & XP_UI_UTILITY_WARMTH) Frontlight.setWarmth(state.warmth);
  if (e.writes & XP_UI_UTILITY_LIGHT) Frontlight.setOn(state.light_on);
  if (e.writes & XP_UI_UTILITY_SETTINGS) {
    SETTINGS.screenInverted = state.inverted;
    SETTINGS.orientation = state.orientation;
    SETTINGS.touchReaderControls = state.touch_mode;
    SETTINGS.saveToFile();
  }
  if (e.writes & XP_UI_UTILITY_CLEAN_PARENT) renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  UtilityActivity::applyUtilityEffect(e);
}

XpUiEffect FrontlightPanelActivity::updateRust(const XpUiInput& input) {
  state.inverted = SETTINGS.screenInverted;
  state.orientation = SETTINGS.orientation;
  state.touch_mode = SETTINGS.touchReaderControls;
  state.switch_owns_light = slideSwitchOwnsLight();
  return UtilityActivity::updateRust(input);
}

bool FrontlightPanelActivity::utilityResourceValid(const uint32_t h) const {
  return h == Battery || (h >= Sun && h <= TouchIcon) || UtilityActivity::utilityResourceValid(h);
}
void FrontlightPanelActivity::utilityResource(const XpUiCommand& c) {
  const Rect r{c.rect.x, c.rect.y, c.rect.w, c.rect.h};
  switch (c.resource) {
    case Battery:
      UITheme::getInstance().getTheme().BaseTheme::drawHeader(renderer, r, nullptr);
      break;
    case Sun:
    case SunFilled:
    case Moon:
    case RefreshIcon:
    case OrientationIcon:
    case TouchIcon: {
      auto target = makeUiTarget(renderer);
      fui::BitmapRef bitmap;
      switch (c.resource) {
        case Sun:
          bitmap = fui::bitmapFromIcon(icon_sun_32);
          break;
        case SunFilled:
          bitmap = fui::bitmapFromIcon(icon_sun_filled_32);
          break;
        case Moon:
          bitmap = fui::bitmapFromIcon(icon_moon_32);
          break;
        case RefreshIcon:
          bitmap = fui::bitmapFromIcon(icon_refresh_cw_32);
          break;
        case OrientationIcon:
          bitmap = fui::bitmapFromIcon(icon_reader_text_24);
          break;
        case TouchIcon:
          bitmap = fui::bitmapFromIcon(icon_pointer_32);
          break;
      }
      target.bitmap({c.rect.x, c.rect.y, c.rect.w, c.rect.h}, bitmap, fui::BitmapMode::Center,
                    fui::Paint::solid(c.paint ? fui::Color::White : fui::Color::Black),
                    static_cast<fui::Rotation>(c.style));
      break;
    }
    default:
      UtilityActivity::utilityResource(c);
      break;
  }
}
