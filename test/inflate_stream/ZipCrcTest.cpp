#include <HalStorage.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
class CrcSink : public Print {
 public:
  uint32_t crc = UINT32_MAX;
  size_t write(uint8_t byte) override {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    return 1;
  }
};

TEST(ZipInflate, EveryEpubEntryMatchesCentralDirectoryCrc) {
  size_t archives = 0, entries = 0;
  const std::string path = "/book.epub";
  const char* fixtures = std::getenv("INFLATE_EPUB_FIXTURES");
  for (const auto& item : std::filesystem::directory_iterator(fixtures ? fixtures : EPUB_FIXTURES)) {
    if (item.path().extension() != ".epub") continue;
    ++archives;
    std::ifstream file(item.path(), std::ios::binary);
    ASSERT_TRUE(file.good());
    Storage.setFile(path, std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {}));
    ZipFile zip(path);
    std::vector<std::pair<std::string, uint32_t>> directory;
    ASSERT_TRUE(zip.enumerateFileEntries(
        [&](std::string_view name, uint32_t crc, uint32_t) { directory.emplace_back(name, crc); }));
    ASSERT_FALSE(directory.empty());
    for (const auto& [name, crc] : directory) {
      SCOPED_TRACE(item.path().filename().string() + ":" + name);
      CrcSink sink;
      ASSERT_TRUE(zip.readFileToStream(name.c_str(), sink, 1024));
      EXPECT_EQ(~sink.crc, crc);
      ++entries;
    }
  }
  RecordProperty("archives", archives);
  RecordProperty("entries", entries);
  EXPECT_GT(archives, 0u);
  EXPECT_GT(entries, 0u);
}
}  // namespace
