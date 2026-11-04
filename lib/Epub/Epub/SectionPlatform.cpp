#include <HalStorage.h>
#include <Logging.h>
#include <xp_book.h>

#include "Section.h"

namespace {
struct SectionFileContext {
  HalFile& file;
  const std::string& path;
  std::string* text = nullptr;
  static size_t operation(void* raw, uint32_t operation, size_t argument) {
    auto& self = *static_cast<SectionFileContext*>(raw);
    switch (operation) {
      case XP_BOOK_FILE_OPEN_READ:
        return Storage.openFileForRead("SCT", self.path, self.file);
      case XP_BOOK_FILE_SEEK:
        return self.file.seek(argument);
      case XP_BOOK_FILE_SIZE:
        return self.file.size();
      case XP_BOOK_FILE_POSITION:
        return self.file.position();
      case XP_BOOK_FILE_RELEASE_TEXT:
        if (self.text) std::string{}.swap(*self.text);
        return 0;
      default:
        return 0;
    }
  }
  static size_t read(void* raw, uint8_t* bytes, size_t length) {
    return static_cast<SectionFileContext*>(raw)->file.read(bytes, length);
  }
  static void diagnostic(void*, uint32_t event, uint32_t a, uint32_t) {
    switch (event) {
      case XP_BOOK_SECTION_SHORT_RECORD:
        LOG_ERR("SCT", "Short cache record %u", a);
        break;
      case XP_BOOK_SECTION_INVALID_RECORD:
        LOG_ERR("SCT", "Invalid cache record %u", a);
        break;
      case XP_BOOK_SECTION_INVALID_STRING:
        LOG_ERR("SCT", "Invalid cache string length %u", a);
        break;
      case XP_BOOK_SECTION_VISIBLE_SEEK:
        LOG_ERR("SCT", "Failed to seek visible-text LUT");
        break;
      case XP_BOOK_SECTION_VISIBLE_READ:
        LOG_ERR("SCT", "Failed to read visible-text LUT batch");
        break;
    }
  }
  static uint8_t* resize(void* raw, size_t length) {
    auto* text = static_cast<SectionFileContext*>(raw)->text;
    if (!text) return nullptr;
    text->resize(length);
    return reinterpret_cast<uint8_t*>(text->data());
  }
  XpBookFileServices services() { return {this, operation, read, nullptr, resize, diagnostic}; }
};
std::optional<uint32_t> sectionQuery(const std::string& path, const XpBookSectionQuery query, const uint32_t index = 0,
                                     const bool first = false) {
  HalFile file;
  SectionFileContext context{file, path};
  const auto services = context.services();
  uint32_t output[3];
  if (!xp_book_section_query(&services, query, index, first, output)) return std::nullopt;
  return output[0];
}
}  // namespace

Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer&)
    : filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

std::optional<uint16_t> Section::getCachedPageCount() const {
  return sectionQuery(filePath, XP_BOOK_SECTION_PAGE_COUNT);
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile file;
  std::string key;
  SectionFileContext context{file, filePath, &key};
  const auto services = context.services();
  uint32_t page;
  if (!xp_book_section_anchor(&services, reinterpret_cast<const uint8_t*>(anchor.data()), anchor.size(), &page))
    return std::nullopt;
  return static_cast<uint16_t>(page);
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  return sectionQuery(filePath, XP_BOOK_SECTION_PARAGRAPH_PAGE, pIndex);
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  return sectionQuery(filePath, XP_BOOK_SECTION_LIST_PAGE, liIndex);
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset, const bool first) const {
  HalFile file;
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  uint32_t page;
  if (!xp_book_section_visible(&io, offset, first, &page)) return std::nullopt;
  return page;
}
