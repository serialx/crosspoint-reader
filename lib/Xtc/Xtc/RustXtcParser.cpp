#include "RustXtcParser.h"

#include <Logging.h>

#include <cstring>
#include <memory>

namespace xtc {
namespace {
enum class Diagnostic : uint32_t {
  HeaderMagic = 1,
  HeaderVersion,
  Header,
  Title,
  Author,
  TableZero,
  TableBounds,
  TableSeek,
  FirstEntry,
  TableValidated,
  HeaderRead,
  TitleRead,
  AuthorRead,
  FirstPageRead,
  Opened,
  Reopen,
  EntrySeek,
  EntryRead,
  Chapters,
  LazyChapters,
  PageSeek,
  PageHeader,
  PageMagic,
  PagePayload,
  BufferSmall,
  PageRead
};
}  // namespace

const XpXtcLeaves RustXtcParser::leaves{
    [](void* p, const uint8_t* path) {
      return Storage.openFileForRead("XTC", reinterpret_cast<const char*>(path), static_cast<RustXtcParser*>(p)->file);
    },
    [](void* p) { return static_cast<RustXtcParser*>(p)->file.isOpen(); },
    [](void* p) { static_cast<RustXtcParser*>(p)->file.close(); },
    [](void* p, uint8_t* bytes, size_t size) -> int32_t {
      return static_cast<RustXtcParser*>(p)->file.read(bytes, size);
    },
    [](void* p, uint64_t offset) { return static_cast<RustXtcParser*>(p)->file.seek64(offset); },
    [](void* p) -> uint64_t { return static_cast<RustXtcParser*>(p)->file.fileSize64(); },
    [](size_t bytes, bool optionalShrink) -> uint8_t* {
      if (!optionalShrink) return std::allocator<uint8_t>{}.allocate(bytes);
#if __cpp_exceptions
      try {
        return std::allocator<uint8_t>{}.allocate(bytes);
      } catch (...) {
        return nullptr;
      }
#else
      return nullptr;
#endif
    },
    [](uint8_t* bytes, size_t size) { std::allocator<uint8_t>{}.deallocate(bytes, size); },
    [](void* p, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t) {
      static_cast<RustXtcParser*>(p)->diagnostic(event, a, b, c);
    }};

RustXtcParser::RustXtcParser() { xp_xtc_owner_init(&owner, this, &leaves); }
RustXtcParser::~RustXtcParser() { xp_xtc_owner_drop(&owner); }
XpXtcView RustXtcParser::view(bool chapters) const {
  XpXtcView result;
  xp_xtc_owner_view(const_cast<XpXtcOwnerStorage*>(&owner), chapters, &result);
  return result;
}
XtcError RustXtcParser::open(const char* path) {
  const auto result =
      static_cast<XtcError>(xp_xtc_owner_open(&owner, reinterpret_cast<const uint8_t*>(path), strlen(path)));
  if (result == XtcError::OK) {
    [[maybe_unused]] const auto value = view();
    LOG_DBG("XTC", "Opened file: %s (%u pages, %dx%d)", path, value.state->header.pageCount, value.state->width,
            value.state->height);
  }
  return result;
}
void RustXtcParser::close() { xp_xtc_owner_close(&owner); }
std::string_view RustXtcParser::getTitle() const {
  const auto value = view();
  return {reinterpret_cast<const char*>(value.title), value.title_len};
}
std::string_view RustXtcParser::getAuthor() const {
  const auto value = view();
  return {reinterpret_cast<const char*>(value.author), value.author_len};
}
std::span<const XpXtcChapterRow> RustXtcParser::getChapters() {
  const auto value = view(true);
  return {value.chapters, value.chapter_count};
}
bool RustXtcParser::getPageInfo(uint32_t index, PageInfo& info) {
  XpXtcEntry entry;
  if (!xp_xtc_owner_page_info(&owner, index, &entry)) return false;
  info.offset = entry.offset;
  info.size = entry.size;
  info.width = entry.width;
  info.height = entry.height;
  info.bitDepth = getBitDepth();
  return true;
}
size_t RustXtcParser::loadPage(uint32_t index, uint8_t* bytes, size_t capacity) {
  return xp_xtc_owner_load(&owner, index, bytes, capacity);
}
XtcError RustXtcParser::loadPageStreaming(uint32_t index, std::function<void(const uint8_t*, size_t, size_t)> callback,
                                          size_t chunkSize) {
  const XpXtcStream stream{&callback, [](void* p, const uint8_t* bytes, size_t size, size_t offset) {
                             (*static_cast<decltype(callback)*>(p))(bytes, size, offset);
                           }};
  return static_cast<XtcError>(xp_xtc_owner_stream(&owner, index, chunkSize, &stream));
}
bool RustXtcParser::isValidXtcFile(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("XTC", path, file)) return false;
  uint8_t magic[4];
  const size_t count = file.read(magic, sizeof(magic));
  file.close();
  return xp_xtc_bit_depth(magic, count) != 0;
}

