#include "HomeCoverCache.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>

#include "UITheme.h"

namespace fui = freeink::ui;

void HomeCoverCache::begin() {
  // Cover regions do not overlap. Each may widen by one physical byte per row.
  const auto capacity = xp_ui_home_cache_init(
      &cacheState, renderer.getRegionByteSize(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()),
      renderer.getScreenWidth(), renderer.getScreenHeight());
  coverCache = HalMemory::allocatePsram(capacity);
  xp_ui_home_cache_allocated(&cacheState, coverCache != nullptr);
  if (!coverCache) {
    LOG_ERR("HOME", "PSRAM cover cache unavailable (%u bytes); rendering uncached", unsigned(capacity));
  }
}

void HomeCoverCache::invalidate() { xp_ui_home_cache_invalidate(&cacheState); }

void HomeCoverCache::invalidate(size_t index) {
  if (index < MAX_COVERS) xp_ui_home_cache_invalidate_slot(&cacheState, index);
}

void HomeCoverCache::prepare() { xp_ui_home_cache_prepare(&cacheState, static_cast<int>(renderer.getOrientation())); }

bool HomeCoverCache::paint(fui::Rect rect, size_t index, const std::string& path) {
  if (index >= MAX_COVERS) return false;
  const XpUiRect region{rect.x, rect.y, rect.width, rect.height};
  const auto cached = xp_ui_home_cache_lookup(&cacheState, index, region);
  uint8_t* const cacheData = coverCache.get();
  if (cached.bytes != 0 &&
      renderer.copyBufferToRegion(rect.x, rect.y, rect.width, rect.height, cacheData + cached.offset, cached.bytes)) {
    return true;
  }
  xp_ui_home_cache_invalidate_slot(&cacheState, index);
  bool drawn = false;
  if (!path.empty() && Storage.openFileForRead("HOME", path, coverFile)) {
    if (coverBitmap.parseHeaders() == BmpReaderError::Ok && coverBitmap.getWidth() > 0 && coverBitmap.getHeight() > 0) {
      // The art nudges a few px right of center; the spine below hugs its
      // left edge either way.
      constexpr int ART_SHIFT = 3;
      drawn = GUI.drawCoverThumbFill(renderer, coverBitmap, Rect{rect.x, rect.y, rect.width, rect.height}, ART_SHIFT);
      if (drawn) {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
        // False spine glued to the art's left edge (centered art means that
        // edge moves with each cover's fit margin): a dark band with a
        // dithered crease makes every cover read as a bound book. Drawn here
        // so the PSRAM snapshot below captures it.
        const int artLeft = std::max<int>(rect.x, rect.x + (rect.width - coverBitmap.getWidth()) / 2 + ART_SHIFT);
        renderer.fillRect(artLeft, rect.y, 3, rect.height);
        renderer.fillRectDither(artLeft + 3, rect.y, 2, rect.height, Color::LightGray);
      }
    }
    coverFile.close();
  }
  if (!drawn) {
    GUI.drawCoverPlaceholder(renderer, Rect{rect.x, rect.y, rect.width, rect.height});
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }
  if (coverCache) {
    const size_t needed = renderer.getRegionByteSize(rect.x, rect.y, rect.width, rect.height);
    const auto slot = xp_ui_home_cache_reserve(&cacheState, index, region, needed);
    if (slot.bytes != 0) {
      const bool copied =
          renderer.copyRegionToBuffer(rect.x, rect.y, rect.width, rect.height, cacheData + slot.offset, slot.bytes);
      xp_ui_home_cache_copied(&cacheState, index, copied);
    }
  }
  return true;  // The cover slot is painted, including fallback art.
}
