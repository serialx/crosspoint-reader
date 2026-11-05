#pragma once
#include <HalStorage.h>

#include <string>
#include <string_view>
class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title, author, language, coverItemHref, textReferenceHref;
  };
  explicit BookMetadataCache(const std::string&) { booktest::step("cache-create", ""); }
  ~BookMetadataCache() { booktest::step("cache-destroy", ""); }
  bool beginWrite() { return booktest::step("begin", ""); }
  bool beginContentOpfPass() { return booktest::step("begin-opf", ""); }
  void createSpineEntry(const std::string& name) { booktest::step("spine", name); }
  bool endContentOpfPass() { return booktest::step("end-opf", ""); }
  bool beginTocPass() { return booktest::step("begin-toc", ""); }
  bool createTocEntry(std::string_view title, std::string_view href, std::string_view, uint8_t level) {
    return booktest::step("toc", std::string(title) + ":" + std::string(href), level);
  }
  bool endTocPass() { return booktest::step("end-toc", ""); }
  bool endWrite() { return booktest::step("end", ""); }
  bool buildBookBin(const std::string& path, const BookMetadata& metadata) {
    return booktest::step("book-bin",
                          path + ":" + metadata.title + ":" + metadata.language + ":" + metadata.coverItemHref);
  }
  bool cleanupTmpFiles() const { return booktest::step("cleanup", ""); }
  bool load() { return booktest::step("cache-load", ""); }
};
