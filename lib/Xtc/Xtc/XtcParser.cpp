#include "XtcParser.h"

#include <Logging.h>

namespace xtc {
namespace {
enum class MetadataAction : uint32_t { Clear, Title, Author, Trim };
enum class ChapterAction : uint32_t { Clear, Reserve, Append };
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

XtcParser::XtcParser() {
  m_state.width = DISPLAY_WIDTH;
  m_state.height = DISPLAY_HEIGHT;
  m_state.depth = 1;
}
XtcParser::~XtcParser() { close(); }

XpBookXtcServices XtcParser::services() {
  return {this,
          [](void* ctx) {
            auto& p = *static_cast<XtcParser*>(ctx);
            return Storage.openFileForRead("XTC", p.m_filepath.c_str(), p.m_file);
          },
          [](void* ctx) { return static_cast<XtcParser*>(ctx)->m_file.isOpen(); },
          [](void* ctx) { static_cast<XtcParser*>(ctx)->m_file.close(); },
          [](void* ctx, uint8_t* out, size_t size) -> int32_t {
            return static_cast<XtcParser*>(ctx)->m_file.read(out, size);
          },
          [](void* ctx, uint64_t offset) { return static_cast<XtcParser*>(ctx)->m_file.seek64(offset); },
          [](void* ctx) -> uint64_t { return static_cast<XtcParser*>(ctx)->m_file.fileSize64(); },
          [](void* ctx, uint32_t action, const uint8_t* bytes, size_t size) {
            auto& p = *static_cast<XtcParser*>(ctx);
            switch (static_cast<MetadataAction>(action)) {
              case MetadataAction::Clear:
                p.m_title.clear();
                p.m_author.clear();
                break;
              case MetadataAction::Title:
                p.m_title.assign(reinterpret_cast<const char*>(bytes), size);
                break;
              case MetadataAction::Author:
                p.m_author.assign(reinterpret_cast<const char*>(bytes), size);
                break;
              case MetadataAction::Trim:
                p.m_title.shrink_to_fit();
                p.m_author.shrink_to_fit();
                break;
            }
          },
          [](void* ctx, uint32_t action, size_t count, const uint8_t* bytes, uint16_t start, uint16_t end) -> size_t {
            auto& p = *static_cast<XtcParser*>(ctx);
            switch (static_cast<ChapterAction>(action)) {
              case ChapterAction::Clear:
                p.m_chapters.clear();
                break;
              case ChapterAction::Reserve:
                p.m_chapters.reserve(count);
                break;
              case ChapterAction::Append:
                p.m_chapters.push_back({std::string(reinterpret_cast<const char*>(bytes), count), start, end});
                break;
            }
            return p.m_chapters.size();
          },
          [](void* ctx, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t) {
            static_cast<XtcParser*>(ctx)->diagnostic(event, a, b, c);
          }};
}

void XtcParser::diagnostic(uint32_t event, [[maybe_unused]] uint32_t a, uint32_t b, uint32_t c) {
  [[maybe_unused]] const auto& h = m_state.header;
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
              h.magic == XTCH_MAGIC ? "XTCH" : "XTC", h.versionMajor, h.versionMinor, h.pageCount, m_state.depth);
      break;
    case Diagnostic::Title:
      LOG_DBG("XTC", "Title: %s", m_title.c_str());
      break;
    case Diagnostic::Author:
      LOG_DBG("XTC", "Author: %s", m_author.c_str());
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
      LOG_DBG("XTC", "Page table validated: %u pages, default %dx%d", h.pageCount, m_state.width, m_state.height);
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
      LOG_DBG("XTC", "Opened file: %s (%u pages, %dx%d)", m_filepath.c_str(), h.pageCount, m_state.width,
              m_state.height);
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
              m_state.depth == 2 ? XTH_MAGIC : XTG_MAGIC);
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

XtcError XtcParser::open(const char* filepath) {
  if (m_state.opened) close();
  m_filepath = filepath;
  const auto io = services();
  return static_cast<XtcError>(xp_book_xtc_open(&m_state, &io));
}
void XtcParser::close() {
  const auto io = services();
  xp_book_xtc_close(&m_state, &io);
}
const std::vector<ChapterInfo>& XtcParser::getChapters() {
  const auto io = services();
  xp_book_xtc_chapters(&m_state, &io);
  return m_chapters;
}
bool XtcParser::getPageInfo(uint32_t pageIndex, PageInfo& info) {
  const auto io = services();
  XpXtcEntry entry;
  if (!xp_book_xtc_page_info(&m_state, &io, pageIndex, &entry)) return false;
  info.offset = entry.offset;
  info.size = entry.size;
  info.width = entry.width;
  info.height = entry.height;
  info.bitDepth = m_state.depth;
  return true;
}
size_t XtcParser::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) {
  const auto io = services();
  return xp_book_xtc_load(&m_state, &io, pageIndex, buffer, bufferSize);
}
XtcError XtcParser::loadPageStreaming(uint32_t pageIndex, std::function<void(const uint8_t*, size_t, size_t)> callback,
                                      size_t chunkSize) {
  struct Context {
    std::vector<uint8_t> chunk;
    decltype(callback)& emit;
  } context{{}, callback};
  const XpBookXtcStream sink{&context,
                             [](void* opaque, size_t size) -> uint8_t* {
                               auto& c = *static_cast<Context*>(opaque);
                               c.chunk = std::vector<uint8_t>(size);
                               return c.chunk.data();
                             },
                             [](void* opaque, const uint8_t* bytes, size_t size, size_t offset) {
                               static_cast<Context*>(opaque)->emit(bytes, size, offset);
                             }};
  const auto io = services();
  return static_cast<XtcError>(xp_book_xtc_stream(&m_state, &io, pageIndex, chunkSize, &sink));
}
bool XtcParser::isValidXtcFile(const char* filepath) {
  HalFile file;
  if (!Storage.openFileForRead("XTC", filepath, file)) return false;
  uint8_t magic[4];
  const size_t count = file.read(magic, sizeof(magic));
  file.close();
  return xp_xtc_bit_depth(magic, count) != 0;
}
}  // namespace xtc
