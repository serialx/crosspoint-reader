#include "RustReaderDrawing.h"

#include <BoardConfig.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/readerToolbarIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;
namespace {
// Resource text is owned and NUL-terminated by Rust for the duration of a call.
const char* text(XpAppText value) { return value.len ? reinterpret_cast<const char*>(value.data) : ""; }
}  // namespace

bool sampleReaderUtilityLayout(GfxRenderer& renderer, const MappedInputManager& input, XpUiUtilityLayout& l) {
  const auto& m = UITheme::getInstance().getMetrics();
  auto target = makeUiTarget(renderer);
  const auto& t = refreshSharedUiThemeTokens(target);
  const auto device = target.deviceContext();
  const auto safe = device.safeRect();
  const auto scale = uiScaleSpec();
  l = {};
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
  l.touch = input.hasTouch();
  return gpio.hasEdgeSideButtons();
}

void sampleReaderChildTheme(const GfxRenderer& renderer, XpUiReaderTheme& theme, XpUiReaderThemeFacts& facts,
                            const bool toolbar) {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto fonts = uiScaleSpec();
  theme = {};
  theme.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                  static_cast<int16_t>(renderer.getScreenHeight())};
  theme.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                static_cast<int16_t>(safe.height)};
  theme.body_font = fonts.bodyFontId;
  theme.small_font = fonts.smallFontId;
  theme.title_font = fonts.titleFontId;
  theme.fixed_title_font = UI_12_FONT_ID;
  facts.help_font = UI_10_FONT_ID;
  facts.bookmark_help_font = SMALL_FONT_ID;
  theme.row_min = m.listRowHeight;
  theme.row_gap = m.listRowGap;
  theme.row_inset = m.listInset;
  theme.row_padding = m.listSidePadding;
  theme.scroll_width = m.listScrollWidth;
  theme.top_padding = m.topPadding;
  theme.header_height = m.headerHeight;
  theme.spacing = m.verticalSpacing;
  theme.tab_height = m.tabBarHeight;
  theme.header_padding = m.headerSidePadding;
  facts.body_line_height = renderer.getLineHeight(fonts.bodyFontId);
  theme.control_radius = m.controlRadius;
  theme.sheet_radius = m.sheetRadius;
  theme.row_radius = m.listRowRadius;
  theme.selection = m.listSelectionStyle;
  theme.scroll_left = m.listScrollSide;
  theme.title_bold = m.listTitleBold;
  theme.touch = BoardConfig::hasTouch();
  theme.orientation = renderer.getOrientation();
  if (toolbar) {
    auto target = makeUiTarget(renderer);
    const auto safe = target.deviceContext().safeRect();
    theme.safe = {safe.x, safe.y, safe.width, safe.height};
  }
}

void drawReaderHeader(GfxRenderer& renderer, const XpUiRect bounds, const XpAppText title, const bool list) {
  const Rect rect{bounds.x, bounds.y, bounds.w, bounds.h};
  GUI.drawHeader(renderer, list ? UITheme::getInstance().getHeaderRect(renderer, true, false) : rect, text(title));
}

void drawReaderUtilityFooter(GfxRenderer& renderer, const MappedInputManager& input, XpUiRect clip,
                             const XpAppText* labels) {
  const auto old = renderer.getClipRect();
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  // Utility hints draw in portrait coordinates over a rotated reader.
  switch (renderer.getOrientation()) {
    case GfxRenderer::Portrait:
      break;
    case GfxRenderer::LandscapeClockwise:
      clip = {clip.y, static_cast<int16_t>(w - clip.x - clip.w), clip.h, clip.w};
      break;
    case GfxRenderer::PortraitInverted:
      clip = {static_cast<int16_t>(w - clip.x - clip.w), static_cast<int16_t>(h - clip.y - clip.h), clip.w, clip.h};
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      clip = {static_cast<int16_t>(h - clip.y - clip.h), clip.x, clip.h, clip.w};
      break;
  }
  renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
  const auto mapped = input.mapLabels(text(labels[0]), text(labels[1]), text(labels[2]), text(labels[3]));
  GUI.drawButtonHints(renderer, mapped.btn1, mapped.btn2, mapped.btn3, mapped.btn4);
  renderer.setClipRect(old[0], old[1], old[2], old[3]);
}

void drawReaderToolbarIcon(GfxRenderer& renderer, const uint32_t icon, const XpUiRect bounds, const XpUiRect clip) {
  fui::BitmapRef bitmap;
  switch (icon) {
    case XP_UI_READER_TOOL_CONTENTS:
      bitmap = fui::bitmapFromIcon(icon_reader_contents_24);
      break;
    case XP_UI_READER_TOOL_TEXT:
      bitmap = fui::bitmapFromIcon(icon_reader_text_24);
      break;
    case XP_UI_READER_TOOL_MORE:
      bitmap = fui::bitmapFromIcon(icon_reader_more_24);
      break;
    case XP_UI_READER_TOOL_PREV:
      bitmap = fui::bitmapFromIcon(icon_reader_back_24);
      break;
    case XP_UI_READER_TOOL_NEXT:
      bitmap = fui::bitmapFromIcon(icon_reader_next_24);
      break;
    default:
      return;
  }
  const auto old = renderer.getClipRect();
  renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
  auto target = makeUiTarget(renderer);
  target.bitmap({bounds.x, bounds.y, bounds.w, bounds.h}, bitmap, fui::BitmapMode::Center);
  renderer.setClipRect(old[0], old[1], old[2], old[3]);
}

#ifdef XP_APP_TEST
void prewarmReaderRows(GfxRenderer& renderer, const int32_t font, const void* context,
                       XpAppText (*row)(const void*, uint32_t), const uint32_t count) {
  struct Rows {
    const void* context;
    XpAppText (*row)(const void*, uint32_t);
    uint32_t count;
  } rows{context, row, count};
  renderer.prewarmFallbackText(
      font,
      [](const void* opaque, const uint32_t index) -> const char* {
        const auto& rows = *static_cast<const Rows*>(opaque);
        return index < rows.count ? text(rows.row(rows.context, index)) : nullptr;
      },
      &rows, count);
}

#endif
