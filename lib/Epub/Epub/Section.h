#pragma once
#include <memory>
#include <optional>
#include <string>

#include "Epub.h"

class GfxRenderer;

// Cache-only view retained for ProgressMapper. The reader owns its build in Rust.
class Section {
  std::string filePath;

 public:
  explicit Section(const std::shared_ptr<Epub>& epub, int spineIndex, GfxRenderer& renderer);
  std::optional<uint16_t> getCachedPageCount() const;
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t pIndex) const;
  std::optional<uint16_t> getPageForListItemIndex(uint16_t liIndex) const;
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset, bool preferFirstAtOffset = false) const;
};