void RustXtcParser::diagnostic(uint32_t event, [[maybe_unused]] uint32_t a, uint32_t b, uint32_t c) {
  [[maybe_unused]] const auto value = view();
  [[maybe_unused]] const auto& h = value.state->header;
  [[maybe_unused]] const uint64_t offset = uint64_t{b} | (uint64_t{c} << 32);
  switch (static_cast<Diagnostic>(event)) {
    case Diagnostic::HeaderMagic:
      LOG_DBG("XTC", "Invalid magic: 0x%08X (expected 0x%08X or 0x%08X)", h.magic, XTC_MAGIC, XTCH_MAGIC);
      break;
    case Diagnostic::HeaderVersion:
      LOG_DBG("XTC", "Unsupported version: %u.%u", h.versionMajor, h.versionMinor);
      break;
    case Diagnostic::Header:
      LOG_DBG("XTC", "Header: magic=0x%08X (%s), ver=%u.%u, pages=%u, bitDepth=%u", h.magic,
              h.magic == XTCH_MAGIC ? "XTCH" : "XTC", h.versionMajor, h.versionMinor, h.pageCount, value.state->depth);
      break;
    case Diagnostic::Title:
      LOG_DBG("XTC", "Title: %s", reinterpret_cast<const char*>(value.title));
      break;
    case Diagnostic::Author:
      LOG_DBG("XTC", "Author: %s", reinterpret_cast<const char*>(value.author));
      break;
    case Diagnostic::TableZero:
      LOG_DBG("XTC", "Page table offset is 0, cannot read");
      break;
    case Diagnostic::TableBounds:
      LOG_DBG("XTC",
              "Page table exceeds file bounds: file=%llu tableOffset=%llu tableSize=%llu pages=%u entrySize=%u "
              "dataOffset=%llu minTableOffset=%llu",
              static_cast<unsigned long long>(uint64_t{a} | (uint64_t{b} << 32)),
              static_cast<unsigned long long>(h.pageTableOffset),
              static_cast<unsigned long long>(h.pageCount) * sizeof(PageTableEntry), h.pageCount,
              static_cast<unsigned int>(sizeof(PageTableEntry)), static_cast<unsigned long long>(h.dataOffset),
              static_cast<unsigned long long>(XTC_LEGACY_HEADER_SIZE));
      break;
    case Diagnostic::TableSeek:
      LOG_DBG("XTC", "Failed to seek to page table at %llu", static_cast<unsigned long long>(h.pageTableOffset));
      break;
    case Diagnostic::FirstEntry:
      LOG_DBG("XTC", "Failed to read first page table entry");
      break;
    case Diagnostic::TableValidated:
      LOG_DBG("XTC", "Page table validated: %u pages, default %dx%d", h.pageCount, value.state->width,
              value.state->height);
      break;
    case Diagnostic::HeaderRead:
      LOG_DBG("XTC", "Failed to read header: %s", errorToString(static_cast<XtcError>(a)));
      break;
    case Diagnostic::TitleRead:
      LOG_DBG("XTC", "Failed to read title: %s", errorToString(static_cast<XtcError>(a)));
      break;
    case Diagnostic::AuthorRead:
      LOG_DBG("XTC", "Failed to read author: %s", errorToString(static_cast<XtcError>(a)));
      break;
    case Diagnostic::FirstPageRead:
      LOG_DBG("XTC", "Failed to read first page info: %s", errorToString(static_cast<XtcError>(a)));
      break;
    case Diagnostic::Opened:
      // Emitted by open() while its original caller path is still borrowed.
      break;
    case Diagnostic::Reopen:
      LOG_DBG("XTC", "Failed to reopen file for page table read");
      break;
    case Diagnostic::EntrySeek:
      LOG_DBG("XTC", "Failed to seek to page table entry %lu at %llu", static_cast<unsigned long>(a),
              static_cast<unsigned long long>(offset));
      break;
    case Diagnostic::EntryRead:
      LOG_DBG("XTC", "Failed to read page table entry %lu", static_cast<unsigned long>(a));
      break;
    case Diagnostic::Chapters:
      LOG_DBG("XTC", "Chapters: %u", a);
      break;
    case Diagnostic::LazyChapters:
      LOG_ERR("XTC", "Failed to lazy-load chapters: %s", errorToString(static_cast<XtcError>(a)));
      break;
    case Diagnostic::PageSeek:
      LOG_DBG("XTC", "Failed to seek to page %u at offset %llu", a, static_cast<unsigned long long>(offset));
      break;
    case Diagnostic::PageHeader:
      LOG_DBG("XTC", "Failed to read page header for page %u", a);
      break;
    case Diagnostic::PageMagic:
      LOG_DBG("XTC", "Invalid page magic for page %u: 0x%08X (expected 0x%08X)", a, b,
              value.state->depth == 2 ? XTH_MAGIC : XTG_MAGIC);
      break;
    case Diagnostic::PagePayload:
      LOG_DBG("XTC", "Page %u payload size mismatch: declared %lu, expected %lu", a, static_cast<unsigned long>(b),
              static_cast<unsigned long>(c));
      break;
    case Diagnostic::BufferSmall:
      LOG_DBG("XTC", "Buffer too small: need %u, have %u", a, b);
      break;
    case Diagnostic::PageRead:
      LOG_DBG("XTC", "Page read error: expected %u, got %u", a, b);
      break;
  }
}

}  // namespace xtc
