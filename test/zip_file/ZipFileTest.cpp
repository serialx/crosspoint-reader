#include <HalStorage.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uint16_t METHOD_STORED = 0;
constexpr uint16_t METHOD_DEFLATED = 8;

// "Hello from the deflated entry. " repeated four times, raw deflate (no zlib header).
const std::string DEFLATED_TEXT = [] {
  std::string text;
  for (int i = 0; i < 4; i++) text += "Hello from the deflated entry. ";
  return text;
}();
const std::vector<uint8_t> DEFLATED_BYTES = {0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x48, 0x2b, 0xca, 0xcf, 0x55, 0x28,
                                             0xc9, 0x48, 0x55, 0x48, 0x49, 0x4d, 0xcb, 0x49, 0x2c, 0x49, 0x4d, 0x51,
                                             0x48, 0xcd, 0x2b, 0x29, 0xaa, 0xd4, 0x53, 0xf0, 0xa0, 0xa5, 0x34, 0x00};

void putLe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void putLe32(std::vector<uint8_t>& out, uint32_t value) {
  putLe16(out, static_cast<uint16_t>(value));
  putLe16(out, static_cast<uint16_t>(value >> 16));
}

void putBytes(std::vector<uint8_t>& out, const std::string& bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}

// Minimal zip writer: local headers, central directory, end of central directory.
class ZipBuilder {
 public:
  ZipBuilder& addStored(const std::string& name, const std::string& content, const std::string& extra = "",
                        const std::string& comment = "") {
    entries.push_back({name, std::vector<uint8_t>(content.begin(), content.end()),
                       static_cast<uint32_t>(content.size()), METHOD_STORED, extra, comment, 0});
    return *this;
  }

  ZipBuilder& addDeflated(const std::string& name, const std::vector<uint8_t>& raw, uint32_t uncompressedSize) {
    entries.push_back({name, raw, uncompressedSize, METHOD_DEFLATED, "", "", 0});
    return *this;
  }

  std::vector<uint8_t> build() {
    std::vector<uint8_t> out;
    for (auto& entry : entries) {
      entry.localOffset = static_cast<uint32_t>(out.size());
      putLe32(out, 0x04034b50);
      putLe16(out, 20);  // version needed
      putLe16(out, 0);   // flags
      putLe16(out, entry.method);
      putLe32(out, 0);  // time + date
      putLe32(out, 0);  // crc, not checked by the reader
      putLe32(out, static_cast<uint32_t>(entry.data.size()));
      putLe32(out, entry.uncompressedSize);
      putLe16(out, static_cast<uint16_t>(entry.name.size()));
      putLe16(out, static_cast<uint16_t>(entry.extra.size()));
      putBytes(out, entry.name);
      putBytes(out, entry.extra);
      out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    centralDirOffset = static_cast<uint32_t>(out.size());
    for (const auto& entry : entries) {
      putLe32(out, 0x02014b50);
      putLe16(out, 20);  // version made by
      putLe16(out, 20);  // version needed
      putLe16(out, 0);   // flags
      putLe16(out, entry.method);
      putLe32(out, 0);  // time + date
      putLe32(out, 0);  // crc
      putLe32(out, static_cast<uint32_t>(entry.data.size()));
      putLe32(out, entry.uncompressedSize);
      putLe16(out, static_cast<uint16_t>(entry.name.size()));
      putLe16(out, static_cast<uint16_t>(entry.extra.size()));
      putLe16(out, static_cast<uint16_t>(entry.comment.size()));
      putLe16(out, 0);  // disk number start
      putLe16(out, 0);  // internal attributes
      putLe32(out, 0);  // external attributes
      putLe32(out, entry.localOffset);
      putBytes(out, entry.name);
      putBytes(out, entry.extra);
      putBytes(out, entry.comment);
    }
    const uint32_t centralDirSize = static_cast<uint32_t>(out.size()) - centralDirOffset;
    putLe32(out, 0x06054b50);
    putLe16(out, 0);  // this disk
    putLe16(out, 0);  // central directory disk
    putLe16(out, static_cast<uint16_t>(entries.size()));
    putLe16(out, static_cast<uint16_t>(entries.size()));
    putLe32(out, centralDirSize);
    putLe32(out, centralDirOffset);
    putLe16(out, 0);  // comment length
    return out;
  }

  uint32_t centralDirOffset = 0;

 private:
  struct Entry {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t uncompressedSize;
    uint16_t method;
    std::string extra;
    std::string comment;
    uint32_t localOffset;
  };
  std::vector<Entry> entries;
};

class VectorSink : public Print {
 public:
  size_t write(uint8_t byte) override {
    bytes.push_back(byte);
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    bytes.insert(bytes.end(), data, data + size);
    return size;
  }
  std::string text() const { return std::string(bytes.begin(), bytes.end()); }

