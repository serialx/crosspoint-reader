#include "WifiSelectionActivity.h"

#include "components/UiAppHelpers.h"

WifiSelectionActivity::WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& input, bool automatic)
    : Activity("WifiSelection", renderer, input), textTarget(makeUiTarget(renderer)), allowAutoConnect(automatic) {}
WifiSelectionActivity::~WifiSelectionActivity() { releaseResources(); }
void WifiSelectionActivity::releaseResources() {
  if (!resourcesLive) return;
  resourcesLive = false;
  textTarget.~GfxRendererTarget();
}

#if defined(CONFIG_IDF_TARGET_ESP32C3)
static_assert(sizeof(WifiSelectionActivity) == 248);
#endif
