#pragma once

#include <xp_font_registry.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "VectorFontSupport.h"

#ifdef XP_SD_FONT_CATALOG
// Data loans from the Rust catalog; no native font/name/file ownership.
struct SdFontFamily : XpSdCatalogView {
  static constexpr size_t PATH_BUFFER_SIZE = 160;
  const XpLoadedFonts* owner = nullptr;
  size_t index = 0;
  std::span<const XpSdFile> entries() const { return {files, count}; }
  bool filePath(const XpSdFile& file, char* out, size_t capacity) const {
    const XpSdCatalogQuery q{
        XP_SD_PATH, index, static_cast<size_t>(&file - files), nullptr, 0, reinterpret_cast<uint8_t*>(out), capacity};
    return xp_sd_catalog_query(owner, &q, nullptr) != 0;
  }
  std::vector<uint8_t> availableSizes() const {
    std::vector<uint8_t> result;
    for (unsigned n = 0; n < 256; ++n)
      if (sizes[n / 32] & (1u << (n % 32))) result.push_back(n);
    return result;
  }
};
class SdFontCatalog {
 public:
  static constexpr int MAX_SD_FAMILIES = 128;
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";
  void bind(const XpLoadedFonts* owner) { owner_ = owner; }
  const XpLoadedFonts* raw() const { return owner_; }
  int fontId(std::string_view name) const {
    const XpSdCatalogQuery q{XP_SD_LOADED_ID, 0, 0, reinterpret_cast<const uint8_t*>(name.data()), name.size()};
    return static_cast<int32_t>(xp_sd_catalog_query(owner_, &q, nullptr));
  }
  void markDirty() { xp_sd_catalog_dirty(owner_, XP_SD_MARK_DIRTY, CROSSPOINT_VECTOR_FONTS); }
  void refreshIfDirty() { xp_sd_catalog_dirty(owner_, XP_SD_REFRESH_DIRTY, CROSSPOINT_VECTOR_FONTS); }
  void discover() { xp_sd_catalog_dirty(owner_, XP_SD_DISCOVER, CROSSPOINT_VECTOR_FONTS); }
  size_t getFamilyCount() const {
    const XpSdCatalogQuery q{XP_SD_COUNT};
    return owner_ ? xp_sd_catalog_query(owner_, &q, nullptr) : 0;
  }
  std::optional<SdFontFamily> family(size_t index) const {
    const XpSdCatalogQuery q{XP_SD_AT, index};
    return view(q);
  }
  std::optional<SdFontFamily> findFamily(std::string_view name) const {
    const XpSdCatalogQuery q{XP_SD_FIND, 0, 0, reinterpret_cast<const uint8_t*>(name.data()), name.size()};
    return view(q);
  }
  const char* findFamilyRoot(const char* name) const { return root(name, XP_SD_ROOT); }
  const char* defaultWriteRoot() const { return root(nullptr, XP_SD_WRITE_ROOT); }

 private:
  const XpLoadedFonts* owner_ = nullptr;
  const char* root(const char* name, XpSdCatalogOperation operation) const {
    const XpSdCatalogQuery q{static_cast<uint32_t>(operation), 0, 0, reinterpret_cast<const uint8_t*>(name)};
    return reinterpret_cast<const char*>(xp_sd_catalog_query(owner_, &q, nullptr));
  }
  std::optional<SdFontFamily> view(const XpSdCatalogQuery& q) const {
    if (!owner_) return std::nullopt;
    SdFontFamily family;
    const auto index = xp_sd_catalog_query(owner_, &q, &family);
    if (!index) return std::nullopt;
    family.owner = owner_;
    family.index = index - 1;
    return family;
  }
};

#endif

// Streamed vector-font physical reads remain native until PT-VEC.
class SdCardFontRegistry {
 public:
#if CROSSPOINT_VECTOR_FONTS
  static unsigned long halFileRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif
};
