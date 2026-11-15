#include "FrozenRenderer.h"

static const char* prepareVisualText(const char*, std::string&, BidiUtils::BidiBaseDir);
static const char* resolveVisualText(const char* text, std::string& storage, BidiUtils::BidiBaseDir dir) {
  return prepareVisualText(text, storage, dir);
}
// Reuse the frozen method bodies without binding their symbols to the production renderer.
#define GfxRenderer FrozenRenderer
#include "measurement.inc"
#include "visual.inc"
#undef GfxRenderer
