#include "RustStore.h"

#include <HalStorage.h>
#include <esp_random.h>

#include <cassert>
#include <cstring>
#include <functional>

namespace clip_test_clock {
extern time_t now;
}
namespace {
struct Bytes {
  const uint8_t* data;
  size_t length;
};
Bytes bytes(std::string_view text) { return {reinterpret_cast<const uint8_t*>(text.data()), text.size()}; }
std::string string(Bytes text) { return {reinterpret_cast<const char*>(text.data), text.length}; }
struct Record {
  uint64_t revision;
  uint32_t timestamp, signature, startOffset, endOffset, textOffset;
  uint16_t spine, startPage, endPage, pageCount, startWord, endWord, wordCount, paragraph, textLength;
  uint8_t pending;
  uint8_t id[65], chapter[48];
};
static_assert(sizeof(Record) == 160);
static_assert(sizeof(Clipping) == 168);
Record record(const Clipping& c) {
  Record out{c.syncRevision, c.timestamp,      c.layoutSignature, c.startOffset,
             c.endOffset,    c.textOffset,     c.spineIndex,      c.startPage,
             c.endPage,      c.pageCount,      c.startWordIndex,  c.endWordIndex,
             c.wordCount,    c.paragraphIndex, c.textLength,      static_cast<uint8_t>(c.pendingUpload)};
  memcpy(out.id, c.id, sizeof(out.id));
  memcpy(out.chapter, c.chapterTitle, sizeof(out.chapter));
  return out;
}
Clipping clipping(const Record& r) {
  Clipping out;
  out.syncRevision = r.revision;
  out.timestamp = r.timestamp;
  out.layoutSignature = r.signature;
  out.startOffset = r.startOffset;
  out.endOffset = r.endOffset;
  out.textOffset = r.textOffset;
  out.spineIndex = r.spine;
  out.startPage = r.startPage;
  out.endPage = r.endPage;
  out.pageCount = r.pageCount;
  out.startWordIndex = r.startWord;
  out.endWordIndex = r.endWord;
  out.wordCount = r.wordCount;
  out.paragraphIndex = r.paragraph;
  out.textLength = r.textLength;
  // Reproduce the native oracle's raw bool byte only in its unchanged dump boundary.
  memcpy(&out.pendingUpload, &r.pending, 1);
  memcpy(out.id, r.id, sizeof(r.id));
  memcpy(out.chapterTitle, r.chapter, sizeof(r.chapter));
  return out;
}
HalFile& file(void* p) { return *static_cast<HalFile*>(p); }
}  // namespace

