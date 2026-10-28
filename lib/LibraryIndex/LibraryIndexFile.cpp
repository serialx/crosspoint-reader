#include "LibraryIndexFile.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "LibraryText.h"

namespace library {

LibraryIndexFile::~LibraryIndexFile() { close(); }

bool LibraryIndexFile::open(const char* path) { return openImpl(path, false); }

bool LibraryIndexFile::openForReconciliation(const char* path) { return openImpl(path, true); }

bool LibraryIndexFile::openImpl(const char* path, const bool acceptStaleFold) {
  close();
  readFailed = false;
  if (!Storage.openFileForRead("LIBIDX", path, file)) {
    readFailed = true;
    return false;
  }

  if (file.read(&head, sizeof(head)) != static_cast<int>(sizeof(head))) {
    readFailed = true;
    lastValidity = ClixValidity::SizeMismatch;
    file.close();
    return false;
  }

  lastValidity =
      acceptStaleFold ? validateHeaderStructure(head, file.fileSize64()) : validateHeader(head, file.fileSize64());
  if (lastValidity != ClixValidity::Ok) {
    LOG_INF("LIBIDX", "index rejected: %s", clixValidityName(lastValidity));
    file.close();
    return false;
  }
  opened = true;
  return true;
}

void LibraryIndexFile::close() {
  if (file.isOpen()) file.close();
  opened = false;
}

bool LibraryIndexFile::readAt(const uint32_t offset, void* dst, const size_t len) {
  if (!opened) return false;
  // Every offset handed to this function comes from the header, and the header
  // was validated against the real file size, so a short read means the card
  // changed under us rather than a bad computation.
  if (!file.seekSet(offset) || file.read(dst, len) != static_cast<int>(len)) {
    readFailed = true;
    return false;
  }
  return true;
}

XpLibraryIndexIo LibraryIndexFile::rustIo(RustIoContext& context) {
  return {&context,
          [](void* ctx, uint32_t at, uint8_t* out, size_t len) -> uint8_t {
            return static_cast<RustIoContext*>(ctx)->index.readAt(at, out, len);
          },
          [](void* ctx, uint32_t at, size_t len, uint8_t append, uint8_t* last) -> uint8_t {
            auto& context = *static_cast<RustIoContext*>(ctx);
            auto& self = context.index;
            auto& out = *context.output;
            if (!append) out.clear();
            const auto start = out.size();
            out.resize(start + len);
            if (len && !self.readAt(at, out.data() + start, len)) return false;
            *last = out.empty() ? 0 : static_cast<uint8_t>(out.back());
            return true;
          },
          [](void* ctx, const uint8_t* bytes, size_t len) {
            static_cast<RustIoContext*>(ctx)->output->append(reinterpret_cast<const char*>(bytes), len);
          },
          [](void* ctx) { static_cast<RustIoContext*>(ctx)->index.readFailed = true; }};
}
uint16_t LibraryIndexFile::ordinalForRow(SortOrder order, uint16_t row) {
  if (!opened || row >= head.bookCount) return 0xFFFF;
  RustIoContext context{*this};
  auto io = rustIo(context);
  return xp_library_read(&head, &io, nullptr, 1, (static_cast<uint32_t>(order) << 16) | row, nullptr);
}
bool LibraryIndexFile::readRecord(uint16_t ordinal, ClixRecord& out) {
  if (!opened) return false;
  RustIoContext context{*this};
  auto io = rustIo(context);
  return xp_library_read(&head, &io, &out, 0, ordinal, nullptr);
}
bool LibraryIndexFile::readPathHash(const ClixRecord& record, uint64_t& out) {
  out = 0;
  if (!opened) return false;
  RustIoContext context{*this};
  auto io = rustIo(context);
  return xp_library_read(&head, &io, const_cast<ClixRecord*>(&record), 2, 0, &out);
}
bool LibraryIndexFile::readName(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened) return false;
  RustIoContext context{*this, &out};
  auto io = rustIo(context);
  return xp_library_read(&head, &io, const_cast<ClixRecord*>(&record), 3, 0, nullptr);
}
bool LibraryIndexFile::readBlobField(const ClixRecord& record, uint8_t field, std::string& out) {
  out.clear();
  if (!opened) return false;
  RustIoContext context{*this, &out};
  auto io = rustIo(context);
  return xp_library_read(&head, &io, const_cast<ClixRecord*>(&record), 4, field, nullptr);
}
bool LibraryIndexFile::readAuthor(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 0, out) && !out.empty();
}
bool LibraryIndexFile::readTitle(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 1, out) && !out.empty();
}
bool LibraryIndexFile::readSourceAuthor(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 2, out);
}
bool LibraryIndexFile::readPath(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened) return false;
  std::string path;
  RustIoContext context{*this, &path};
  auto io = rustIo(context);
  if (!xp_library_read(&head, &io, const_cast<ClixRecord*>(&record), 5, 0, nullptr)) return false;
  out = std::move(path);
  return true;
}
bool LibraryIndexFile::recentRowsFor(const BookIdentity* books, size_t count, uint16_t* rows) {
  for (size_t i = 0; i < count; ++i) rows[i] = 0xFFFF;
  if (!opened || count == 0 || count > MAX_IDENTITY_LOOKUPS || head.bookCount == 0) return opened;
  auto chunk = makeUniqueNoThrow<uint8_t[]>(4096);
  if (!chunk) {
    LOG_ERR("LIBIDX", "OOM: %u-byte lookup chunk", 4096u);
    return false;
  }
  static_assert(sizeof(BookIdentity) == sizeof(XpLibraryIdentity));
  RustIoContext context{*this};
  auto io = rustIo(context);
  return xp_library_recent(&head, &io, reinterpret_cast<const XpLibraryIdentity*>(books), count, rows, chunk.get());
}
}  // namespace library
