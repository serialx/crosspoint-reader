#include "KOReaderUiActivity.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstddef>

#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

static_assert(sizeof(XpKoState) == 12 && sizeof(XpKoOptions) == 12);
static_assert(offsetof(XpKoOptions, local_percentage) == 0 && offsetof(XpKoOptions, smart) == 4);

KOReaderUiActivity::KOReaderUiActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                                       XpKoKind kind)
    : RustActivityAdapter(name, renderer, input), textTarget(makeUiTarget(renderer)) {
  xp_ui_koreader_init(&model, kind, 0, 0);
}
bool KOReaderUiActivity::initializeUi() {
  const unsigned count = model.kind == XP_KO_SYNC ? 32 : 6;
  commands = makeUniqueNoThrow<XpUiCommand[]>(count);
  if (!commands) {
    LOG_ERR("RUI", "Cannot allocate %u command bytes", count * unsigned(sizeof(XpUiCommand)));
    return false;
  }
  refreshSharedUiThemeTokens(textTarget);
  return true;
}
void KOReaderUiActivity::onExit() {
  publication.ready = false;
  commands.reset();
  Activity::onExit();
}
bool KOReaderUiActivity::preventAutoSleep() {
  ModelLock lock(*this);
  return xp_ui_koreader_busy(&model);
}
void KOReaderUiActivity::publish(XpKoPhase phase, XpKoMessage message, uint8_t selected, const char* error) {
  const char* text = nullptr;
  switch (message) {
    case XP_KO_NO_MESSAGE:
      if (phase == XP_KO_FAILED) text = "";
      break;
    case XP_KO_HASH:
      text = tr(STR_CALC_HASH);
      break;
    case XP_KO_HASH_FAILED:
      text = tr(STR_HASH_FAILED);
      break;
    case XP_KO_CLIPPINGS:
      text = tr(STR_SYNCING_CLIPPINGS);
      break;
    case XP_KO_CLIPPINGS_FAILED:
      text = tr(STR_CLIPPING_SYNC_FAILED);
      break;
    case XP_KO_FETCH:
      text = tr(STR_FETCH_PROGRESS);
      break;
    case XP_KO_UPLOAD:
      text = tr(STR_UPLOAD_PROGRESS);
      break;
    case XP_KO_SAVE_FAILED:
      text = tr(STR_SAVE_PROGRESS_FAILED);
      break;
    case XP_KO_WIFI_FAILED:
      text = tr(STR_WIFI_CONN_FAILED);
      break;
    case XP_KO_AUTH_BUSY:
      text = model.kind == XP_KO_SIGNUP ? tr(STR_CREATING_ACCOUNT) : tr(STR_AUTHENTICATING);
      break;
    case XP_KO_AUTH_OK:
      text = model.kind == XP_KO_SIGNUP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS);
      break;
    case XP_KO_USER_EXISTS:
      text = tr(STR_USERNAME_TAKEN);
      break;
    case XP_KO_SERVICE_ERROR:
      text = error ? error : "";
      break;
  }
  ModelLock lock(*this);
  xp_ui_koreader_publish(&model, phase, selected);
  if (text) status = text;
}
void KOReaderUiActivity::markReturn() {
  ModelLock lock(*this);
  xp_ui_koreader_mark_return(&model, millis());
}
const char* KOReaderUiActivity::resourceText(uint32_t handle) const {
  if (handle == 3) return paintedStatus;
  // Handles bind view semantics to the one generated translation table.
  static constexpr StrId ids[] = {StrId::STR_REMOTE_LABEL,       StrId::STR_LOCAL_LABEL,
                                  StrId::STR_APPLY_REMOTE,       StrId::STR_UPLOAD_LOCAL,
                                  StrId::STR_NO_REMOTE_MSG,      StrId::STR_UPLOAD_PROMPT,
                                  StrId::STR_NO_CREDENTIALS_MSG, StrId::STR_KOREADER_SETUP_HINT,
                                  StrId::STR_UPLOAD_SUCCESS,     StrId::STR_ALREADY_SYNCED,
                                  StrId::STR_SYNC_FAILED_MSG,    StrId::STR_AUTH_SUCCESS,
                                  StrId::STR_ACCOUNT_CREATED,    StrId::STR_SYNC_READY,
                                  StrId::STR_AUTH_FAILED,        StrId::STR_SIGNUP_FAILED,
                                  StrId::STR_KOREADER_SYNC,      StrId::STR_PROGRESS_FOUND,
                                  StrId::STR_KOREADER_AUTH,      StrId::STR_SIGN_UP};
  if (handle < 64 || handle - 64 >= sizeof(ids) / sizeof(ids[0])) return nullptr;
  return xp_ui_koreader_text(static_cast<uint8_t>(I18N.getLanguage()), static_cast<uint16_t>(ids[handle - 64]));
}
XpUiInterest KOReaderUiActivity::rustInterest(uint32_t now) const { return xp_ui_koreader_interest(&model, now); }
void KOReaderUiActivity::rustSceneFailed() {
  model.touch_armed = 0;
  touchDispatched = false;
}
XpUiEffect KOReaderUiActivity::updateRust(const XpUiInput& input) {
  auto effect = xp_ui_koreader_update(&model, &input);
  touchDispatched = input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) && effect.kind == XP_UI_ACTIVATE;
  // The original touch router invalidates after the synchronous handler.
  if (touchDispatched) effect.invalidate = 0;
  return effect;
}
bool KOReaderUiActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    painting = model;
    paintedStatus = status;
    snapshotResources();
  }
  prepareText();
  if (!commands && !initializeUi()) {
    return false;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const auto& tokens = refreshSharedUiThemeTokens(textTarget);
  const auto device = textTarget.deviceContext();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto header = painting.kind == XP_KO_SYNC ? UITheme::getInstance().getHeaderRect(renderer, true, false)
                                                  : Rect{0, m.topPadding, renderer.getScreenWidth(), m.headerHeight};
  const auto fonts = uiScaleSpec();
  XpKoLayout layout{};
  auto& style = layout.list;
  style.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                  static_cast<int16_t>(renderer.getScreenHeight())};
  style.area = {device.safeArea.left, device.safeArea.top,
                static_cast<int16_t>(device.width - device.safeArea.left - device.safeArea.right),
                static_cast<int16_t>(device.height - device.safeArea.top - device.safeArea.bottom)};
  style.font = fonts.bodyFontId;
  style.min_touch = std::max(device.minTouchSize, tokens.minTouchSize);
  style.row_height = mappedInput.hasTouch() ? tokens.rowHeight : m.listRowHeight;
  style.row_gap = tokens.listRowGap;
  style.inset = tokens.listInset;
  style.padding = tokens.listSidePadding;
  style.radius = tokens.listRowRadius;
  style.selection = static_cast<uint8_t>(tokens.listSelectionStyle);
  style.bold = tokens.bodyText.bold;
  layout.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                 static_cast<int16_t>(safe.height)};
  layout.header = {static_cast<int16_t>(header.x), static_cast<int16_t>(header.y), static_cast<int16_t>(header.width),
                   static_cast<int16_t>(header.height)};
  layout.small_font = fonts.smallFontId;
  layout.fixed_font = UI_10_FONT_ID;
  layout.header_bottom = m.topPadding + m.headerHeight;
  layout.hints_height = m.buttonHintsHeight;
  layout.spacing = m.verticalSpacing;
  layout.space_xs = tokens.spaceXs;
  layout.space_sm = tokens.spaceSm;
  layout.space_md = tokens.spaceMd;
  layout.space_lg = tokens.spaceLg;
  layout.body_height = renderer.getLineHeight(fonts.bodyFontId);
  layout.small_height = renderer.getLineHeight(fonts.smallFontId);
  layout.fixed_height = renderer.getLineHeight(UI_10_FONT_ID);
  layout.touch = mappedInput.hasTouch();
  const auto* deviceText = resourceText(6);
  layout.has_device = deviceText && deviceText[0];
  XpUiBuffer buffer{commands.get(), hitBuffers[nextHits], static_cast<uint16_t>(painting.kind == XP_KO_SYNC ? 32 : 6),
                    2};
  return xp_ui_koreader_view(&painting, &layout, &buffer, &scene);
}
XpUiPaint KOReaderUiActivity::rustResources() {
  return {
      this, nullptr, nullptr,
      [](void* context, uint32_t handle, uint32_t generation) -> uint8_t {
        if (generation != 1) return false;
        return handle == 1 || handle == 2 || handle == 9 || handle == 10 ||
               static_cast<KOReaderUiActivity*>(context)->resourceText(handle) != nullptr;
      },
      [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
        auto& self = *static_cast<KOReaderUiActivity*>(context);
        const auto r = command->rect;
        if (command->resource == 1) {
          GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, self.resourceText(command->font));
          return;
        }
        if (command->resource == 2) {
          const char* confirm = "";
          const char* up = "";
          const char* down = "";
          switch (command->font) {
            case 0:
              break;
            case 1:
              confirm = tr(STR_SELECT);
              up = tr(STR_DIR_UP);
              down = tr(STR_DIR_DOWN);
              break;
            case 2:
              confirm = tr(STR_UPLOAD);
              break;
            case 3:
              confirm = tr(STR_DONE);
              break;
          }
          const auto labels = self.mappedInput.mapLabels(tr(STR_BACK), confirm, up, down);
          GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          return;
        }
        if (command->kind == XP_UI_BITMAP) {
          const auto& icon = command->resource == 9 ? icon_download_24 : icon_upload_24;
          const auto old = self.renderer.getClipRect();
          self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
          self.textTarget.bitmap(
              {r.x, r.y, r.w, r.h}, freeink::ui::bitmapFromIcon(icon), freeink::ui::BitmapMode::Contain,
              freeink::ui::Paint::solid(command->paint == 1 ? freeink::ui::Color::White : freeink::ui::Color::Black));
          self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
          return;
        }
        auto text = *command;
        if (text.max_lines == 0 && text.align == 1) {
          text.rect.x += (text.rect.w - self.renderer.getTextWidth(text.font, self.resourceText(text.resource),
                                                                   static_cast<EpdFontFamily::Style>(text.style))) /
                         2;
        }
        self.renderer.drawUiText(text, clip, self.resourceText(text.resource));
      }};
}

void KOReaderUiActivity::applyRustEffect(const XpUiEffect& effect) {
  const bool blocked =
      rustSceneRejected() && effect.kind == XP_UI_ACTIVATE && !(model.kind != XP_KO_SYNC && effect.value == 3);
  if (blocked) {
    requestUpdate();
    return;
  }
  performEffect(effect);
  if (touchDispatched) requestUpdate();
  XpUiEffect pending{};
  {
    ModelLock lock(*this);
    if (model.pending_back) {
      const XpUiInput empty{};
      pending = xp_ui_koreader_update(&model, &empty);
    }
  }
  if (pending.kind) performEffect(pending);
}

void KOReaderUiActivity::cancelRejectedInput(const XpUiInput& input) {
  XpUiInput back{};
  back.pressed = input.pressed & XP_UI_BACK;
  back.released = input.released & XP_UI_BACK;
  back.now_ms = input.now_ms;
  XpUiEffect effect{};
  {
    ModelLock lock(*this);
    effect = updateRust(back);
  }
  applyRustEffect(effect);
}

bool KOReaderUiActivity::rustSceneCurrent() const {
  return model.kind == painting.kind && model.phase == painting.phase;
}