 private:
  std::vector<uint8_t> bytes;
};

class ZipFileTest : public ::testing::Test {
 protected:
  // ZipFile keeps a reference to its path, so the fixture owns the string.
  const std::string path = "/books/sample.epub";

  void install(ZipBuilder& builder) {
    std::vector<uint8_t> bytes = builder.build();
    centralDirOffset = builder.centralDirOffset;
    // the end-of-central-directory record is the last 22 bytes
    endOfDirectoryOffset = static_cast<uint32_t>(bytes.size()) - 22;
    Storage.setFile(path, std::move(bytes));
  }

  static size_t sizeOf(ZipFile& zip, const char* name) {
    size_t size = 0;
    EXPECT_TRUE(zip.getInflatedFileSize(name, &size)) << name;
    return size;
  }

  static std::string extract(ZipFile& zip, const char* name) {
    size_t size = 0;
    uint8_t* data = zip.readFileToMemory(name, &size, true);
    if (!data) {
      ADD_FAILURE() << "cannot extract " << name;
      return "";
    }
    std::string text(reinterpret_cast<const char*>(data), size);
    EXPECT_EQ(data[size], '\0');
    free(data);
    return text;
  }

  bool touchedCentralDirectory() const {
    return std::any_of(HalFile::readOffsets.begin(), HalFile::readOffsets.end(),
                       [this](size_t offset) { return offset >= centralDirOffset; });
  }

