#include "CatalogScreens.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <numeric>

#include "HeaderBackTapTarget.h"
#include "UITheme.h"
#include "icons/headerIcons.h"

namespace fui = freeink::ui;

void catalogScreenHeader(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const char* title,
                         const fui::BitmapRef& trailingIcon, const fui::ActionId trailingAction) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgeBottom;
  // Same battery/clock band as every GUI.drawHeader screen; the header
  // heights are unified across themes, so the buttons derive from the band.
  GUI.applyHeaderStatus(renderer, header);
  const auto frameRect = screen.frame().screen();
  const fui::Rect band = BaseTheme::headerBand(renderer,
                                               fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding),
                                                         frameRect.width, static_cast<int16_t>(metrics.headerHeight)},
                                               header);
  // Back button on touch boards, as on every GUI.drawHeader screen: the rect
  // goes to HeaderBackTapTarget, which MappedInputManager folds into
  // Button::Back, so each page's own Back handling applies (leaving a list,
  // cancelling a download). The action id only makes the header paint the
  // button; no screen registers it.
  if (gpio.hasTouch()) {
    static constexpr fui::ActionId PAINT_ONLY_BACK = 0xFFFF;
    header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    header.leadingAction = PAINT_ONLY_BACK;
    // Every state identical, like the trailing icon: no filled active box,
    // and a tap flash (never cleared, since no handler owns this action)
    // renders the same as the resting state instead of sticking.
    header.leadingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
    HeaderBackTapTarget::set(band.x + 4, band.y + 4 + header.actionOffsetY, header.leadingSize, header.leadingSize);
  }
  if (trailingIcon && trailingAction != fui::NO_ACTION) {
    header.trailingIcon = trailingIcon;
    header.trailingAction = trailingAction;
  }
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  fui::header(screen.frame(), band, header);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
}
