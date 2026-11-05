#pragma once
#include <Print.h>

#include <memory>
#include <string>
#include <string_view>
class BookMetadataCache;
struct Txt {
  static bool isTxtOrMd(std::string_view) { return false; }
  static bool validateCache(const std::string&, const std::string&, size_t) { return false; }
  static bool buildTxtCache(const std::string&, const std::string&, std::unique_ptr<BookMetadataCache>&) {
    return false;
  }
  static bool streamTxtToHtml(const std::string&, Print&) { return false; }
  static std::string findCompanionCoverImage(const std::string&) { return {}; }
  static bool convertCoverImageToBmp(const std::string&, const std::string&, int = 0, bool = false, bool = false) {
    return false;
  }
};