  uint32_t centralDirOffset = 0;
  uint32_t endOfDirectoryOffset = 0;
};

TEST_F(ZipFileTest, ReportsSizesForStoredAndDeflatedEntries) {
  ZipBuilder builder;
  builder.addStored("mimetype", "application/epub+zip")
      .addDeflated("OEBPS/chapter.xhtml", DEFLATED_BYTES, static_cast<uint32_t>(DEFLATED_TEXT.size()));
  install(builder);

  ZipFile zip(path);
  EXPECT_EQ(sizeOf(zip, "mimetype"), 20u);
  EXPECT_EQ(sizeOf(zip, "OEBPS/chapter.xhtml"), DEFLATED_TEXT.size());
}

TEST_F(ZipFileTest, ExtractsStoredAndDeflatedEntriesToMemory) {
  ZipBuilder builder;
  builder.addStored("mimetype", "application/epub+zip")
      .addDeflated("OEBPS/chapter.xhtml", DEFLATED_BYTES, static_cast<uint32_t>(DEFLATED_TEXT.size()));
  install(builder);

  ZipFile zip(path);
  EXPECT_EQ(extract(zip, "OEBPS/chapter.xhtml"), DEFLATED_TEXT);
  EXPECT_EQ(extract(zip, "mimetype"), "application/epub+zip");
}

TEST_F(ZipFileTest, StreamsEntriesInChunks) {
  ZipBuilder builder;
  builder.addStored("mimetype", "application/epub+zip")
      .addDeflated("OEBPS/chapter.xhtml", DEFLATED_BYTES, static_cast<uint32_t>(DEFLATED_TEXT.size()));
  install(builder);

  ZipFile zip(path);
  VectorSink deflated;
  ASSERT_TRUE(zip.readFileToStream("OEBPS/chapter.xhtml", deflated, 16));
  EXPECT_EQ(deflated.text(), DEFLATED_TEXT);
  VectorSink stored;
  ASSERT_TRUE(zip.readFileToStream("mimetype", stored, 7));
  EXPECT_EQ(stored.text(), "application/epub+zip");
}

TEST_F(ZipFileTest, LookupCacheDoesNotOutliveTheOpenArchive) {
  ZipBuilder before;
  before.addStored("a.txt", "aaaa").addStored("b.txt", "bb");
  install(before);

  ZipFile zip(path);
  EXPECT_EQ(sizeOf(zip, "b.txt"), 2u);

  // replace the archive while the ZipFile lives on; same total length keeps
  // the central directory where the remembered zip details expect it
  ZipBuilder after;
  after.addStored("a.txt", "aa").addStored("b.txt", "bbbb");
  install(after);
  EXPECT_EQ(sizeOf(zip, "b.txt"), 4u);
}

TEST_F(ZipFileTest, ExtractAfterSizeQuerySkipsTheCentralDirectory) {
  ZipBuilder builder;
  builder.addStored("a.txt", "aaaa").addStored("b.txt", "bb").addStored("c.txt", "c");
  install(builder);

  ZipFile zip(path);
  // the lookup is only remembered while the archive stays open
  ASSERT_TRUE(zip.open());
  EXPECT_EQ(sizeOf(zip, "b.txt"), 2u);
  HalFile::readOffsets.clear();
  EXPECT_EQ(extract(zip, "b.txt"), "bb");
  EXPECT_FALSE(touchedCentralDirectory());

  // A different entry is a cache miss and must scan again.
  HalFile::readOffsets.clear();
  EXPECT_EQ(extract(zip, "c.txt"), "c");
  EXPECT_TRUE(touchedCentralDirectory());
}

TEST_F(ZipFileTest, AlternatingLookupsWrapAroundTheDirectoryCursor) {
  ZipBuilder builder;
  builder.addStored("one", "1").addStored("two", "22").addStored("three", "333").addStored("four", "4444");
  install(builder);

  ZipFile zip(path);
  // The cursor only survives while the archive stays open, as it does while
  // an EPUB loads. Each lookup then starts from the previous match, so going
  // backwards forces the scan to run off the end and wrap.
  ASSERT_TRUE(zip.open());
  EXPECT_EQ(sizeOf(zip, "four"), 4u);
  HalFile::readOffsets.clear();
  EXPECT_EQ(sizeOf(zip, "one"), 1u);
  // The scan resumed after "four", hit the end record, and only then wrapped.
  ASSERT_FALSE(HalFile::readOffsets.empty());
  EXPECT_EQ(HalFile::readOffsets.front(), endOfDirectoryOffset);
  EXPECT_EQ(sizeOf(zip, "three"), 3u);
  EXPECT_EQ(sizeOf(zip, "two"), 2u);
  EXPECT_EQ(sizeOf(zip, "four"), 4u);
  EXPECT_EQ(sizeOf(zip, "three"), 3u);
  EXPECT_EQ(sizeOf(zip, "one"), 1u);
}

TEST_F(ZipFileTest, MissingNamesFailWithoutBreakingLaterLookups) {
  ZipBuilder builder;
  builder.addStored("one.txt", "1").addStored("two.txt", "22").addStored("three.txt", "333");
  install(builder);

  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize("missing.txt", &size));
  // Same length, different bytes; and a prefix of a real name.
  EXPECT_FALSE(zip.getInflatedFileSize("ona.txt", &size));
  EXPECT_FALSE(zip.getInflatedFileSize("one.tx", &size));
  EXPECT_EQ(sizeOf(zip, "two.txt"), 2u);
  // From a cursor in the middle of the directory the miss must wrap once and stop.
  EXPECT_FALSE(zip.getInflatedFileSize("missing.txt", &size));
  EXPECT_EQ(sizeOf(zip, "one.txt"), 1u);
  EXPECT_EQ(nullptr, zip.readFileToMemory("missing.txt"));
}

