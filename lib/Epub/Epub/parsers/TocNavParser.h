#pragma once
#include <Print.h>
#include <xp_ffi.h>

#include <string>

class BookMetadataCache;

// Parser for EPUB 3 nav.xhtml navigation documents
// Parses HTML5 nav elements with epub:type="toc" to extract table of contents
class TocNavParser final : public Print {
  const std::string& baseContentPath;
  size_t remainingSize;
  XpPkg* parser = nullptr;
  BookMetadataCache* cache;

 public:
  TocNavParser(const TocNavParser&) = delete;
  TocNavParser& operator=(const TocNavParser&) = delete;
  explicit TocNavParser(const std::string& baseContentPath, const size_t xmlSize, BookMetadataCache* cache)
      : baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~TocNavParser() override { xp_pkg_destroy(parser); }

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
