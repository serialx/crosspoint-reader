#include "DictionaryUiBindings.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/Dictionary.h"

static_assert(static_cast<uint8_t>(ClippingResult::Action::Clip) == 0);
static_assert(static_cast<uint8_t>(ClippingResult::Action::Lookup) == 1);
static_assert(static_cast<uint8_t>(ClippingResult::Action::Bookmark) == 2);
static_assert(static_cast<uint8_t>(Dictionary::LookupResult::Found) == 0);
static_assert(static_cast<uint8_t>(Dictionary::LookupResult::NotFound) == 1);
static_assert(static_cast<uint8_t>(Dictionary::LookupResult::LowMemory) == 2);
static_assert(static_cast<uint8_t>(Dictionary::LookupResult::Decompress) == 3);
static_assert(static_cast<uint8_t>(Dictionary::LookupResult::ReadError) == 4);
static_assert(static_cast<uint8_t>(Dictionary::IndexResult::Ok) == 0);
static_assert(static_cast<uint8_t>(Dictionary::IndexResult::LowMemory) == 1);
static_assert(static_cast<uint8_t>(Dictionary::IndexResult::ReadError) == 2);

XpUiInput dictionaryUiInput(const MappedInputManager& mappedInput) {
  XpUiInput input{};
  constexpr MappedInputManager::Button buttons[] = {
      MappedInputManager::Button::Back,        MappedInputManager::Button::Confirm,
      MappedInputManager::Button::NavNext,     MappedInputManager::Button::NavPrevious,
      MappedInputManager::Button::Left,        MappedInputManager::Button::Right,
      MappedInputManager::Button::Up,          MappedInputManager::Button::Down,
      MappedInputManager::Button::Power,       MappedInputManager::Button::PageBack,
      MappedInputManager::Button::PageForward, MappedInputManager::Button::ScreenLeft,
      MappedInputManager::Button::ScreenRight, MappedInputManager::Button::ScreenUp,
      MappedInputManager::Button::ScreenDown};
  for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    if (mappedInput.wasPressed(buttons[i])) input.pressed |= 1u << i;
    if (mappedInput.wasReleased(buttons[i])) input.released |= 1u << i;
    if (mappedInput.isPressed(buttons[i])) input.held |= 1u << i;
  }
  input.now_ms = millis();
  input.held_ms = mappedInput.getHeldTime();
  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      input.swipe = XP_UI_SWIPE_UP;
      break;
    case MappedInputManager::SwipeDir::Down:
      input.swipe = XP_UI_SWIPE_DOWN;
      break;
    case MappedInputManager::SwipeDir::Left:
      input.swipe = XP_UI_SWIPE_LEFT;
      break;
    case MappedInputManager::SwipeDir::Right:
      input.swipe = XP_UI_SWIPE_RIGHT;
      break;
    case MappedInputManager::SwipeDir::None:
      break;
  }
  return input;
}

XpUiDefinitionLayout dictionaryUiDefinitionLayout(const GfxRenderer& renderer) {
  const auto& m = UITheme::getInstance().getMetrics();
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  return {{0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
          static_cast<int16_t>(top),
          static_cast<int16_t>(right),
          static_cast<int16_t>(bottom),
          static_cast<int16_t>(left),
          static_cast<int16_t>(m.topPadding),
          static_cast<int16_t>(m.headerHeight),
          static_cast<int16_t>(m.verticalSpacing),
          static_cast<int16_t>(m.contentSidePadding),
          static_cast<int16_t>(m.buttonHintsHeight),
          static_cast<int16_t>(m.sideButtonHintsWidth),
          static_cast<uint8_t>(renderer.getOrientation()),
          0,
          {0, 0}};
}

bool dictionaryUiPopup(const GfxRenderer& renderer, const char* message, XpUiCommand* commands, uint16_t capacity,
                       XpUiScene& scene) {
  const auto& m = UITheme::getInstance().getMetrics();
  const XpUiDictionaryPopup layout{
      {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
      UI_12_FONT_ID,
      static_cast<int16_t>(m.popupMarginX),
      static_cast<int16_t>(m.popupMarginY),
      static_cast<int16_t>(m.popupFrameThickness),
      static_cast<int16_t>(m.popupCornerRadius),
      static_cast<int16_t>(m.popupTextBaselineOffsetY),
      m.popupTopOffsetRatio,
      static_cast<uint8_t>(m.popupTextBold),
      static_cast<uint8_t>(m.popupTextInverted),
      {0, 0}};
  const XpUiMetrics metrics{
      const_cast<GfxRenderer*>(&renderer),
      [](void* p, int32_t font, const uint8_t* text, uint16_t length, uint8_t style) {
        const auto provider = static_cast<GfxRenderer*>(p)->textProvider();
        return xp_native_width(&provider, font, text, length, style, -1);
      },
      [](void* p, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(p)->getLineHeight(font); }};
  const XpUiBuffer buffer{commands, nullptr, capacity, 0};
  return xp_ui_dictionary_popup(&layout, message, &metrics, &buffer, &scene) == 1;
}