TEST_F(ZipFileTest, SkipsOversizedNamesAndEntriesWithExtraFieldsAndComments) {
  const std::string longName(300, 'n');
  ZipBuilder builder;
  builder.addStored(longName, "long")
      .addStored("with-extra", "extra", std::string(12, 'x'), "a comment")
      .addStored("plain", "plain!");
  install(builder);

  ZipFile zip(path);
  ASSERT_TRUE(zip.open());
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize(longName.c_str(), &size));
  EXPECT_EQ(sizeOf(zip, "plain"), 6u);
  EXPECT_EQ(sizeOf(zip, "with-extra"), 5u);
  EXPECT_EQ(extract(zip, "with-extra"), "extra");
  EXPECT_EQ(extract(zip, "plain"), "plain!");
}

TEST_F(ZipFileTest, PreloadedDirectoryServesLookupsWithoutOpeningTheArchive) {
  ZipBuilder builder;
  builder.addStored("a.txt", "aaaa").addStored("b.txt", "bb");
  install(builder);

  ZipFile zip(path);
  ASSERT_TRUE(zip.loadAllFileStatSlims());
  const int opensAfterPreload = Storage.openCount;
  EXPECT_EQ(sizeOf(zip, "a.txt"), 4u);
  EXPECT_EQ(sizeOf(zip, "b.txt"), 2u);
  EXPECT_EQ(Storage.openCount, opensAfterPreload);
}

TEST_F(ZipFileTest, RejectsArchivesWithoutAnEndOfCentralDirectoryRecord) {
  Storage.setFile(path, std::vector<uint8_t>(64, 0));
  ZipFile zip(path);
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize("anything", &size));

  Storage.setFile(path, std::vector<uint8_t>(8, 'P'));
  ZipFile tiny(path);
  EXPECT_FALSE(tiny.getInflatedFileSize("anything", &size));
}

}  // namespace

