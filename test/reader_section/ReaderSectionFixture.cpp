#include "ReaderSectionFixture.h"

#include <GfxRenderer.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

#include "../reader_page_owner/TransferredPage.h"

extern "C" {
void* xp_epub_test_section_fixture_open(const XpAppResources*, const XpEpubLeaves*, const XpAppRuntime*, XpAppText,
                                        XpAppText, int32_t);
void xp_epub_test_section_fixture_close(void*);
uint8_t xp_epub_test_section_fixture_load(void*, const XpEpubRenderSpec*);
uint8_t xp_epub_test_section_fixture_create(void*, const XpEpubRenderSpec*, XpEpubBuildPopup, void*);
uint8_t xp_epub_test_section_fixture_start(void*, const XpEpubRenderSpec*, XpEpubBuildPopup, void*);
uint8_t xp_epub_test_section_fixture_advance(void*, int32_t, uint8_t);
void xp_epub_test_section_fixture_suspend(void*);
void xp_epub_test_section_fixture_abandon(void*);
uint8_t xp_epub_test_section_fixture_clear(void*);
void xp_epub_test_section_fixture_facts(void*, uint32_t (*)[4]);
int32_t xp_epub_test_section_fixture_estimate(void*);
uint8_t xp_epub_test_section_fixture_html(void*);

uint8_t xp_epub_test_section_fixture_query(void*, uint8_t, uint32_t, uint8_t, uint32_t*);
uint8_t xp_epub_test_section_fixture_anchor(void*, XpAppText, uint8_t, uint16_t*);
int32_t xp_epub_test_section_fixture_toc(void*, int32_t);
uint8_t xp_epub_test_section_fixture_text(void*, int32_t, XpAppTextSink, void*);
}
const XpAppResources& sectionFixtureResources();
const XpEpubLeaves& sectionFixtureLeaves(GfxRenderer&);
const XpAppRuntime& sectionFixtureRuntime();
void sectionFixtureYield(const std::function<bool()>*);
namespace {
XpAppText wire(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
using Bytes = std::vector<uint8_t>;
void number(Bytes& out, uint32_t value, unsigned width = 4) {
  for (unsigned i = 0; i < width; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void string(Bytes& out, const std::string& value) {
  number(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}
Bytes read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
void write(const char* path, const Bytes& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  assert(out);
}
uint32_t crc(const Bytes& bytes) {
  uint32_t value = ~0u;
  for (auto byte : bytes) {
    value ^= byte;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
  }
  return ~value;
}
void prepare(const ReaderSectionInput& input) {
  std::filesystem::create_directories("cache");
  Bytes zip, directory;
  uint16_t entries = 0;
  if (std::filesystem::exists("book")) {
    for (const auto& file : std::filesystem::recursive_directory_iterator("book")) {
      if (!file.is_regular_file()) continue;
      const auto name = std::filesystem::relative(file.path(), "book").generic_string();
      const auto bytes = read(file.path());
      const auto checksum = crc(bytes), offset = static_cast<uint32_t>(zip.size());
      number(zip, 0x04034b50);
      number(zip, 20, 2);
      number(zip, 0, 2);
      number(zip, 0, 2);
      number(zip, 0);
      number(zip, checksum);
      number(zip, bytes.size());
      number(zip, bytes.size());
      number(zip, name.size(), 2);
      number(zip, 0, 2);
      zip.insert(zip.end(), name.begin(), name.end());
      zip.insert(zip.end(), bytes.begin(), bytes.end());
      number(directory, 0x02014b50);
      number(directory, 20, 2);
      number(directory, 20, 2);
      number(directory, 0, 2);
      number(directory, 0, 2);
      number(directory, 0);
      number(directory, checksum);
      number(directory, bytes.size());
      number(directory, bytes.size());
      number(directory, name.size(), 2);
      number(directory, 0, 2);
      number(directory, 0, 2);
      number(directory, 0, 2);
      number(directory, 0, 2);
      number(directory, 0);
      number(directory, offset);
      directory.insert(directory.end(), name.begin(), name.end());
      ++entries;
    }
  }
  const auto offset = zip.size();
  zip.insert(zip.end(), directory.begin(), directory.end());
  number(zip, 0x06054b50);
  number(zip, 0, 2);
  number(zip, 0, 2);
  number(zip, entries, 2);
  number(zip, entries, 2);
  number(zip, directory.size());
  number(zip, offset);
  number(zip, 0, 2);
  write(".reader-fixture.epub", zip);
  auto spine = input.spine;
  if (spine.empty()) spine.push_back({"empty.xhtml"});
  Bytes metadata;
  string(metadata, "");
  string(metadata, "");
  string(metadata, input.language);
  string(metadata, "");
  string(metadata, spine[0].href);
  std::vector<Bytes> rows;
  uint32_t cumulative = 0;
  for (size_t index = 0; index < spine.size(); ++index) {
    Bytes row;
    string(row, spine[index].href);
    cumulative += read(std::filesystem::path("book") / spine[index].href).size();
    number(row, cumulative);
    int toc = -1;
    for (size_t i = 0; i < input.toc.size(); ++i)
      if (input.toc[i].spineIndex == static_cast<int>(index)) {
        toc = i;
        break;
      }
    number(row, static_cast<uint16_t>(toc), 2);
    rows.push_back(std::move(row));
  }
  for (const auto& entry : input.toc) {
    Bytes row;
    string(row, "");
    string(row, entry.spineIndex >= 0 && size_t(entry.spineIndex) < spine.size() ? spine[entry.spineIndex].href : "");
    string(row, entry.anchor);
    number(row, 1, 1);
    number(row, static_cast<uint16_t>(entry.spineIndex), 2);
    rows.push_back(std::move(row));
  }
  const auto table = 9 + metadata.size();
  Bytes book;
  number(book, 10, 1);
  number(book, table);
  number(book, spine.size(), 2);
  number(book, input.toc.size(), 2);
  book.insert(book.end(), metadata.begin(), metadata.end());
  uint32_t position = table + rows.size() * 4;
  for (const auto& row : rows) {
    number(book, position);
    position += row.size();
  }
  for (const auto& row : rows) book.insert(book.end(), row.begin(), row.end());
  write("cache/book.bin", book);
  write(".reader-fixture.book", book);
}
}  // namespace
void* ReaderSectionFixture::open(const ReaderSectionInput& input, int spine, GfxRenderer& renderer) {
  prepare(input);
  return xp_epub_test_section_fixture_open(&sectionFixtureResources(), &sectionFixtureLeaves(renderer),
                                           &sectionFixtureRuntime(), wire(".reader-fixture.epub"), wire("cache"),
                                           spine);
}
ReaderSectionFixture::ReaderSectionFixture(void* handle) : owner(handle) {
  assert(owner);
  refresh();
}
ReaderSectionFixture::ReaderSectionFixture(const std::shared_ptr<ReaderSectionInput>& input, int spine,
                                           GfxRenderer& renderer)
    : ReaderSectionFixture(open(*input, spine, renderer)) {}
std::unique_ptr<ReaderSectionFixture> ReaderSectionFixture::tryOpen(const ReaderSectionInput& input, int spine,
                                                                    GfxRenderer& renderer) {
  auto* handle = open(input, spine, renderer);
  if (!handle) return {};
  return std::unique_ptr<ReaderSectionFixture>(new ReaderSectionFixture(handle));
}
ReaderSectionFixture::~ReaderSectionFixture() { xp_epub_test_section_fixture_close(owner); }
void ReaderSectionFixture::refresh() {
  uint32_t facts[4];
  xp_epub_test_section_fixture_facts(owner, &facts);
  pageCount = facts[0];
}
XpEpubRenderSpec ReaderSectionFixture::spec(const ReaderRenderSpec& s) {
  return {s.fontId,
          s.lineCompression,
          s.viewportWidth,
          s.viewportHeight,
          s.extraParagraphSpacing,
          s.paragraphIndentSpaces,
          s.characterSpacing,
          s.wordSpacingPercent,
          s.paragraphAlignment,
          s.hyphenationEnabled,
          s.embeddedStyle,
          s.imageRendering,
          s.focusReadingEnabled};
}
void ReaderSectionFixture::showPopup(void* context) { static_cast<ReaderSectionFixture*>(context)->popup(); }
bool ReaderSectionFixture::loadSectionFile(const ReaderRenderSpec& s) {
  const auto v = spec(s);
  const bool result = xp_epub_test_section_fixture_load(owner, &v);
  refresh();
  return result;
}
bool ReaderSectionFixture::createSectionFile(const ReaderRenderSpec& s, const std::function<void()>& callback) {
  popup = callback;
  const auto v = spec(s);
  const bool result = xp_epub_test_section_fixture_create(owner, &v, popup ? showPopup : nullptr, this);
  refresh();
  return result;
}
bool ReaderSectionFixture::startBuild(const ReaderRenderSpec& s, const std::function<void()>& callback) {
  popup = callback;
  const auto v = spec(s);
  const bool result = xp_epub_test_section_fixture_start(owner, &v, popup ? showPopup : nullptr, this);
  refresh();
  return result;
}
bool ReaderSectionFixture::buildSomeMore(int pages, const std::function<bool()>& yielding) {
  sectionFixtureYield(&yielding);
  const bool result = xp_epub_test_section_fixture_advance(owner, pages, bool(yielding));
  sectionFixtureYield(nullptr);
  refresh();
  return result;
}
void ReaderSectionFixture::suspendBuild() {
  xp_epub_test_section_fixture_suspend(owner);
  refresh();
}
void ReaderSectionFixture::abandonBuild() {
  xp_epub_test_section_fixture_abandon(owner);
  refresh();
}
bool ReaderSectionFixture::clearCache() { return xp_epub_test_section_fixture_clear(owner); }
bool ReaderSectionFixture::hasHtmlCache() const { return xp_epub_test_section_fixture_html(owner); }
bool ReaderSectionFixture::isBuilding() const {
  uint32_t v[4];
  xp_epub_test_section_fixture_facts(owner, &v);
  return v[1];
}
bool ReaderSectionFixture::isBuildComplete() const {
  uint32_t v[4];
  xp_epub_test_section_fixture_facts(owner, &v);
  return v[2];
}
bool ReaderSectionFixture::isPartial() const {
  uint32_t v[4];
  xp_epub_test_section_fixture_facts(owner, &v);
  return v[3];
}
uint16_t ReaderSectionFixture::estimatedTotalPages() const { return xp_epub_test_section_fixture_estimate(owner); }
std::unique_ptr<Page> ReaderSectionFixture::loadPage(int page) const { return fixtureSectionPage(owner, page); }
std::optional<uint32_t> ReaderSectionFixture::query(uint8_t kind, uint32_t value, bool first) const {
  uint32_t result;
  if (!xp_epub_test_section_fixture_query(owner, kind, value, first, &result)) return {};
  return result;
}
std::optional<uint16_t> ReaderSectionFixture::getCachedPageCount() const { return query(0); }
std::optional<uint16_t> ReaderSectionFixture::getPageForParagraphIndex(uint16_t index) const { return query(1, index); }
std::optional<uint16_t> ReaderSectionFixture::getPageForListItemIndex(uint16_t index) const { return query(2, index); }
std::optional<uint16_t> ReaderSectionFixture::getParagraphIndexForPage(uint16_t index) const { return query(3, index); }
std::optional<uint32_t> ReaderSectionFixture::getVisibleTextOffsetForPage(uint16_t index) const {
  return query(4, index);
}
std::optional<uint16_t> ReaderSectionFixture::getPageForVisibleTextOffset(uint32_t offset, bool first) const {
  return query(5, offset, first);
}
bool ReaderSectionFixture::buildReachedVisibleTextOffset(uint32_t offset) const {
  return query(6, offset).value() != 0;
}
std::optional<uint16_t> ReaderSectionFixture::anchor(const std::string& text, uint8_t kind) const {
  uint16_t result;
  if (!xp_epub_test_section_fixture_anchor(owner, wire(text), kind, &result)) return {};
  return result;
}
std::optional<uint16_t> ReaderSectionFixture::findAnchor(const std::string& text) const { return anchor(text, 0); }
std::optional<uint16_t> ReaderSectionFixture::getPageForAnchor(const std::string& text) const {
  return anchor(text, 1);
}
std::optional<uint16_t> ReaderSectionFixture::findAnchorDuringBuild(const std::string& text) const {
  return anchor(text, 2);
}
int ReaderSectionFixture::getTocIndexForPage(int page) const { return xp_epub_test_section_fixture_toc(owner, page); }
std::string ReaderSectionFixture::getTextFromSectionFile() const {
  std::string out;
  xp_epub_test_section_fixture_text(
      owner, currentPage,
      [](void* raw, XpAppText text) -> uint8_t {
        static_cast<std::string*>(raw)->assign(reinterpret_cast<const char*>(text.data), text.len);
        return 1;
      },
      &out);
  return out;
}
