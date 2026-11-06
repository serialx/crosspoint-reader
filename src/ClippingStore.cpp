#include "ClippingStore.h"

#include <HalStorage.h>
#include <Memory.h>
#include <esp_random.h>
#include <xp_clippings.h>

#include <cstring>
#include <ctime>

namespace {
XpAppText clippingSpan(std::string_view text) { return {reinterpret_cast<const uint8_t*>(text.data()), text.size()}; }
}  // namespace

bool ClippingStoreFacade::loadForBook(std::string_view path, std::string_view title, std::string_view author,
                                      std::string_view kind) {
  return xp_clippings_p18_load(clippingSpan(path), clippingSpan(title), clippingSpan(author), clippingSpan(kind));
}
void ClippingStoreFacade::unload() { xp_clippings_p18_unload(); }
size_t ClippingStoreFacade::clippingCount() { return xp_clippings_p18_count(); }
bool ClippingStoreFacade::clippingAt(size_t index, Clipping& output) {
  if (index > UINT32_MAX) return false;
  XpClippingRecord row;
  if (!xp_clippings_p18_copy_row(static_cast<uint32_t>(index), &row)) return false;
  output.spineIndex = row.spine;
  output.startPage = row.start_page;
  output.endPage = row.end_page;
  output.pageCount = row.page_count;
  output.startWordIndex = row.start_word;
  output.endWordIndex = row.end_word;
  output.wordCount = row.word_count;
  output.paragraphIndex = row.paragraph;
  output.timestamp = row.timestamp;
  output.layoutSignature = row.signature;
  output.startOffset = row.start_offset;
  output.endOffset = row.end_offset;
  output.syncRevision = row.revision;
  output.pendingUpload = row.pending != 0;
  output.textOffset = row.text_offset;
  output.textLength = row.text_length;
  memcpy(output.id, row.id, sizeof(output.id));
  memcpy(output.chapterTitle, row.chapter, sizeof(output.chapterTitle));
  return true;
}
bool ClippingStoreFacade::readClippingText(size_t index, std::string& output) {
  Clipping row;
  if (!clippingAt(index, row)) return false;
  output.resize(row.textLength);
  uint32_t length = 0;
  if (!xp_clippings_p18_read_text(static_cast<uint32_t>(index), reinterpret_cast<uint8_t*>(output.data()),
                                  output.size(), &length)) {
    output.clear();
    return false;
  }
  output.resize(length);
  return true;
}
bool ClippingStoreFacade::prepareSync() { return xp_clippings_p18_prepare_sync(); }
bool ClippingStoreFacade::finishUploads() { return xp_clippings_p18_finish_uploads(); }
bool ClippingStoreFacade::applyRemote(const Clipping& clipping, const std::string& text, bool deleted) {
  XpClippingRecord row{
      .revision = clipping.syncRevision,
      .timestamp = clipping.timestamp,
      .signature = clipping.layoutSignature,
      .start_offset = clipping.startOffset,
      .end_offset = clipping.endOffset,
      .text_offset = clipping.textOffset,
      .spine = clipping.spineIndex,
      .start_page = clipping.startPage,
      .end_page = clipping.endPage,
      .page_count = clipping.pageCount,
      .start_word = clipping.startWordIndex,
      .end_word = clipping.endWordIndex,
      .word_count = clipping.wordCount,
      .paragraph = clipping.paragraphIndex,
      .text_length = clipping.textLength,
      .pending = static_cast<uint8_t>(clipping.pendingUpload),
      .id = {},
      .chapter = {},
  };
  memcpy(row.id, clipping.id, sizeof(row.id));
  memcpy(row.chapter, clipping.chapterTitle, sizeof(row.chapter));
  return xp_clippings_p18_apply_remote(&row, clippingSpan(text), deleted);
}
bool ClippingStoreFacade::nextDeletion(uint32_t& offset, char (&id)[65]) {
  return xp_clippings_p18_next_deletion(&offset, reinterpret_cast<uint8_t*>(id));
}
bool ClippingStoreFacade::finishDeletions() { return xp_clippings_p18_finish_deletions(); }
bool ClippingStoreFacade::moveBook(const std::string& from, const std::string& to) {
  return xp_clippings_p17_p18_move_book(clippingSpan(from), clippingSpan(to));
}
bool ClippingStoreFacade::deleteForFilePath(const std::string& path, const std::string& kind) {
  return xp_clippings_p17_p18_delete_for_path(clippingSpan(path), clippingSpan(kind));
}

const XpClippingPhysical CLIPPING_PHYSICAL{
    .open = [](XpAppText path, XpClippingOpenMode mode) -> void* {
      const char* name = path.len ? reinterpret_cast<const char*>(path.data) : "";
      int flags = 0;
      switch (mode) {
        case XP_CLIPPING_OPEN_READ:
          break;
        case XP_CLIPPING_OPEN_REWRITE:
          flags = O_WRONLY | O_CREAT | O_TRUNC;
          break;
        case XP_CLIPPING_OPEN_APPEND:
          flags = O_WRONLY | O_CREAT | O_APPEND;
          break;
        case XP_CLIPPING_OPEN_READ_WRITE:
          flags = O_RDWR;
          break;
      }
      auto file = makeUniqueNoThrow<HalFile>();
      if (!file) return nullptr;
      *file = Storage.open(name, flags);
      return *file ? file.release() : nullptr;
    },
    .seek_relative = [](void* file, uint32_t offset) -> uint8_t {
      return static_cast<HalFile*>(file)->seekCur(offset);
    },
    .available = [](void* file) -> int32_t { return static_cast<HalFile*>(file)->available(); },
    .truncate = [](void* file, uint32_t size) -> uint8_t { return static_cast<HalFile*>(file)->truncate(size); },
    .is_directory = [](void* file) -> uint8_t { return static_cast<HalFile*>(file)->isDirectory(); },
    .next = [](void* directory, uint8_t* failed) -> void* {
      *failed = 0;
      auto entry = static_cast<HalFile*>(directory)->openNextFile();
      if (!entry) return nullptr;
      auto file = makeUniqueNoThrow<HalFile>();
      if (!file) {
        *failed = 1;
        return nullptr;
      }
      *file = std::move(entry);
      return file.release();
    },
    .name = [](void* file, uint8_t* out, uint32_t capacity) -> uint32_t {
      return static_cast<HalFile*>(file)->getName(reinterpret_cast<char*>(out), capacity);
    },
    .epoch_seconds = []() -> int64_t { return time(nullptr); },
    .random = [](uint8_t* out, uint32_t size) { esp_fill_random(out, size); },
};
