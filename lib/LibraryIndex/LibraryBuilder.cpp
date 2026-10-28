#include "LibraryBuilder.h"

#include <Arduino.h>
#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>
#include <xp_ffi.h>

#include <cstring>
namespace library {
namespace {
constexpr char INDEX_PATH[] = "/.crosspoint/library.idx";
[[maybe_unused]] constexpr char NEW_PATH[] = "/.crosspoint/library.new";
[[maybe_unused]] constexpr char BACKUP_PATH[] = "/.crosspoint/library.bak";
[[maybe_unused]] constexpr char FOLDER_STAGE_PATH[] = "/.crosspoint/library.stage.f";
uint8_t dirtyInMemory = 0;
struct Context {
  // At most previous, stage, folders, directory, and current entry are open.
  HalFile files[6];
  std::string title, author;
};
enum class FileOp : uint8_t {
  ReadOpen,
  WriteOpen,
  Open,
  Next,
  Close,
  Read,
  Write,
  Seek,
  Position,
  Size,
  Time,
  IsDirectory,
  Rewind,
  Name
};
int64_t fileOp(void* opaque, uint8_t operation, uint32_t id, const uint8_t* path, uint8_t* data, size_t len,
               uint64_t value) {
  auto& ctx = *static_cast<Context*>(opaque);
  const auto op = static_cast<FileOp>(operation);
  if (op == FileOp::ReadOpen || op == FileOp::WriteOpen || op == FileOp::Open || op == FileOp::Next) {
    for (size_t i = 0; i < 6; ++i) {
      if (ctx.files[i].isOpen()) continue;
      auto& dest = ctx.files[i];
      const auto* name = reinterpret_cast<const char*>(path);
      switch (op) {
        case FileOp::ReadOpen:
          Storage.openFileForRead("LIBIDX", name, dest);
          break;
        case FileOp::WriteOpen:
          Storage.openFileForWrite("LIBIDX", name, dest);
          break;
        case FileOp::Open:
          dest = Storage.open(name);
          break;
        case FileOp::Next:
          if (id > 0 && id <= 6) dest = ctx.files[id - 1].openNextFile();
          break;
        default:
          break;
      }
      return dest ? static_cast<int64_t>(i + 1) : 0;
    }
    return 0;
  }
  if (id == 0 || id > 6) return 0;
  auto& file = ctx.files[id - 1];
  switch (op) {
    case FileOp::Close:
      return file.close();
    case FileOp::Read:
      return file.read(data, len);
    case FileOp::Write:
      return file.write(data, len);
    case FileOp::Seek:
      return file.seekSet(value);
    case FileOp::Position:
      return file.position();
    case FileOp::Size:
      return file.fileSize64();
    case FileOp::Time:
      return file.modificationTime();
    case FileOp::IsDirectory:
      return file.isDirectory();
    case FileOp::Rewind:
      file.rewindDirectory();
      return 1;
    case FileOp::Name:
      return file.getName(reinterpret_cast<char*>(data), len);
    case FileOp::ReadOpen:
    case FileOp::WriteOpen:
    case FileOp::Open:
    case FileOp::Next:
      return 0;
  }
  return 0;
}
enum class StorageOp : uint8_t { Exists, Remove, Mkdir, Rename };
uint8_t storageOp(void*, uint8_t operation, const uint8_t* a, const uint8_t* b) {
  const auto* path = reinterpret_cast<const char*>(a);
  switch (static_cast<StorageOp>(operation)) {
    case StorageOp::Exists:
      return Storage.exists(path);
    case StorageOp::Remove:
      return Storage.remove(path);
    case StorageOp::Mkdir:
      return Storage.mkdir(path);
    case StorageOp::Rename:
      return Storage.rename(path, reinterpret_cast<const char*>(b));
  }
  return false;
}
uint8_t* allocate(void*, size_t bytes) {
  // Rust owns this zeroed allocation until its matching release callback.
  return makeUniqueNoThrow<uint8_t[]>(bytes).release();
}
void release(void*, uint8_t* data) { delete[] data; }
uint32_t clock(void*, uint8_t operation) {
  if (operation) {
    delay(1);
    return 0;
  }
  return millis();
}
enum class MetadataOp : uint8_t { Epub, Xtc, Release };
uint8_t metadata(void* opaque, const uint8_t* path, uint8_t operation, uint8_t** title, size_t* titleLen,
                 const uint8_t** author, size_t* authorLen) {
  auto& ctx = *static_cast<Context*>(opaque);
  ctx.title.clear();
  ctx.author.clear();
  bool ok = false;
  switch (static_cast<MetadataOp>(operation)) {
    case MetadataOp::Epub: {
      Epub epub(std::string(reinterpret_cast<const char*>(path)), "/.crosspoint");
      ok = epub.loadMetadata(ctx.title, ctx.author);
      break;
    }
    case MetadataOp::Xtc: {
      Xtc xtc(std::string(reinterpret_cast<const char*>(path)), "/.crosspoint");
      ok = xtc.load();
      if (ok) { ctx.title = xtc.getTitle(); ctx.author = xtc.getAuthor(); }
      break;
    }
    case MetadataOp::Release:
      std::string().swap(ctx.title);
      std::string().swap(ctx.author);
      return 0;
  }
  *title = reinterpret_cast<uint8_t*>(ctx.title.data());
  *titleLen = ctx.title.size();
  *author = reinterpret_cast<const uint8_t*>(ctx.author.data());
  *authorLen = ctx.author.size();
  return ok;
}

void diagnostic(void* opaque, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const uint8_t* text) {
  const auto* path = reinterpret_cast<const char*>(text);
  (void)opaque;
  (void)path;
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  switch (event) {
    case 1:
      LOG_INF("LIBIDX", "restored previous index after interrupted install");
      break;
    case 2:
      LOG_ERR("LIBIDX", "cannot restore %s; rebuild deferred", BACKUP_PATH);
      break;
    case 3:
      LOG_ERR("LIBIDX", "cannot remove stale backup; rebuild deferred");
      break;
    case 4:
      LOG_ERR("LIBIDX", "cannot read live index during recovery");
      break;
    case 5:
      LOG_ERR("LIBIDX", "validated backup could not replace an invalid live index");
      break;
    case 6:
      LOG_ERR("LIBIDX", "cannot read backup index during recovery");
      break;
    case 7:
      LOG_ERR("LIBIDX", "invalid stale backup cannot be removed; rebuild deferred");
      break;
    case 8:
      LOG_ERR("LIBIDX", "cannot remove stale backup; keeping the live index");
      break;
    case 9:
      LOG_ERR("LIBIDX", "cannot stage previous index for replacement");
      break;
    case 10:
      LOG_ERR("LIBIDX", "rename %s -> %s failed", NEW_PATH, INDEX_PATH);
      break;
    case 11:
      LOG_ERR("LIBIDX", "previous index rollback failed; backup retained at %s", BACKUP_PATH);
      break;
    case 12:
      LOG_ERR("LIBIDX", "new index installed but stale backup cleanup failed");
      break;
    case 13:
      LOG_ERR("LIBIDX", "cannot clear dirty marker");
      break;
    case 14:
      LOG_DBG("LIBIDX", "no metadata for %s", path);
      break;
    case 15:
      LOG_ERR("LIBIDX", "cannot resume directory %s at %u", path, a);
      break;
    case 16:
      LOG_INF("LIBIDX", "duplicate detection capped at %u entries in %s", a, path);
      break;
    case 17:
      LOG_ERR("LIBIDX", "folder stage write failed: %s", path);
      break;
    case 18:
      LOG_ERR("LIBIDX", "arrival order array alloc failed");
      break;
    case 19:
      LOG_ERR("LIBIDX", "record stage read failed at %u", a);
      break;
    case 20:
      LOG_ERR("LIBIDX", "folder stage truncated: read %u of %u bytes", a, b);
      break;
    case 21:
      LOG_ERR("LIBIDX", "folder stage unreadable: %s", FOLDER_STAGE_PATH);
      break;
    case 22:
      LOG_ERR("LIBIDX", "emit failed while copying the folder stage");
      break;
    case 23:
      LOG_DBG("LIBIDX", "author sort alloc: %u bytes, heap %u, max block %u", a,
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      break;
    case 24:
      LOG_ERR("LIBIDX", "canonical author array alloc failed; author order degraded");
      break;
    case 25:
      LOG_ERR("LIBIDX", "author spelling scratch alloc failed; spelling harmonisation skipped");
      break;
    case 26:
      LOG_ERR("LIBIDX", "OOM: %u-byte mtime array, arrival falls back to firstSeen", a);
      break;
    case 27:
      LOG_INF("LIBIDX", "%u books: sort allocation unavailable; index kept in walk order", a);
      break;
    case 28:
      LOG_ERR("LIBIDX", "emit failed while reading the record stage");
      break;
    case 29:
      LOG_ERR("LIBIDX", "staging buffers alloc failed");
      break;
    case 30:
      LOG_ERR("LIBIDX", "emit incomplete (I/O %s, close %s, size %u vs %u) — keeping the old index",
              a ? "failed" : "ok", b ? "ok" : "failed", c, d);
      break;
    case 32:
      LOG_ERR("LIBIDX", "cannot create cache directory for dirty marker");
      break;
    case 33:
      LOG_ERR("LIBIDX", "cannot create dirty marker");
      break;
    case 34:
      LOG_ERR("LIBIDX", "name buffer alloc failed (%u bytes)", a);
      break;
    case 35:
      LOG_ERR("LIBIDX", "staging record alloc failed (%u bytes)", a);
      break;
    case 36:
      LOG_ERR("LIBIDX", "dedup key buffer alloc failed; continuing without duplicate detection");
      break;
    case 37:
      LOG_ERR("LIBIDX", "prior index array alloc failed");
      break;
    case 38:
      LOG_ERR("LIBIDX", "prior index read failed at record %u", a);
      break;
    case 39:
      LOG_ERR("LIBIDX", "cannot read previous index; rebuild deferred");
      break;
    case 40:
      LOG_ERR("LIBIDX", "cannot open staging files");
      break;
    case 41:
      LOG_DBG("LIBIDX", "phase prepare/prior: %ums", a);
      break;
    case 42:
      LOG_DBG("LIBIDX", "phase walk/metadata/stage: %ums", a);
      break;
    case 43:
      LOG_ERR("LIBIDX", "staging failed; keeping the previous index");
      break;
    case 44:
      LOG_INF("LIBIDX", "unchanged: %u reused, %u parsed, no replacement, %ums", a, b, c);
      break;
    case 45:
      LOG_ERR("LIBIDX", "firstSeen array alloc failed");
      break;
    case 46:
      LOG_ERR("LIBIDX", "firstSeen reconciliation: cannot reopen the stage");
      break;
    case 47:
      LOG_ERR("LIBIDX", "firstSeen reconciliation: short read at record %u", a);
      break;
    case 48:
      LOG_ERR("LIBIDX", "firstSeen reconciliation: stage close failed");
      break;
    case 49:
      LOG_DBG("LIBIDX", "phase reconcile: %ums", a);
      break;
    case 50:
      LOG_ERR("LIBIDX", "order array alloc failed (%u books)", a);
      break;
    case 51:
      LOG_DBG("LIBIDX", "title sort alloc: %u bytes, heap %u, max block %u", a,
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      break;
    case 52:
      LOG_ERR("LIBIDX", "title sort: cannot reopen the record stage");
      break;
    case 53:
      LOG_ERR("LIBIDX", "title sort: record stage read failed at %u", a);
      break;
    case 54:
      LOG_ERR("LIBIDX", "title sort: stage close failed");
      break;
    case 55:
      LOG_ERR("LIBIDX", "sort skipped: key array alloc failed");
      break;
    case 56:
      LOG_DBG("LIBIDX", "phase title order: %ums", a);
      break;
    case 57:
      LOG_DBG("LIBIDX", "phase author/orders/emit: %ums", a);
      break;
    case 59:
      LOG_INF("LIBIDX", "restored previous index after interrupted install");
      break;
    case 60:
      LOG_INF("LIBIDX", "index rejected: %s", clixValidityName(static_cast<ClixValidity>(a)));
      break;
  }
}
XpLibraryServices services(Context& context) {
  return {&context, fileOp, storageOp, allocate, release, clock, metadata, diagnostic};
}
static_assert(sizeof(BuildStats) == sizeof(XpLibraryStats));
static_assert(offsetof(BuildStats, indexReplaced) == offsetof(XpLibraryStats, replaced));
}  // namespace
const char* libraryIndexPath() { return INDEX_PATH; }
bool isLibraryBookFile(std::string_view path) {
  return xp_library_book(reinterpret_cast<const uint8_t*>(path.data()), path.size());
}
bool buildLibraryIndex(const char* root, BuildStats& stats, bool readMetadata) {
  Context context{};
  const auto api = services(context);
  const auto result = xp_library_build(&api, reinterpret_cast<const uint8_t*>(root), strlen(root) + 1,
                                       reinterpret_cast<XpLibraryStats*>(&stats), readMetadata, &dirtyInMemory);
  if (result == XP_LIBRARY_BUILD_EMIT_FAILED || result == XP_LIBRARY_BUILD_INSTALLED) {
    LOG_INF("LIBIDX",
            "%s: %u books, %u folders, %u parsed, %u metadata reused, replaced %u, %u dup dropped, %u unreadable, %ums",
            stats.indexReplaced ? "built" : "FAILED", static_cast<unsigned>(stats.books),
            static_cast<unsigned>(stats.folders), static_cast<unsigned>(stats.parsed),
            static_cast<unsigned>(stats.metadataReused), static_cast<unsigned>(stats.indexReplaced),
            static_cast<unsigned>(stats.duplicatesDropped), static_cast<unsigned>(stats.unreadableSkipped),
            static_cast<unsigned>(stats.walkMs));
  }
  if (result == XP_LIBRARY_BUILD_INSTALLED) xp_library_dirty(&api, &dirtyInMemory, 2);
  return result == XP_LIBRARY_BUILD_UNCHANGED || result == XP_LIBRARY_BUILD_INSTALLED;
}
bool markLibraryIndexDirty() {
  Context context{};
  const auto api = services(context);
  return xp_library_dirty(&api, &dirtyInMemory, true);
}
bool isLibraryIndexDirty() {
  Context context{};
  const auto api = services(context);
  return xp_library_dirty(&api, &dirtyInMemory, false);
}
}  // namespace library