extern "C" {
void* xp_clippings_test_create(const void*);
void xp_clippings_test_destroy(void*);
uint8_t xp_clippings_test_load(void*, Bytes, Bytes, Bytes, Bytes);
void xp_clippings_test_unload(void*);
size_t xp_clippings_test_count(const void*);
uint8_t xp_clippings_test_row(const void*, size_t, Record*);
uint8_t xp_clippings_test_add(void*, const Record*, Bytes, Bytes);
uint8_t xp_clippings_test_remove(void*, size_t);
uint8_t xp_clippings_test_save(void*);
uint8_t xp_clippings_test_sync(void*);
uint8_t xp_clippings_test_uploads(void*);
uint8_t xp_clippings_test_remote(void*, const Record*, Bytes, uint8_t);
uint8_t xp_clippings_test_next_deletion(const void*, uint32_t*, char*);
uint8_t xp_clippings_test_finish_deletions(const void*);
uint8_t xp_clippings_test_text(const void*, size_t, uint8_t*, size_t, size_t*);
uint8_t xp_clippings_test_preview(const void*, size_t, char*, size_t, size_t*);
uint8_t xp_clippings_test_move(const void*, Bytes, Bytes);
uint8_t xp_clippings_test_delete_path(const void*, Bytes, Bytes);

void* xpc_test_open(Bytes path, uint8_t mode, uint8_t checked) {
  const auto name = string(path);
  HalFile value;
  if (checked) {
    if (!Storage.openFileForRead("CLIP", name, value)) return nullptr;
  } else {
    oflag_t flags = O_RDONLY;
    switch (mode) {
      case 0:
        break;
      case 1:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
      case 2:
        flags = O_WRONLY | O_CREAT | O_APPEND;
        break;
      case 3:
        flags = O_RDWR;
        break;
    }
    value = Storage.open(name.c_str(), flags);
    if (!value) return nullptr;
  }
  return new HalFile(std::move(value));
}
int32_t xpc_test_read(void* p, uint8_t* out, size_t n) { return file(p).read(out, n); }
size_t xpc_test_write(void* p, const uint8_t* data, size_t n) { return file(p).write(data, n); }
uint8_t xpc_test_seek(void* p, uint32_t offset, uint8_t relative) {
  return relative ? file(p).seekCur(offset) : file(p).seek(offset);
}
uint32_t xpc_test_position(void* p) { return file(p).position(); }
int32_t xpc_test_available(void* p) { return file(p).available(); }
uint32_t xpc_test_size(void* p) { return file(p).size(); }
uint8_t xpc_test_truncate(void* p, uint32_t size) { return file(p).truncate(size); }
void xpc_test_flush(void* p) { file(p).flush(); }
void xpc_test_close(void* p) { file(p).close(); }
void xpc_test_drop(void* p) { delete static_cast<HalFile*>(p); }
uint8_t xpc_test_directory(void* p) { return file(p).isDirectory(); }
void* xpc_test_next(void* p) {
  auto child = file(p).openNextFile();
  return child ? new HalFile(std::move(child)) : nullptr;
}
size_t xpc_test_name(void* p, uint8_t* out, size_t n) { return file(p).getName(reinterpret_cast<char*>(out), n); }
uint8_t xpc_test_exists(Bytes path) { return Storage.exists(string(path).c_str()); }
void xpc_test_mkdir(Bytes path) { Storage.mkdir(string(path).c_str()); }
uint8_t xpc_test_remove(Bytes path) { return Storage.remove(string(path).c_str()); }
uint8_t xpc_test_rename(Bytes from, Bytes to) { return Storage.rename(string(from).c_str(), string(to).c_str()); }
uint64_t xpc_test_hash(Bytes path) { return std::hash<std::string>{}(string(path)); }
int64_t xpc_test_epoch() { return clip_test_clock::now; }
void xpc_test_random(uint8_t* out, size_t n) { esp_fill_random(out, n); }
}

namespace {
struct Operations {
  decltype(&xpc_test_open) xpc_test_open;
  decltype(&xpc_test_read) xpc_test_read;
  decltype(&xpc_test_write) xpc_test_write;
  decltype(&xpc_test_seek) xpc_test_seek;
  decltype(&xpc_test_position) xpc_test_position;
  decltype(&xpc_test_available) xpc_test_available;
  decltype(&xpc_test_size) xpc_test_size;
  decltype(&xpc_test_truncate) xpc_test_truncate;
  decltype(&xpc_test_flush) xpc_test_flush;
  decltype(&xpc_test_close) xpc_test_close;
  decltype(&xpc_test_drop) xpc_test_drop;
  decltype(&xpc_test_directory) xpc_test_directory;
  decltype(&xpc_test_next) xpc_test_next;
  decltype(&xpc_test_name) xpc_test_name;
  decltype(&xpc_test_exists) xpc_test_exists;
  decltype(&xpc_test_mkdir) xpc_test_mkdir;
  decltype(&xpc_test_remove) xpc_test_remove;
  decltype(&xpc_test_rename) xpc_test_rename;
  decltype(&xpc_test_hash) xpc_test_hash;
  decltype(&xpc_test_epoch) xpc_test_epoch;
  decltype(&xpc_test_random) xpc_test_random;
};
const Operations operations{
    xpc_test_open, xpc_test_read,     xpc_test_write,  xpc_test_seek,  xpc_test_position, xpc_test_available,
    xpc_test_size, xpc_test_truncate, xpc_test_flush,  xpc_test_close, xpc_test_drop,     xpc_test_directory,
    xpc_test_next, xpc_test_name,     xpc_test_exists, xpc_test_mkdir, xpc_test_remove,   xpc_test_rename,
    xpc_test_hash, xpc_test_epoch,    xpc_test_random};
}  // namespace

