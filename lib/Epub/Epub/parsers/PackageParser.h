#pragma once
#include <HalStorage.h>
#include <Print.h>
#include <xp_ffi.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class BookMetadataCache;

// Print adapter and HAL owner for one Rust package document pass.
class PackageParser final : public Print {
  uint32_t kind;
  std::string_view base;
  size_t size;
  std::string_view cachePath;
  BookMetadataCache* cache;
  bool metadataOnly;
  HalFile items;
  XpPkgPass* pass = nullptr;
  static uint32_t control(void*, uint32_t, uint32_t);
  static size_t read(void*, uint32_t, uint8_t*, size_t);
  static size_t write(void*, uint32_t, const uint8_t*, size_t);
  static uint8_t seek(void*, uint32_t, uint32_t);

 public:
  // base/cachePath and cache are borrowed through destruction, including repeated setup().
  PackageParser(uint32_t kind, std::string_view base, size_t size, std::string_view cachePath,
                BookMetadataCache* cache = nullptr, bool metadataOnly = false)
      : kind(kind), base(base), size(size), cachePath(cachePath), cache(cache), metadataOnly(metadataOnly) {}
  PackageParser(const PackageParser&) = delete;
  PackageParser& operator=(const PackageParser&) = delete;
  ~PackageParser() override;
  bool setup();
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t len) override;
  bool finish() { return xp_pkg_pass_finish(pass); }
  bool needsProtectionCheck() const { return xp_pkg_pass_count(pass) != 0; }
  // Borrowed until setup(), write(), finish(), or destruction; value() returns a copy.
  std::string_view view(uint32_t field = 0) const;
  std::string value(uint32_t field = 0) const;
  std::optional<float> seriesIndex() const;
  std::vector<std::string> cssFiles() const;
};
