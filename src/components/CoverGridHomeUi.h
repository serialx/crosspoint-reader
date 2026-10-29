#pragma once
#include <xp_ui_home.h>

#include <array>
#include <string>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"

// Screen-owned bitmap resources; Rust owns the grid layout and input geometry.
class CoverGridHomeUi final {
 public:
  static constexpr int THUMB_HEIGHT = 400;
  static constexpr int MAX_BOOKS = 7;
  static_assert(MAX_BOOKS <= HomeCoverCache::MAX_COVERS);
  explicit CoverGridHomeUi(GfxRenderer& renderer) : coverCache(renderer), renderer(renderer) {}
  void begin(const std::vector<RecentBook>& books, bool continuing);
  void refreshCoverPaths();
  int thumbHeightFor() const { return thumbHeight > 0 ? thumbHeight : THUMB_HEIGHT; }
  bool prepare(const XpUiHomeGridGeometry& geometry);
  XpUiHomeGridLayout layout() const;
  XpUiHomeGridTexts textServices();
  const char* text(uint32_t handle) const;
  void paint(uint32_t handle, XpUiRect rect);

 private:
  HomeCoverCache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books = nullptr;
  std::array<std::string, MAX_BOOKS> coverPaths;
  int thumbHeight = 0;
  int progress = -1;
  char progressText[12]{};
};
