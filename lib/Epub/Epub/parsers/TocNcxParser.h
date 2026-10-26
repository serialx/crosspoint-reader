#pragma once
#include <Print.h>
#include <xp_ffi.h>

#include <string>

class BookMetadataCache;

class TocNcxParser final : public Print {
  const std::string& baseContentPath;
  size_t remainingSize;
  XpPkg* parser = nullptr;
  BookMetadataCache* cache;

 public:
  TocNcxParser(const TocNcxParser&) = delete;
  TocNcxParser& operator=(const TocNcxParser&) = delete;
  explicit TocNcxParser(const std::string& baseContentPath, const size_t xmlSize, BookMetadataCache* cache)
      : baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~TocNcxParser() override { xp_pkg_destroy(parser); }

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
