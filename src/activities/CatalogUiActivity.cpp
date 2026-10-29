#include "CatalogUiActivity.h"

#include <Memory.h>

#include "components/CatalogScreens.h"
#include "components/UiAppHelpers.h"
#include "components/icons/headerIcons.h"
#include "components/icons/opdsIcons.h"
#include "components/icons/search32.h"
static_assert(sizeof(XpUiCatalogState) == 84);
static_assert(offsetof(XpUiCatalogState, mode) == 61);
static_assert(sizeof(XpUiCatalogHits) == 288);
static_assert(offsetof(XpUiCatalogHits, controls) == 140);
static_assert(sizeof(XpUiCatalogLayout) == 104);
static_assert(offsetof(XpUiCatalogLayout, search_button) == 48);
static_assert(sizeof(XpUiCatalogRow) == 4 * sizeof(void*));
static_assert(offsetof(XpUiCatalogResources, count) == 6 * sizeof(void*));
namespace fui = freeink::ui;
CatalogUiActivity::CatalogUiActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                                     XpUiCatalogKind kind, int index, bool picker)
    : RustActivityAdapter(name, renderer, input), target(makeUiTarget(renderer)) {
  xp_ui_catalog_init(&ui, kind, index, picker);
}
CatalogUiActivity::~CatalogUiActivity() { vSemaphoreDelete(resourceMutex); }
bool CatalogUiActivity::reserveCommands(uint16_t count) {
  if (commands && count <= capacity) return true;
  if (count > 128) return false;
  commands.reset();
  capacity = count;
  commands = makeUniqueNoThrow<XpUiCommand[]>(count);
  if (!commands) {
    LOG_ERR("RUI", "Cannot allocate %u command bytes", unsigned(count * sizeof(XpUiCommand)));
    return false;
  }
  capacity = count;
  return true;
}
void CatalogUiActivity::onEnter() {
  Activity::onEnter();
  publication.ready = false;
  resetSelection();
  if (!reserveCommands(ui.kind == XP_UI_CATALOG_HUB ? 16 : ui.kind >= XP_UI_CATALOG_BROWSER ? 48 : 32)) {
    onBackButton();
    return;
  }
  requestUpdate();
}
void CatalogUiActivity::onExit() {
  publication.ready = false;
  commands.reset();
  capacity = 0;
  Activity::onExit();
}
void CatalogUiActivity::loop() {
  if (handleCustomInput()) return;
  if (consumeRejectedInput()) return;
  int x = 0, y = 0;
  bool listOwnsTouch;
  {
    ModelLock lock(*this);
    listOwnsTouch = ui.kind == XP_UI_CATALOG_PLUGINS && !ui.popup &&
                    (ui.mode == XP_UI_CATALOG_PLUGIN_PICKER || ui.mode == XP_UI_CATALOG_LIST_PICKER ||
                     ui.mode == XP_UI_CATALOG_BROWSING);
  }
  if (listOwnsTouch && mappedInput.wasScreenLongPress(x, y)) {
    XpUiInput input{};
    input.touch_flags = XP_UI_TOUCH_RELEASED;
    input.touch_x = x;
    input.touch_y = y;
    input.action = XP_UI_CATALOG_UNINSTALL;
    XpUiEffect effect;
    {
      ModelLock lock(*this);
      effect = updateRust(input);
    }
    if (effect.invalidate) requestUpdate();
    applyRustEffect(effect);
    return;
  }
  RustActivityAdapter::loop();
}
void CatalogUiActivity::render(RenderLock&&) {
  XpUiScene scene{};
  {
    ResourceLock resources(*this);
    if (!buildRustScene(scene)) {
      rejectRustScene();
      LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
      return;
    }
    const auto paint = rustResources();
    if (!renderer.drawUiScene(scene, &paint)) {
      rejectRustScene();
      LOG_ERR("RUI", "Invalid scene in %s", name.c_str());
      return;
    }
    publishRustScene(scene);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
int CatalogUiActivity::selectedIndex() const {
  ModelLock lock(*const_cast<CatalogUiActivity*>(this));
  return ui.list.selected;
}
void CatalogUiActivity::selectIndex(int i) {
  ModelLock lock(*this);
  xp_ui_catalog_select(&ui, i, 0);
}
void CatalogUiActivity::resetSelection() {
  ModelLock lock(*this);
  xp_ui_catalog_select(&ui, 0, 1);
}
void CatalogUiActivity::closeRouting() {
  ModelLock lock(*this);
  publication.ready = false;
}
XpUiInterest CatalogUiActivity::rustInterest(uint32_t now) const { return xp_ui_catalog_interest(&ui, now); }
XpUiEffect CatalogUiActivity::updateRust(const XpUiInput& source) {
  touchActivated = false;
  auto input = source;
  if (publication.ready && input.touch_flags) {
    XpUiInteraction hit{};
    input.action = 0;
    if (xp_ui_catalog_hit(&publishedHits, input.touch_x, input.touch_y, &hit)) {
      if (source.action == XP_UI_CATALOG_UNINSTALL) {
        if (hit.action == 1 && !ui.popup) input.action = XP_UI_CATALOG_UNINSTALL;
      } else {
        input.action = hit.action;
      }
      input.value = hit.value;
    }
    if (source.action == XP_UI_CATALOG_UNINSTALL && !input.action) input.touch_flags = 0;
  } else if (ui.popup || source.action == XP_UI_CATALOG_UNINSTALL) {
    input.touch_flags = 0;
  }
  const bool popupWasOpen = ui.popup != 0;
  const auto effect = xp_ui_catalog_update(&ui, &input, listCount(), hasSearch(), pageLinks());
  touchActivated = input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) &&
                   (effect.kind == XP_UI_ACTIVATE || effect.kind == XP_UI_CATALOG_SEARCH ||
                    effect.kind == XP_UI_CATALOG_PAGE || effect.kind == XP_UI_CATALOG_CANCEL_TRANSFER ||
                    effect.kind == XP_UI_CATALOG_UNINSTALL || effect.kind == XP_UI_CATALOG_HUB_OPTION);
  if (popupWasOpen && !ui.popup && effect.invalidate && (input.touch_flags & XP_UI_TOUCH_RELEASED)) {
    touchActivated = true;
  }
  return effect;
}
void CatalogUiActivity::applyRustEffect(const XpUiEffect& e) {
  if (touchActivated)
    haptic_feedback::touchAction(e.kind == XP_UI_CATALOG_UNINSTALL);
  else if (e.kind == XP_UI_CATALOG_UNINSTALL)
    mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 1000);
  switch (e.kind) {
    case XP_UI_CANCEL:
      onBackButton();
      break;
    case XP_UI_ACTIVATE:
      activateIndex(e.value);
      break;
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), int(e.value), listCount());
      break;
    default:
      catalogEffect(e);
      break;
  }
}
void CatalogUiActivity::drawFooter() {
  auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
const char* CatalogUiActivity::catalogText(uint32_t h) const {
  switch (h) {
    case 0x80000004:
      return ui.kind == XP_UI_CATALOG_SERVERS ? tr(STR_NO_SERVERS) : tr(STR_NO_ENTRIES);
    case 0x80000005:
      return tr(STR_ERROR_MSG);
    case 0x80000007:
      return tr(STR_TAP_TO_RETRY);
    case 0x80000008:
      return tr(STR_DOWNLOADING);
    case 0x8000000a:
      return tr(STR_CANCEL);
    case 0x8000000b:
      return tr(STR_CALIBRE_URL_HINT);
    default:
      return nullptr;
  }
}
const char* CatalogUiActivity::text(uint32_t h) const {
  if (h >= 0x80000000) return catalogText(h);
  XpUiCatalogRow row{};
  if (h == 0 || !rowAt((h - 1) / 4, row)) return nullptr;
  switch ((h - 1) % 4) {
    case 0:
      return row.label;
    case 1:
      return row.subtitle;
    case 2:
      return row.value;
    default:
      return row.heading;
  }
}
bool CatalogUiActivity::buildRustScene(XpUiScene& scene) { return buildCatalogScene(scene); }
void CatalogUiActivity::rustSceneFailed() {
  touchActivated = false;
  ui.list.active = -1;
  ui.list.pending_scroll = 0;
  ui.list.last_repeat = 0;
  ui.search_pending = 0;
  ui.long_press = 0;
}
bool CatalogUiActivity::buildCatalogScene(XpUiScene& scene) {
  if (!commands && !reserveCommands(capacity)) return false;
  if (listCount() < 0 || listCount() > UINT16_MAX) return false;
  XpUiCatalogState snapshot{};
  {
    ModelLock lock(*this);
    snapshot = ui;
    paintState = snapshot;
    consumedScroll = ui.list.pending_scroll;
  }
  prepareRows();
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = target.deviceContext();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  layout = {};
  auto& style = layout.list;
  style.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                  static_cast<int16_t>(renderer.getScreenHeight())};
  style.area = {device.safeArea.left, device.safeArea.top,
                static_cast<int16_t>(device.width - device.safeArea.left - device.safeArea.right),
                static_cast<int16_t>(device.height - device.safeArea.top - device.safeArea.bottom)};
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
  layout.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                 static_cast<int16_t>(safe.height)};
  layout.small_font = fonts.smallFontId;
  layout.title_font = fonts.titleFontId;
  layout.top_padding = m.topPadding;
  layout.header_height = m.headerHeight;
  layout.hints_height = m.buttonHintsHeight;
  layout.spacing = m.verticalSpacing;
  layout.tab_height = m.tabBarHeight;
  layout.side_padding = ui.kind == XP_UI_CATALOG_EDITOR ? m.headerSidePadding : m.contentSidePadding;
  layout.touch = mappedInput.hasTouch();
  const auto& tokens = refreshSharedUiThemeTokens(target);
  layout.space_md = tokens.spaceMd;
  layout.button_height = tokens.rowHeight;
  layout.control_radius = tokens.controlRadius;
  layout.popup_padding = m.optionPopupInnerPadding;
  layout.popup_gap = m.optionPopupItemSpacing;
  layout.popup_side_margin = m.optionPopupDialogSideMargin;
  layout.selection_padding = m.optionPopupSelectionVPadding;
  layout.popup_radius = m.popupCornerRadius;
  layout.popup_border = m.popupFrameThickness;
  layout.search_button = searchButtonRect(tokens.minTouchSize);
  layout.search = hasSearch();
  layout.pages = pageLinks();
  pageNumbers(layout.page_current, layout.page_total);
  const XpUiCatalogResources resources{
      this,
      [](void* p, uint16_t i, XpUiCatalogRow* out) -> uint8_t {
        return static_cast<CatalogUiActivity*>(p)->rowAt(i, *out);
      },
      [](void* p, uint32_t h) { return static_cast<CatalogUiActivity*>(p)->text(h); },
      [](void* p, const char* s, int32_t f, uint8_t b) -> int32_t {
        return static_cast<CatalogUiActivity*>(p)->renderer.getTextWidth(f, s, static_cast<EpdFontFamily::Style>(b));
      },
      [](void* p, const char* s, int32_t f, uint8_t b, uint8_t lines, int16_t w) -> int32_t {
        auto& self = *static_cast<CatalogUiActivity*>(p);
        self.target.setFont(0, f);
        fui::TextStyle style;
        style.font = 0;
        style.bold = b;
        style.maxLines = lines;
        return fui::measureWrappedText(self.target, s, style, w).height;
      },
      [](void* p, int32_t f) -> int32_t { return static_cast<CatalogUiActivity*>(p)->renderer.getLineHeight(f); },
      static_cast<uint16_t>(listCount()),
      0};
  for (;;) {
    const XpUiBuffer buffer{commands.get(), nullptr, capacity, 0};
    const auto result = xp_ui_catalog_view(&snapshot, &layout, &resources, &buffer, &next, &scene, &nextHits);
    if (result == 1) return true;
    if (result != 2 || !reserveCommands(capacity + 16)) return false;
  }
}
void CatalogUiActivity::rustPublished() {
  xp_ui_catalog_publish(&ui, &next, consumedScroll);
  publishedHits = nextHits;
}
XpUiPaint CatalogUiActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t h, uint32_t g) -> uint8_t {
            if (g != 1) return false;
            return h == 0x80000001 || h == 0x80000002 || (h >= 0x80000010 && h <= 0x80000013) || h == 0x80000017 ||
                   static_cast<CatalogUiActivity*>(p)->text(h) != nullptr;
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
            auto& self = *static_cast<CatalogUiActivity*>(p);
            if (c->resource == 0x80000001) {
              self.paintHeader(c->rect);
              return;
            }
            if (c->resource == 0x80000002) {
              self.drawFooter();
              return;
            }
            if (c->resource >= 0x80000010 && c->resource <= 0x80000013) {
              static constexpr const freeink::Icon* icons[] = {&icon_page_first_32, &icon_page_prev_32,
                                                               &icon_page_next_32, &icon_page_last_32};
              auto r = c->rect;
              self.target.bitmap({r.x, r.y, r.w, r.h}, fui::bitmapFromIcon(*icons[c->resource - 0x80000010]),
                                 fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
              return;
            }
            if (c->resource == 0x80000017) {
              self.paintCatalogResource(*c);
              return;
            }
            self.renderer.drawUiText(*c, clip, self.text(c->resource));
          }};
}
void CatalogUiActivity::paintHeader(XpUiRect r) {
  if (ui.kind < XP_UI_CATALOG_BROWSER) {
    GUI.drawHeader(renderer, Rect{r.x, r.y, r.w, r.h}, headerTitle());
    return;
  }
  paintBrowserHeader();
}
void CatalogUiActivity::paintBrowserHeader() {
  // Reuse the shared chrome instantiation while Rust owns content and routing.
  const auto fonts = uiScaleSpec();
  fui::GfxRendererFrame<24> frame(renderer, fonts.smallFontId, fonts.bodyFontId, fonts.titleFontId);
  frame.target.setFont(fui::GfxRendererTarget::FONT_LABEL, SMALL_FONT_ID);
  const auto& theme = refreshSharedUiThemeTokens(frame.target);
  UiAppHost::UiScreen screen(frame.frame, theme);
  const bool search = paintState.mode == XP_UI_CATALOG_BROWSING && hasSearch();
  catalogScreenHeader(screen, renderer, headerTitle(), search ? fui::bitmapFromIcon(icon_search_32) : fui::BitmapRef{},
                      search ? XP_UI_CATALOG_SEARCH : fui::NO_ACTION);
}

XpUiRect CatalogUiActivity::searchButtonRect(int16_t minimum) const {
  // Keep the SDK header descriptor off the Rust layout call chain's stack.
  fui::HeaderProps header;
  GUI.applyHeaderStatus(renderer, header);
  const auto& m = UITheme::getInstance().getMetrics();
  const int16_t width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
  const auto band = BaseTheme::headerBand(
      renderer, {0, static_cast<int16_t>(m.topPadding), width, static_cast<int16_t>(m.headerHeight)}, header);
  const auto rect = fui::ensureMinTouchRect(
      {static_cast<int16_t>(band.right() - 12 - header.trailingSize),
       static_cast<int16_t>(band.y + 4 + header.actionOffsetY), header.trailingSize, header.trailingSize},
      minimum, {0, 0, width, height});
  return {rect.x, rect.y, rect.width, rect.height};
}

void CatalogUiActivity::cancelRejectedInput(const XpUiInput& input) {
  if (!(input.released & XP_UI_BACK)) return;
  XpUiInput back{};
  back.released = XP_UI_BACK;
  XpUiEffect effect{};
  {
    ModelLock lock(*this);
    effect = updateRust(back);
  }
  applyRustEffect(effect);
}

