/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <xp_book.h>

#include <optional>

#include "../Memory/Memory.h"

namespace {
// Keep the copy buffer off the stack for ordinary cover operations.
[[gnu::noinline]] bool copyCoverFile(HalFile& source, HalFile& target) {
  uint8_t buffer[512];
  struct Context {
    HalFile& source;
    HalFile& target;
  } context{source, target};
  return xp_book_copy_available(
      buffer, sizeof(buffer), &context,
      [](void* opaque) { return static_cast<Context*>(opaque)->source.available() != 0; },
      [](void* opaque, uint8_t* bytes, size_t size) -> int32_t {
        return static_cast<Context*>(opaque)->source.read(bytes, size);
      },
      [](void* opaque, const uint8_t* bytes, size_t size) {
        return static_cast<Context*>(opaque)->target.write(bytes, size);
      });
}
}  // namespace

bool Xtc::load() {
  LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  parser = makeUniqueNoThrow<xtc::XtcParser>();
  if (!parser) {
    LOG_ERR("XTC", "OOM: XtcParser");
    return false;
  }

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  const XpBookMetadataServices services{const_cast<Xtc*>(this),
                                        [](void* opaque, uint32_t operation) -> uint8_t {
                                          const auto& book = *static_cast<Xtc*>(opaque);
                                          switch (operation) {
                                            case XP_BOOK_META_CACHE_EXISTS:
                                              return Storage.exists(book.cachePath.c_str());
                                            case XP_BOOK_META_CACHE_REMOVE:
                                              return Storage.removeDir(book.cachePath.c_str());
                                          }
                                          return false;
                                        },
                                        [](void*, uint32_t event) {
                                          switch (event) {
                                            case XP_BOOK_META_LOG_CACHE_ABSENT:
                                              LOG_DBG("XTC", "Cache does not exist, no action needed");
                                              break;
                                            case XP_BOOK_META_LOG_CACHE_CLEAR_FAILED:
                                              LOG_ERR("XTC", "Failed to clear cache");
                                              break;
                                            case XP_BOOK_META_LOG_CACHE_CLEARED:
                                              LOG_DBG("XTC", "Cache cleared successfully");
                                              break;
                                          }
                                        }};
  return xp_book_clear_cache(&services);
}

