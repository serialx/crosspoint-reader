#pragma once
#include <Epub.h>
#include <GfxRenderer.h>

#include <memory>
#include <optional>
namespace fake {
inline int cacheMode = 0;
}
class Section {
  int spine;

 public:
  Section(const std::shared_ptr<Epub>&, int spine, GfxRenderer&) : spine(spine) {}
  std::optional<int> getCachedPageCount() const {
    return fake::cacheMode ? std::optional<int>(11 + (fake::cacheMode == 5 ? spine : 0)) : std::nullopt;
  }
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset, bool image) const {
    return fake::cacheMode == 1 ? std::optional<uint16_t>(offset / 9 + (image ? 1 : 0)) : std::nullopt;
  }
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t p) const {
    return fake::cacheMode == 2 && p < 9 ? std::optional<uint16_t>(p * 2) : std::nullopt;
  }
  std::optional<uint16_t> getPageForListItemIndex(uint16_t p) const {
    return fake::cacheMode == 3 ? std::optional<uint16_t>(p + 2) : std::nullopt;
  }
  std::optional<uint16_t> getPageForAnchor(const std::string& p) const {
    return fake::cacheMode == 4 && p == "anchor" ? std::optional<uint16_t>(7) : std::nullopt;
  }
};