RustStore::RustStore() : owner(xp_clippings_test_create(&operations)) { assert(owner); }
RustStore::~RustStore() { xp_clippings_test_destroy(owner); }
bool RustStore::loadForBook(std::string_view p, std::string_view t, std::string_view a, std::string_view k) {
  return xp_clippings_test_load(owner, bytes(p), bytes(t), bytes(a), bytes(k));
}
void RustStore::unload() { xp_clippings_test_unload(owner); }
size_t RustStore::clippingCount() const { return xp_clippings_test_count(owner); }
const Clipping* RustStore::clippingAt(size_t i) const {
  Record r;
  if (!xp_clippings_test_row(owner, i, &r)) return nullptr;
  snapshot = clipping(r);
  return &snapshot;
}
RustStore::AddResult RustStore::addClipping(uint16_t spine, uint16_t start, uint16_t end, uint16_t pages,
                                            uint16_t startWord, uint16_t endWord, uint16_t words, const char* chapter,
                                            uint16_t para, const std::string& text, uint32_t signature,
                                            uint32_t startOffset, uint32_t endOffset) {
  Clipping c;
  c.spineIndex = spine;
  c.startPage = start;
  c.endPage = end;
  c.pageCount = pages;
  c.startWordIndex = startWord;
  c.endWordIndex = endWord;
  c.wordCount = words;
  c.paragraphIndex = para;
  c.layoutSignature = signature;
  c.startOffset = startOffset;
  c.endOffset = endOffset;
  const auto r = record(c);
  return static_cast<AddResult>(xp_clippings_test_add(owner, &r, bytes(chapter ? chapter : ""), bytes(text)));
}
bool RustStore::removeClippingAt(size_t i) { return xp_clippings_test_remove(owner, i); }
bool RustStore::saveToFile() { return xp_clippings_test_save(owner); }
bool RustStore::prepareSync() { return xp_clippings_test_sync(owner); }
bool RustStore::finishUploads() { return xp_clippings_test_uploads(owner); }
bool RustStore::applyRemote(const Clipping& c, const std::string& text, bool deleted) {
  const auto r = record(c);
  return xp_clippings_test_remote(owner, &r, bytes(text), deleted);
}
bool RustStore::nextDeletion(uint32_t& offset, char (&id)[65]) const {
  return xp_clippings_test_next_deletion(owner, &offset, id);
}
bool RustStore::finishDeletions() { return xp_clippings_test_finish_deletions(owner); }
bool RustStore::readClippingText(size_t i, std::string& out) const {
  out.resize(CLIPPING_TEXT_MAX);
  size_t length = 0;
  const bool ok = xp_clippings_test_text(owner, i, reinterpret_cast<uint8_t*>(out.data()), out.size(), &length);
  out.resize(length);
  return ok;
}
bool RustStore::readClippingPreview(size_t i, char* out, size_t size, size_t& length) const {
  return xp_clippings_test_preview(owner, i, out, size, &length);
}
bool RustStore::moveBook(const std::string& from, const std::string& to) {
  RustStore store;
  return xp_clippings_test_move(store.owner, bytes(from), bytes(to));
}
bool RustStore::deleteForFilePath(const std::string& path, const std::string& type) {
  RustStore store;
  return xp_clippings_test_delete_path(store.owner, bytes(path), bytes(type));
}
