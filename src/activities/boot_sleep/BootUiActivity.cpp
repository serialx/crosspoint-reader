#include "BootUiActivity.h"

#include <I18n.h>

#include <cstddef>

#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"

static_assert(XP_UI_BOOT_XTC_PIXELS == 12 && XP_UI_BOOT_XTC_PAGE == 8);
static_assert(sizeof(XpUiXtcDiagnostics) == 2 * sizeof(void*) && alignof(XpUiXtcDiagnostics) == alignof(void*) &&
              offsetof(XpUiXtcDiagnostics, outside) == sizeof(void*));
static_assert(sizeof(XpUiSleepPlan) == 8 && offsetof(XpUiSleepPlan, overlay_cover) == 7);
static_assert(sizeof(XpUiImageState) == 12 && offsetof(XpUiImageState, can_set_cover) == 8);
static_assert(sizeof(XpUiXtcState) == 4);
static_assert(sizeof(XpUiImagePlacement) == 16 && offsetof(XpUiImagePlacement, crop_x) == 8);
static_assert(alignof(XpUiImagePlacement) == 4 && offsetof(XpUiImagePlacement, crop_x) == 8 &&
              offsetof(XpUiImagePlacement, crop_y) == 12);
static_assert(sizeof(XpUiBootLayout) == 36 && offsetof(XpUiBootLayout, view) == 30);
static_assert(MOONICON_WIDTH == 48 && MOONICON_HEIGHT == 48);

const char* BootUiActivity::resourceText(const uint32_t handle) const {
  if (!activeView) return nullptr;
  switch (handle) {
    case XP_UI_BOOT_VERSION:
      return CROSSPOINT_VERSION;
    case XP_UI_BOOT_TITLE:
      return tr(STR_CROSSPOINT);
    case XP_UI_BOOT_SUBTITLE:
      return activeView->subtitle;
    case XP_UI_BOOT_REASON:
      return tr(STR_CRASH_REASON);
    case XP_UI_BOOT_ERROR:
      return activeView->error;
  }
  if (handle >= XP_UI_BOOT_DESCRIPTION && handle < XP_UI_BOOT_DESCRIPTION + 10 && activeView->description &&
      handle - XP_UI_BOOT_DESCRIPTION < activeView->description->size())
    return (*activeView->description)[handle - XP_UI_BOOT_DESCRIPTION].c_str();
  if (handle >= XP_UI_BOOT_PANIC && handle < XP_UI_BOOT_PANIC + 5 && activeView->panic &&
      handle - XP_UI_BOOT_PANIC < activeView->panic->size())
    return (*activeView->panic)[handle - XP_UI_BOOT_PANIC].c_str();
  return nullptr;
}
XpUiPaint BootUiActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
            auto& self = *static_cast<BootUiActivity*>(p);
            return generation == 1 &&
                   ((handle >= XP_UI_BOOT_LOGO && handle <= XP_UI_BOOT_HINTS) || handle == XP_UI_BOOT_IMAGE_BUTTONS ||
                    handle == XP_UI_BOOT_FIRST_HINT || self.resourceText(handle));
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<BootUiActivity*>(p);
            const auto& c = *command;
            if (c.kind == XP_UI_TEXT_HANDLE) {
              self.renderer.drawUiText(c, clip, self.resourceText(c.resource));
              return;
            }
            const auto previous = self.renderer.getClipRect();
            self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
            switch (c.resource) {
              case XP_UI_BOOT_LOGO:
                self.renderer.drawImage(Logo120, c.rect.x, c.rect.y, c.rect.w, c.rect.h);
                break;
              case XP_UI_BOOT_MOON_ICON:
                self.renderer.drawImage(MoonIcon, c.rect.x, c.rect.y, c.rect.w, c.rect.h);
                break;
              case XP_UI_BOOT_HEADER:
                GUI.drawHeader(self.renderer, Rect{c.rect.x, c.rect.y, c.rect.w, c.rect.h}, tr(STR_CRASH_TITLE),
                               nullptr, false);
                break;
              case XP_UI_BOOT_HINTS:
                self.BootUiActivity::drawBootHints();
                break;
              case XP_UI_BOOT_IMAGE_BUTTONS:
                self.drawBootHints();
                break;
              case XP_UI_BOOT_FIRST_HINT: {
                const auto labels = self.mappedInput.mapLabels(tr(STR_BACK), "", "", "");
                GUI.drawButtonHints(self.renderer, labels.btn1, "", "", "");
                break;
              }
            }
            self.renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
          }};
}
template <uint16_t Capacity>
__attribute__((noinline)) bool BootUiActivity::paintBootViewWithCapacity(const XpUiBootView view,
                                                                         const XpUiRefresh refresh,
                                                                         const ViewData& data, const uint16_t flags) {
  // Keep the crash arena off the stack of entry-only image and sleep views.
  XpUiCommand commands[Capacity]{};
  const XpUiBuffer buffer{commands, nullptr, Capacity, 0};
  return paintBootViewInto(view, refresh, data, flags, buffer);
}

