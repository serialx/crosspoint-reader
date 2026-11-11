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

struct SdCardFontFileInfo {
  // Filename, stored only when SdCardFontFamilyInfo::filePath() cannot rebuild
  // it: a .cpfont not named "<Family>_<size>.cpfont", or any vector file. Null
  // for the canonical name, so downloaded families keep no per-file heap strings
  // and the entry is two pointers wide (8 bytes on the ESP32-C3).
  std::unique_ptr<char[]> name;
  uint8_t pointSize;  // parsed from filename: 14 (0 for size-free vector fonts)
  uint8_t style;      // .cpfont: always 0 (all 4 styles bundled in one file).
                      // Vector family in a folder: the style ROLE of this file —
                      // 0=regular, 1=bold, 2=italic, 3=bold-italic (parsed from
                      // the filename). A loose vector file is always role 0.
};

struct XpFontFiles;
// The view borrows this vector and its filenames until the next mutation.
XpFontFiles sdFontFileView(const std::vector<SdCardFontFileInfo>& files);

struct SdCardFontFamilyInfo {
  // Buffer size for filePath(), matching the installer's own path buffers.
  // Discovery drops files whose path would not fit, so every registered file
  // can be opened through filePath() with a buffer of this size.
  static constexpr size_t PATH_BUFFER_SIZE = 160;

  std::string name;  // directory name, e.g. "NotoSansCJK"
  std::vector<SdCardFontFileInfo> files;
  // SdCardFontRegistry::FONTS_DIR_HIDDEN or FONTS_DIR_VISIBLE: static storage,
  // never freed, so a pointer is enough.
  const char* root = nullptr;
  // true for TrueType/OpenType files (.ttf/.otf/.ttc) rendered at any size via
  // the FreeInkFont engine (see TtfEpdFont / SdCardFontSystem): `files` holds
  // one entry per style role, each with its filename in `name` and pointSize 0
  // (size-free). false = a directory of pre-rasterized .cpfont files.
  bool vector = false;
  bool loose = false;  // vector file directly under `root`, no family directory

  // Writes the file's SD path, e.g. "/.fonts/NotoSansCJK/NotoSansCJK_14.cpfont".
  // Returns false when `outSize` is too small; `out` is then truncated.
  bool filePath(const SdCardFontFileInfo& file, char* out, size_t outSize) const;

  const SdCardFontFileInfo* findFile(uint8_t size, uint8_t style = 0) const;
  // Installed file closest to `pointSize` (ties → smaller). nullptr when the
  // family ships nothing in `style`.
  const SdCardFontFileInfo* findNearestSize(uint8_t pointSize, uint8_t style = 0) const;
  bool hasSize(uint8_t size) const;
  std::vector<uint8_t> availableSizes() const;
};

class SdCardFontRegistry {
 public:
  static constexpr int MAX_SD_FAMILIES = 128;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Invalidates pointers and references
  // obtained from getFamilies()/findFamily(). Returns true if any families found.
  bool discover();

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(std::string_view name) const;
  int getFamilyIndex(std::string_view name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

#if CROSSPOINT_VECTOR_FONTS
  // FtFont::ReadFn over a HalFile* ctx (absolute-offset reads; count 0 is a
  // seek probe). Shared by face inspection here and streamed TTF sources
  // (SdCardFontSystem).
  static unsigned long halFileRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif

 private:
  static bool parseFilename(const char*, uint8_t& size, uint8_t& style);
  static bool isCanonicalCpfontName(const char*, const std::string& family, uint8_t size);

  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

#if CROSSPOINT_VECTOR_FONTS
  // Refine each vector file's style role from its real face metadata
  // (FtFont::inspectStream: OS/2 weight + italic flag), keeping the
  // filename-derived role when the face can't be read. Then dedup by role.
  static void refineVectorStyles(const char* dirPath, std::vector<SdCardFontFileInfo>& files);
#endif
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
