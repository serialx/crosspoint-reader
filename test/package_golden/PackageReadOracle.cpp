#include <HalStorage.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"

namespace {
void pod(std::string& bytes, uint32_t value, size_t width) {
  for (size_t i = 0; i < width; ++i) bytes.push_back(static_cast<char>(value >> (8 * i)));
}
void field(std::string& bytes, const std::string& value) {
  pod(bytes, value.size(), 4);
  bytes += value;
}
void hex(const std::string& value) {
  std::cout << value.size() << ':';
  for (unsigned char byte : value) std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
  std::cout << std::dec << ' ';
}
std::string fixture() {
  std::string bytes;
  pod(bytes, 10, 1);
  pod(bytes, 0, 4);
  pod(bytes, 1, 2);
  pod(bytes, 1, 2);
  field(bytes, std::string("A\0B", 3));
  field(bytes, "");
  field(bytes, std::string("\xff\xc0", 2));
  field(bytes, "cover");
  field(bytes, "text");
  const auto lut = bytes.size();
  for (size_t i = 0; i < 4; ++i) bytes[1 + i] = static_cast<char>(lut >> (i * 8));
  const auto spine = lut + 8;
  pod(bytes, spine, 4);
  pod(bytes, spine + 4 + 3 + 4 + 2, 4);
  field(bytes, "abc");
  pod(bytes, 0x12345678, 4);
  pod(bytes, 0x0102, 2);
  field(bytes, std::string("T\0X", 3));
  field(bytes, "abc");
  field(bytes, "anchor");
  pod(bytes, 7, 1);
  pod(bytes, 0x0304, 2);
  return bytes;
}
void snapshot(BookMetadataCache& cache) {
  std::cout << cache.isLoaded() << ' ' << cache.getSpineCount() << ' ' << cache.getTocCount() << ' ';
  const auto& m = cache.coreMetadata;
  hex(m.title);
  hex(m.author);
  hex(m.language);
  hex(m.coverItemHref);
  hex(m.textReferenceHref);
  if (cache.isLoaded() && cache.getSpineCount()) {
    const auto spine = cache.getSpineEntry(0);
    hex(spine.href);
    std::cout << spine.cumulativeSize << ' ' << spine.tocIndex << ' ';
  }
  if (cache.isLoaded() && cache.getTocCount()) {
    const auto toc = cache.getTocEntry(0);
    hex(toc.title);
    hex(toc.href);
    hex(toc.anchor);
    std::cout << unsigned(toc.level) << ' ' << toc.spineIndex << ' ';
  }
  std::cout << '\n';
}
}  // namespace
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string root = argv[1];
  std::filesystem::create_directories(root);
  const auto bytes = fixture();
  for (size_t length = 0; length <= bytes.size(); ++length) {
    std::ofstream(root + "/book.bin", std::ios::binary).write(bytes.data(), length);
    BookMetadataCache cache(root);
    cache.coreMetadata = {"old title", "old author", "old language", "old cover", "old text"};
    std::cout << length << ' ' << cache.load() << ' ';
    snapshot(cache);
  }
  // A short POD read keeps the unread bytes of an existing destination.
  std::ofstream(root + "/book.bin", std::ios::binary).write(bytes.data(), bytes.size());
  BookMetadataCache reused(root);
  assert(reused.load());
  for (const size_t length : {size_t{1}, size_t{2}, size_t{5}, size_t{6}, size_t{8}}) {
    std::ofstream(root + "/book.bin", std::ios::binary).write(bytes.data(), length);
    std::cout << "reuse " << length << ' ' << reused.load() << ' ';
    snapshot(reused);
  }
  std::filesystem::remove_all(root);
}
