// Actual Section cache reads; Page ownership is the existing physical fake.
#include <xp_ffi.h>
struct ChildSectionDecoder { const uint8_t* input; size_t len, position; uint32_t kind; };
extern "C" uint32_t xp_sec_decode_next(ChildSectionDecoder*, XpSecRecord*);

namespace {
uint16_t readerSectionPages = 10;
XpSecRecord sectionRecord(const std::vector<uint8_t>& bytes, uint32_t kind, size_t offset) {
  ChildSectionDecoder decoder{bytes.data(), bytes.size(), offset, kind};
  XpSecRecord record{};
  assert(xp_sec_decode_next(&decoder, &record) == XP_SEC_OK);
  return record;
}
std::vector<uint8_t> childSectionBytes() {
  std::vector<uint8_t> bytes;
  const uint32_t header[] = {68, 1, 0x3f800000, 0, 0, 0, 528, 792, 0, 0, 0, 0, 0, 100,
                             readerSectionPages, 0, 0, 0, 0, 0};
  const unsigned widths[] = {1, 4, 4, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 4, 4, 4, 4, 4};
  for (unsigned i = 0; i < 20; ++i) number(bytes, header[i], widths[i]);
  assert(bytes.size() == 44);
  std::vector<uint32_t> positions;
  for (unsigned i = 0; i < readerSectionPages; ++i) {
    positions.push_back(uint32_t(bytes.size()));
    // A valid empty Page body; its UI/word observations are physical fakes.
    number(bytes, 0, 2); number(bytes, 0, 2); number(bytes, 0, 2);
  }
  uint32_t offsets[5];
  offsets[0] = uint32_t(bytes.size());
  for (auto value : positions) number(bytes, value, 4);
  offsets[1] = uint32_t(bytes.size());
  number(bytes, 1, 2); stringBytes(bytes, "anchor"); number(bytes, 2, 2);
  offsets[2] = uint32_t(bytes.size());
  number(bytes, readerSectionPages, 2);
  for (unsigned i = 0; i < readerSectionPages; ++i) number(bytes, 7, 2);
  offsets[3] = uint32_t(bytes.size());
  for (unsigned i = 0; i < readerSectionPages; ++i) number(bytes, 0, 2);
  offsets[4] = uint32_t(bytes.size());
  for (unsigned i = 0; i < readerSectionPages; ++i) number(bytes, i * 100, 4);
  for (unsigned i = 0; i < 5; ++i)
    for (unsigned j = 0; j < 4; ++j) bytes[24 + i * 4 + j] = uint8_t(offsets[i] >> (j * 8));
  const auto decoded = sectionRecord(bytes, XP_SEC_HEADER, 0);
  for (unsigned i = 0; i < 15; ++i) assert(decoded.values[i] == header[i]);
  for (unsigned i = 0; i < 5; ++i) assert(decoded.values[15 + i] == offsets[i]);
  for (unsigned i = 0; i < readerSectionPages; ++i) {
    assert(sectionRecord(bytes, XP_SEC_U32, offsets[0] + 4 * i).values[0] == positions[i]);
    assert(sectionRecord(bytes, XP_SEC_U16, positions[i]).values[0] == 0);
    assert(sectionRecord(bytes, XP_SEC_FOOTNOTE_COUNT, positions[i] + 2).values[0] == 0);
    assert(sectionRecord(bytes, XP_SEC_LINK_COUNT, positions[i] + 4).values[0] == 0);
    assert(sectionRecord(bytes, XP_SEC_U16, offsets[2] + 2 + 2 * i).values[0] == 7);
    assert(sectionRecord(bytes, XP_SEC_U16, offsets[3] + 2 * i).values[0] == 0);
    assert(sectionRecord(bytes, XP_SEC_U32, offsets[4] + 4 * i).values[0] == i * 100);
  }
  assert(sectionRecord(bytes, XP_SEC_U16, offsets[1]).values[0] == 1);
  assert(sectionRecord(bytes, XP_SEC_U32, offsets[1] + 2).values[0] == 6);
  assert(sectionRecord(bytes, XP_SEC_U16, offsets[1] + 12).values[0] == 2);
  assert(sectionRecord(bytes, XP_SEC_U16, offsets[2]).values[0] == readerSectionPages);
  assert(std::memcmp(bytes.data() + offsets[1] + 6, "anchor", 6) == 0);
  return bytes;
}
void setReaderSectionPages(uint16_t pages) {
  assert(readerFiles.empty() && pages >= 3 && pages <= 10);
  readerSectionPages = pages;
  readerSectionFiles.clear();
  const auto bytes = childSectionBytes();
  for (unsigned i = 0; i < 7; ++i)
    readerSectionFiles.emplace(readerCache + "/sections/" + std::to_string(i) + ".bin", bytes);
  std::printf("SECTION_CACHE_BYTES %u ", pages);
  for (auto byte : bytes) std::printf("%02x", byte);
  std::printf("\n");
  std::printf("SECTION_CACHE_CODEC pages=%u bytes=%zu paths=%zu\n", pages, bytes.size(), readerSectionFiles.size());
}
bool hasReaderSection() {
  auto* owner = xp_app_test_current_epub();
  if (!owner) return false;
  XpEpubTestChildObservation observation{};
  xp_epub_test_child_observe(owner, &observation);
  return observation.has_section;
}
void readerSectionBalanced() {
  assert(readerFiles.empty() && readerBuffers.empty() && !readerCacheLocked);
  assert(readerSectionOpens == readerSectionCloses);
  assert(!xp_app_test_current_epub() && !xp_app_test_parent_count());
  std::printf("SECTION_FILES opens=%u closes=%u\n", readerSectionOpens, readerSectionCloses);
}
void bindReaderSections() {
  setReaderSectionPages(10);
  static auto pathHash = leaves.native_path_hash;
  leaves.native_path_hash = [](XpAppText value) -> uint64_t {
    if (text(value) == "anchor") return std::hash<std::string>{}("anchor");
    return pathHash(value);
  };
  legacyBook.section.page.page_load = [](void* raw, uint32_t size, uint32_t visible) -> void* {
    physical("page-open", 1);
    auto& file = readerFiles.at(raw);
    assert(readerSectionFiles.count(file.path));
    const auto header = sectionRecord(file.bytes, XP_SEC_HEADER, 0);
    unsigned page = 0;
    for (; page < header.values[14]; ++page) {
      const auto start = sectionRecord(file.bytes, XP_SEC_U32, header.values[15] + page * 4).values[0];
      if (start != file.cursor) continue;
      const auto end = page + 1 == header.values[14] ? header.values[15] :
          sectionRecord(file.bytes, XP_SEC_U32, header.values[15] + (page + 1) * 4).values[0];
      assert(end > start && size == end - start && end <= file.bytes.size());
      assert(visible == sectionRecord(file.bytes, XP_SEC_U32, header.values[19] + page * 4).values[0]);
      std::fprintf(stderr, "section-page path=%s index=%u start=%u end=%u visible=%u\n",
                   file.path.c_str(), page, start, end, visible);
      file.cursor = end; break;
    }
    assert(page < header.values[14]);
    auto* value = new ResultPage{int(page)};
    assert(results.pages.insert(value).second); return value;
  };
  legacyBook.section.page.page_info = [](void* raw, XpEpubPageInfo* out) {
    physical("page-info", 1); assert(results.pages.count(static_cast<ResultPage*>(raw))); *out = {}; out->elements = 1;
  };
  legacyBook.section.page.page_element = [](void* raw, uint16_t index, XpEpubElement* out) {
    physical("page-element", 1); assert(results.pages.count(static_cast<ResultPage*>(raw)) && index == 0);
    *out = {}; out->tag = 1; out->valid = 1; out->words = 2;
  };
  legacyBook.section.page.page_word = [](void* raw, uint16_t element, uint16_t word, XpEpubWord* out) {
    physical("page-word", 1); assert(results.pages.count(static_cast<ResultPage*>(raw)) && element == 0 && word < 2);
    *out = {}; out->text = literal(word ? "words" : "page");
  };
}
}
