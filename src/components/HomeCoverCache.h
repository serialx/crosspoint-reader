#pragma once

#include <Bitmap.h>
#include <FreeInkUICore.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <xp_ui_home.h>

#include <string>

class GfxRenderer;

// Screen-lifetime thumbnail decoding and PSRAM region snapshots.
class HomeCoverCache {
 public:
  static constexpr size_t MAX_COVERS = 7;
  explicit HomeCoverCache(GfxRenderer& renderer) : renderer(renderer) {}
  void begin();
  void prepare();
  void invalidate();
  void invalidate(size_t index);
  bool paint(freeink::ui::Rect rect, size_t index, const std::string& path);

 private:
  HalMemory::PsramBuffer coverCache;
  XpUiHomeCache cacheState{};

  HalFile coverFile;
  Bitmap coverBitmap{coverFile};
  GfxRenderer& renderer;
};
