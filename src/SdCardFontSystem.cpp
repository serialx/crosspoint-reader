#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <VectorFontSupport.h>

#include "CrossPointSettings.h"
#include "fontIds.h"

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.bind(renderer.getFontMap().raw());
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* family, uint8_t points) {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(family, points);
  };
  SETTINGS.sdFontResolverCtx = this;
  runEffects(renderer, true);
}
void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) { runEffects(renderer, false); }
void SdCardFontSystem::runEffects(GfxRenderer& renderer, bool begin) {
  XpSdSettings request{};
  XpSdContinuation token{};
  const auto* owner = registry_.raw();
  fontEffect(XP_SD_BORROW_SETTINGS, request, token, renderer);
  auto action = begin ? xp_sd_fonts_begin(owner, &request, &token) : xp_sd_fonts_ensure(owner, &request, &token);
  while (action != XP_SD_COMPLETE) {
    fontEffect(action, request, token, renderer);
    if (action == XP_SD_REJECTED) return;
    action = xp_sd_fonts_ensure(owner, &request, &token);
  }
}
void SdCardFontSystem::fontEffect(XpSdAction action, XpSdSettings& request, XpSdContinuation&, GfxRenderer&) {
  switch (action) {
    case XP_SD_BORROW_SETTINGS:
      request = {SETTINGS.sdFontFamilyName,
                 &SETTINGS.fontPointSize,
                 {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID},
                 CROSSPOINT_VECTOR_FONTS};
      break;
    case XP_SD_PERSIST_SETTINGS:
      SETTINGS.saveToFile();
      break;
    case XP_SD_REJECTED:
      LOG_ERR("SDFS", "Rejected font continuation");
      break;
    case XP_SD_COMPLETE:
#ifdef XP_APP_TEST
    case XP_SD_PREPARE_VECTOR:
    case XP_SD_UNLOAD_VECTOR:
    case XP_SD_LOAD_VECTOR:
#endif
      break;
  }
}
int SdCardFontSystem::resolveFontId(const char* family, uint8_t) const {
  return registry_.fontId(family ? family : "");
}