bool BootUiActivity::paintBootView(const XpUiBootView view, const XpUiRefresh refresh, const ViewData& data,
                                   const uint16_t flags) {
  switch (view) {
    case XP_UI_BOOT_SPLASH:
      return paintBootViewWithCapacity<5>(view, refresh, data, flags);
    case XP_UI_BOOT_SLEEP:
      return paintBootViewWithCapacity<4>(view, refresh, data, flags);
    case XP_UI_BOOT_BLANK:
    case XP_UI_BOOT_MOON:
    case XP_UI_BOOT_IMAGE_HINTS:
    case XP_UI_BOOT_XTC_PAGE:
      return paintBootViewWithCapacity<1>(view, refresh, data, flags);
    case XP_UI_BOOT_CRASH:
      // Clear/header + 10 description lines + reason + 5 panic lines + hints.
      return paintBootViewWithCapacity<19>(view, refresh, data, flags);
    case XP_UI_BOOT_IMAGE_ERROR:
      return paintBootViewWithCapacity<3>(view, refresh, data, flags);
    case XP_UI_BOOT_PAGE_ERROR:
      return paintBootViewWithCapacity<2>(view, refresh, data, flags);
  }
  return paintBootViewWithCapacity<1>(view, refresh, data, flags);
}

bool BootUiActivity::paintBootViewInto(const XpUiBootView view, const XpUiRefresh refresh, const ViewData& data,
                                       const uint16_t flags, const XpUiBuffer& buffer) {
  const auto& m = UITheme::getInstance().getMetrics();
  XpUiBootLayout layout{};
  layout.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                   static_cast<int16_t>(renderer.getScreenHeight())};
  layout.title_font = UI_10_FONT_ID;
  layout.small_font = SMALL_FONT_ID;
  layout.error_font = UI_12_FONT_ID;
  layout.side_padding = m.contentSidePadding;
  layout.top_padding = m.topPadding;
  layout.header_height = m.headerHeight;
  layout.spacing = m.verticalSpacing;
  layout.line_height = renderer.getLineHeight(UI_10_FONT_ID);
  layout.flags = flags;
  layout.view = view;
  layout.refresh = refresh;
  layout.description_lines = data.description ? data.description->size() : 0;
  layout.panic_lines = data.panic ? data.panic->size() : 0;
  XpUiScene scene{};
  activeView = &data;
  const XpUiBootMetrics metrics{
      this, [](void* p, uint32_t h, int32_t f, uint8_t s) -> int32_t {
        auto& self = *static_cast<BootUiActivity*>(p);
        const char* text = self.resourceText(h);
        return text ? self.renderer.getTextWidth(f, text, static_cast<EpdFontFamily::Style>(s)) : -1;
      }};
  const bool result = xp_ui_boot_view(&layout, &metrics, &buffer, &scene) && paintRustScene(scene);
  activeView = nullptr;
  if (!result) LOG_ERR("RUI", "Cannot build boot/image scene");
  return result;
}

void BootUiActivity::drawBootHints() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