namespace {
std::string hexName(std::string_view name) {
  std::ostringstream out;
  for (unsigned char c : name) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
  return name.empty() ? "-" : out.str();
}

std::string directoryOracle(const std::vector<uint8_t>& bytes) {
  const std::string path = "/oracle.epub";
  Storage.setFile(path, bytes);
  ZipFile zip(path);
  std::ostringstream out;
  zip.open();
  out << "details " << zip.loadZipDetails() << ' ' << zip.zipDetails.centralDirOffset << ' '
      << zip.zipDetails.totalEntries << '\n';
  std::vector<std::string> names;
  const bool entries = zip.enumerateFileEntries([&](std::string_view name, uint32_t crc, uint32_t compressed) {
    names.emplace_back(name);
    out << "entry " << hexName(name) << ' ' << crc << ' ' << compressed << '\n';
  });
  out << "entries " << entries << '\n';
  out << "paths";
  const bool paths = zip.enumerateFilePaths([&](std::string_view name) { out << ' ' << hexName(name); });
  out << ' ' << paths << '\n';
  // Repetition checks the last-lookup cache; reverse order forces cursor wraps.
  names.push_back("missing-entry");
  names.push_back("./mimetype");
  names.push_back("/mimetype");
  names.push_back(std::string(256, 'n'));
  for (int pass = 0; pass < 2; ++pass) {
    if (pass) std::reverse(names.begin(), names.end());
    for (const auto& name : names) {
      ZipFile::FileStatSlim stat{};
      const bool found = zip.loadFileStatSlim(name.c_str(), &stat);
      size_t size = 0;
      const bool sized = zip.getInflatedFileSize(name.c_str(), &size);
      out << "lookup " << found << ' ' << sized << ' ' << size << ' ' << stat.method << ' ' << stat.compressedSize
          << ' ' << stat.uncompressedSize << ' ' << stat.localHeaderOffset << ' '
          << (found ? zip.getDataOffset(stat) : -1) << ' ' << zip.lastCentralDirPos << '\n';
    }
  }
  std::deque<ZipFile::SizeTarget> targets;
  std::deque<uint32_t> sizes(names.size() + 1, 0xffffffff);
  for (size_t i = 0; i < names.size(); ++i) {
    targets.push_back({ZipFile::fnvHash64(names[i].data(), names[i].size()), uint16_t(names[i].size()), uint16_t(i)});
  }
  // A duplicate and an out-of-range index preserve the batch's counting policy.
  if (!targets.empty()) {
    auto duplicate = targets.front();
    duplicate.index = uint16_t(names.size());
    targets.push_back(duplicate);
    duplicate.index = 65535;
    targets.push_back(duplicate);
  }
  std::sort(targets.begin(), targets.end(),
            [](const auto& a, const auto& b) { return a.hash < b.hash || (a.hash == b.hash && a.len < b.len); });
  out << "batch " << zip.fillUncompressedSizes(targets, sizes);
  for (auto size : sizes) out << ' ' << size;
  out << '\n';
  out << "preload " << zip.loadAllFileStatSlims() << '\n';
  out << "cached-paths";
  zip.enumerateFilePaths([&](std::string_view name) { out << ' ' << hexName(name); });
  out << '\n';
  zip.close();
  for (const auto& name : names) {
    ZipFile::FileStatSlim stat{};
    const bool found = zip.loadFileStatSlim(name.c_str(), &stat);
    out << "cached " << found << ' ' << stat.method << ' ' << stat.compressedSize << ' ' << stat.uncompressedSize << ' '
        << stat.localHeaderOffset << '\n';
  }
  return out.str();
}

TEST(ZipDirectoryOracle, CurrentCppCorpus) {
  std::vector<std::pair<std::string, std::vector<uint8_t>>> cases;
  std::vector<std::filesystem::path> epubs;
  for (const auto& item : std::filesystem::directory_iterator(ZIP_EPUB_DIR)) {
    if (item.path().extension() == ".epub") epubs.push_back(item.path());
  }
  std::sort(epubs.begin(), epubs.end());
  for (const auto& path : epubs) {
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.good());
    cases.emplace_back(path.filename().string(), std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {}));
  }
  ZipBuilder basic;
  basic.addStored("mimetype", "application/epub+zip")
      .addDeflated("OEBPS/chapter.xhtml", DEFLATED_BYTES, uint32_t(DEFLATED_TEXT.size()))
      .addStored("a.txt", "aaaa")
      .addStored("b.txt", "bb")
      .addStored("c.txt", "c")
      .addStored("one", "1")
      .addStored("two", "22")
      .addStored("three", "333")
      .addStored("four", "4444")
      .addStored(std::string(300, 'n'), "long")
      .addStored(std::string(255, 'n'), "boundary")
      .addStored(std::string(256, 'n'), "too-long")
      .addStored("with-extra", "extra", std::string(12, 'x'), "a comment")
      .addStored("plain", "plain!")
      .addStored("./raw/../path", "raw")
      .addStored("duplicate", "first")
      .addStored("duplicate", "second-longer");
  cases.emplace_back("generated", basic.build());
  for (size_t comment : {size_t(1002), size_t(1003), size_t(65535)}) {
    auto bytes = basic.build();
    bytes[bytes.size() - 2] = uint8_t(comment);
    bytes[bytes.size() - 1] = uint8_t(comment >> 8);
    bytes.resize(bytes.size() + comment, 'x');
    cases.emplace_back("comment-" + std::to_string(comment), std::move(bytes));
  }
  ZipBuilder large;
  for (int i = 0; i < 1100; ++i) large.addStored("directory/entry-" + std::to_string(i), "x", "extra", "comment");
  auto largeBytes = large.build();
  ASSERT_GT(largeBytes.size() - large.centralDirOffset - 22, 65536u);
  cases.emplace_back("large-directory", std::move(largeBytes));
  cases.emplace_back("missing-eocd", std::vector<uint8_t>(64, 0));
  cases.emplace_back("too-small", std::vector<uint8_t>(8, 'P'));
  std::ostringstream actual;
  for (const auto& [name, bytes] : cases) actual << "archive " << name << '\n' << directoryOracle(bytes);
  const std::string golden = std::string(ZIP_GOLDEN_DIR) + "/directory-golden.txt";
  if (std::getenv("RECORD_ZIP_GOLDENS")) {
    std::ofstream output(golden, std::ios::binary);
    ASSERT_TRUE(output.good());
    output << actual.str();
  } else {
    std::ifstream input(golden, std::ios::binary);
    ASSERT_TRUE(input.good());
    const std::string expected(std::istreambuf_iterator<char>(input), {});
    EXPECT_EQ(expected, actual.str());
  }
}
}  // namespace
