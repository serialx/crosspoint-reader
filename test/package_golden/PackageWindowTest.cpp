#include <ZipFile.h>

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string root = argv[1];
  std::filesystem::create_directories(root);
  for (const int count : {14, XP_BOOK_METADATA_WINDOW, 2 * XP_BOOK_METADATA_WINDOW}) {
    zipEntrySizes.clear();
    std::vector<uint32_t> totals;
    BookMetadataCache writer(root);
    assert(writer.beginWrite() && writer.beginContentOpfPass());
    for (int i = 0; i < count; ++i) {
      const auto href = "chapter" + std::to_string(i);
      const uint32_t size = 17 + i * 31;
      zipEntrySizes[href] = size;
      totals.push_back((i ? totals.back() : 0) + size);
      writer.createSpineEntry(href);
    }
    assert(writer.endContentOpfPass() && writer.beginTocPass());
    assert(writer.endTocPass() && writer.endWrite());
    assert(writer.buildBookBin("book.epub", {}));
    BookMetadataCache reader(root);
    assert(reader.load());
    // Check every destination, including offsets 12, 13 and 31, then reload backwards.
    for (int i = 0; i < count; ++i) assert(reader.getCumulativeSize(i) == totals[i]);
    for (int i = count - 1; i >= 0; --i) assert(reader.getCumulativeSize(i) == totals[i]);
    assert(reader.getCumulativeSize(0) == totals[0]);
    assert(reader.getCumulativeSize(count - 1) == totals.back());
  }
  std::filesystem::remove_all(root);
}