void Xtc::setupCacheDir() const {
  xp_book_directories({reinterpret_cast<const uint8_t*>(cachePath.data()), cachePath.size()},
                      const_cast<std::string*>(&cachePath), [](void* opaque, uint32_t operation, XpBookBorrow path) {
                        const auto& root = *static_cast<const std::string*>(opaque);
                        if (operation == XP_BOOK_DIRECTORY_EXISTS) return Storage.exists(root.c_str());
                        if (path.len == root.size())
                          Storage.mkdir(root.c_str());
                        else
                          Storage.mkdir(std::string(reinterpret_cast<const char*>(path.data), path.len).c_str());
                        return true;
                      });
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  std::string title = parser->getTitle();
  const auto value = xp_book_xtc_title({reinterpret_cast<const uint8_t*>(filepath.data()), filepath.size()},
                                       {reinterpret_cast<const uint8_t*>(title.data()), title.size()});
  if (value.data == reinterpret_cast<const uint8_t*>(title.data())) return title;
  return {reinterpret_cast<const char*>(value.data), value.len};
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const { return cachePath + xp_book_path(XpBookPath::Cover); }

bool Xtc::generateCoverBmp() const { return generateImage(false, 0); }
std::string Xtc::getThumbBmpPath() const { return cachePath + xp_book_path(XpBookPath::ThumbPattern); }
std::string Xtc::getThumbBmpPath(int height) const {
  return cachePath + xp_book_path(XpBookPath::ThumbPrefix) + std::to_string(height) + ".bmp";
}
bool Xtc::generateThumbBmp(int height) const { return generateImage(true, height); }
bool Xtc::generateImage(bool thumb, int height) const {
  struct Context {
    const Xtc& book;
    bool thumb;
    int height;
    Context(const Xtc& book, bool thumb, int height) : book(book), thumb(thumb), height(height) {}
    xtc::PageInfo page{};
    uint8_t depth = 0;
    XpBookXtcThumbnail plan{};
    size_t bitmap = 0, row = 0;
    std::unique_ptr<uint8_t[]> scratch;
    struct CopyFiles {
      HalFile source, target;
    };
    std::optional<CopyFiles> copy;
    HalFile image;
    std::string path() const { return thumb ? book.getThumbBmpPath(height) : book.getCoverBmpPath(); }
  } context{*this, thumb, height};
  const XpBookXtcCoverServices services{
      &context,
      [](void* opaque, uint32_t op) -> size_t {
        auto& ctx = *static_cast<Context*>(opaque);
        const auto& book = ctx.book;
        switch (static_cast<XpBookXtcOp>(op)) {
          case XP_BOOK_XTC_EXISTS:
            return Storage.exists(ctx.path().c_str());
          case XP_BOOK_XTC_READY:
            return book.loaded && bool(book.parser);
          case XP_BOOK_XTC_PAGES:
            return book.parser->getPageCount();
          case XP_BOOK_XTC_SETUP:
            book.setupCacheDir();
            return 1;
          case XP_BOOK_XTC_FIRST:
            return book.parser->getPageInfo(0, ctx.page);
          case XP_BOOK_XTC_GENERATE_COVER:
            return book.generateCoverBmp();
          case XP_BOOK_XTC_OPEN_SOURCE:
            return Storage.openFileForRead("XTC", book.getCoverBmpPath(), ctx.copy.emplace().source);
          case XP_BOOK_XTC_OPEN_TARGET:
            return Storage.openFileForWrite("XTC", book.getThumbBmpPath(ctx.height), ctx.copy->target);
          case XP_BOOK_XTC_COPY:
            return copyCoverFile(ctx.copy->source, ctx.copy->target);
          case XP_BOOK_XTC_LOAD:
            return const_cast<xtc::XtcParser*>(book.parser.get())->loadPage(0, ctx.scratch.get(), ctx.bitmap);
          case XP_BOOK_XTC_OPEN_IMAGE:
            return Storage.openFileForWrite("XTC", ctx.path(), ctx.image);
          case XP_BOOK_XTC_RENDER:
            xp_book_xtc_image(
                ctx.scratch.get(), ctx.bitmap, ctx.page.width, ctx.page.height, ctx.depth,
                ctx.scratch.get() + ctx.bitmap, ctx.row, ctx.thumb ? &ctx.plan : nullptr, &ctx.image,
                [](void* opaque, const uint8_t* bytes, size_t size) {
                  return static_cast<HalFile*>(opaque)->write(bytes, size);
                },
                ctx.thumb ? +[](void*) { vTaskDelay(1); } : nullptr);
            return 1;
        }
        return 0;
      },
      [](void* opaque) -> XpBookXtcInfo {
        auto& ctx = *static_cast<Context*>(opaque);
        ctx.depth = ctx.book.parser->getBitDepth();
        return {ctx.page.width, ctx.page.height, ctx.depth};
      },
      [](void* opaque, size_t bitmap, size_t row, const XpBookXtcThumbnail* plan) {
        auto& ctx = *static_cast<Context*>(opaque);
        ctx.bitmap = bitmap;
        ctx.row = row;
        if (plan) {
          ctx.plan = *plan;
          LOG_DBG("XTC", "Generating thumb BMP: %dx%d -> %dx%d (scale: %.3f)", ctx.page.width, ctx.page.height,
                  plan->width, plan->height, plan->scale);
        }
        ctx.scratch = makeUniqueNoThrow<uint8_t[]>(bitmap + row);
        return bool(ctx.scratch);
      },
      [](void* opaque, uint32_t event, [[maybe_unused]] size_t bytes) {
        [[maybe_unused]] const auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<XpBookXtcDiagnostic>(event)) {
          case XP_BOOK_XTC_LOG_COVER_UNLOADED:
            LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
            break;
          case XP_BOOK_XTC_LOG_THUMB_UNLOADED:
            LOG_ERR("XTC", "Cannot generate thumb BMP, file not loaded");
            break;
          case XP_BOOK_XTC_LOG_NO_PAGES:
            LOG_ERR("XTC", "No pages in XTC file");
            break;
          case XP_BOOK_XTC_LOG_PAGE_INFO:
            LOG_DBG("XTC", "Failed to get first page info");
            break;
          case XP_BOOK_XTC_LOG_COPIED:
            LOG_DBG("XTC", "Copied cover to thumb (no scaling needed)");
            break;
          case XP_BOOK_XTC_LOG_COVER_OOM:
            LOG_ERR("XTC", "Failed to allocate cover buffers (%lu bytes)", static_cast<unsigned long>(bytes));
            break;
          case XP_BOOK_XTC_LOG_THUMB_OOM:
            LOG_ERR("XTC", "Failed to allocate thumb buffers (%lu bytes)", static_cast<unsigned long>(bytes));
            break;
          case XP_BOOK_XTC_LOG_COVER_READ:
            LOG_ERR("XTC", "Failed to load cover page");
            break;
          case XP_BOOK_XTC_LOG_THUMB_READ:
            LOG_ERR("XTC", "Failed to load cover page for thumb");
            break;
          case XP_BOOK_XTC_LOG_COVER_OPEN:
            LOG_DBG("XTC", "Failed to create cover BMP file");
            break;
          case XP_BOOK_XTC_LOG_THUMB_OPEN:
            LOG_DBG("XTC", "Failed to create thumb BMP file");
            break;
          case XP_BOOK_XTC_LOG_COVER_DONE:
            LOG_DBG("XTC", "Generated cover BMP: %s", ctx.book.getCoverBmpPath().c_str());
            break;
          case XP_BOOK_XTC_LOG_THUMB_DONE:
            LOG_DBG("XTC", "Generated thumb BMP (%dx%d): %s", ctx.plan.width, ctx.plan.height,
                    ctx.book.getThumbBmpPath(ctx.height).c_str());
            break;
        }
      }};
  return xp_book_xtc_generate(&services, thumb, height);
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  return xp_book_xtc_progress(loaded && bool(parser), currentPage, loaded && parser ? parser->getPageCount() : 0);
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
