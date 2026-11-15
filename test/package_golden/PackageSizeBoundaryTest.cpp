#include <ZipFile.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"

namespace {
void build(const std::string& root, const std::string& path, int count, bool raw) {
  std::filesystem::create_directories(root);
  zipEntrySizes = {{"alpha", 5}, {"beta", 9}};
  ZipFile::failOpen = raw;
  BookMetadataCache cache(root);
  assert(cache.beginWrite() && cache.beginContentOpfPass());
  const std::string hrefs[] = {"alpha", "a/../beta", "alpha", "missing"};
  for (int i = 0; i < count; ++i) cache.createSpineEntry(hrefs[i % 4]);
  assert(cache.endContentOpfPass() && cache.beginTocPass());
  assert(cache.endTocPass() && cache.endWrite());
  assert(cache.buildBookBin(path, {"Title", "Author", "en", "cover", "text"}));
  assert(cache.load());
  assert(cache.coreMetadata.title == "Title" && cache.coreMetadata.author == "Author");
  assert(cache.coreMetadata.language == "en" && cache.coreMetadata.coverItemHref == "cover");
  assert(cache.coreMetadata.textReferenceHref == "text");
  assert(cache.getSpineCount() == count && cache.getTocCount() == 0);
  uint32_t total = 0;
  const uint32_t sizes[] = {5, 9, 5, 0};
  for (int i = 0; i < count; ++i) {
    total += raw ? 31 : sizes[i % 4];
    const auto entry = cache.getSpineEntry(i);
    assert(entry.href == hrefs[i % 4]);
    assert(entry.cumulativeSize == total && entry.tocIndex == -1);
    assert(cache.getCumulativeSize(i) == total);
  }
  ZipFile::failOpen = false;
}
}  // namespace
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string root = argv[1];
  std::filesystem::create_directories(root);
  for (const int count : {0, 1, 399, 400, 2048, 2049}) build(root + "/zip", "book.epub", count, false);
  for (const char* suffix : {".TXT", ".Md"}) {
    const std::string path = root + "/source" + suffix;
    std::ofstream(path, std::ios::binary) << std::string(31, 'x');
    build(root + "/raw", path, 400, true);
  }
  std::filesystem::remove_all(root);
}
