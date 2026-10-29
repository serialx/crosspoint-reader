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

#include "../Memory/Memory.h"

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
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      Storage.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  Storage.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
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

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  // Already generated
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  // Get bit depth
  const uint8_t bitDepth = parser->getBitDepth();

  // Allocate buffer for page data
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two column-major bit planes; each column occupies ceil(height/8) bytes
  const size_t bitmapSize = xp_xtc_bitmap_size(pageInfo.width, pageInfo.height, bitDepth);
  const uint32_t rowSize2 = ((static_cast<uint32_t>(pageInfo.width) * 2 + 31) / 32) * 4;
  const size_t rowBufferSize = (bitDepth == 2) ? rowSize2 : 0;
  auto coverScratch = makeUniqueNoThrow<uint8_t[]>(bitmapSize + rowBufferSize);
  if (!coverScratch) {
    LOG_ERR("XTC", "Failed to allocate cover buffers (%lu bytes)",
            static_cast<unsigned long>(bitmapSize + rowBufferSize));
    return false;
  }
  uint8_t* pageBuffer = coverScratch.get();
  uint8_t* rowBuffer = coverScratch.get() + bitmapSize;

  // Load first page (cover)
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page");
    return false;
  }

  // Create BMP file
  HalFile coverBmp;
  if (!Storage.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
    LOG_DBG("XTC", "Failed to create cover BMP file");
    return false;
  }

  xp_book_xtc_image(
      pageBuffer, bitmapSize, pageInfo.width, pageInfo.height, bitDepth, rowBuffer, rowBufferSize, nullptr, &coverBmp,
      [](void* ctx, const uint8_t* bytes, size_t size) { return static_cast<HalFile*>(ctx)->write(bytes, size); },
      nullptr);
  LOG_DBG("XTC", "Generated cover BMP: %s", getCoverBmpPath().c_str());
  return true;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Xtc::generateThumbBmp(int height) const {
  // Already generated
  if (Storage.exists(getThumbBmpPath(height).c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate thumb BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  // Get bit depth
  const uint8_t bitDepth = parser->getBitDepth();

  XpBookXtcThumbnail plan;
  xp_book_xtc_thumbnail(pageInfo.width, pageInfo.height, height, &plan);
  const float scale = plan.scale;

  // Only scale down, never up
  if (scale >= 1.0f) {
    // Page is already small enough, just use cover.bmp
    // Copy cover.bmp to thumb.bmp
    if (generateCoverBmp()) {
      HalFile src, dst;
      if (Storage.openFileForRead("XTC", getCoverBmpPath(), src)) {
        if (Storage.openFileForWrite("XTC", getThumbBmpPath(height), dst)) {
          uint8_t buffer[512];
          struct CopyContext {
            HalFile& source;
            HalFile& target;
          } copy{src, dst};
          if (!xp_book_copy_available(
                  buffer, sizeof(buffer), &copy,
                  [](void* ctx) { return static_cast<CopyContext*>(ctx)->source.available() != 0; },
                  [](void* ctx, uint8_t* bytes, size_t size) -> int32_t {
                    return static_cast<CopyContext*>(ctx)->source.read(bytes, size);
                  },
                  [](void* ctx, const uint8_t* bytes, size_t size) {
                    return static_cast<CopyContext*>(ctx)->target.write(bytes, size);
                  }))
            return false;
        }
      }
      LOG_DBG("XTC", "Copied cover to thumb (no scaling needed)");
      return Storage.exists(getThumbBmpPath(height).c_str());
    }
    return false;
  }

  const uint16_t thumbWidth = plan.width;
  const uint16_t thumbHeight = plan.height;

  LOG_DBG("XTC", "Generating thumb BMP: %dx%d -> %dx%d (scale: %.3f)", pageInfo.width, pageInfo.height, thumbWidth,
          thumbHeight, scale);

  // Allocate buffer for page data
  const size_t bitmapSize = xp_xtc_bitmap_size(pageInfo.width, pageInfo.height, bitDepth);
  const uint32_t rowSize = (thumbWidth + 31) / 32 * 4;
  auto thumbScratch = makeUniqueNoThrow<uint8_t[]>(bitmapSize + rowSize);
  if (!thumbScratch) {
    LOG_ERR("XTC", "Failed to allocate thumb buffers (%lu bytes)", static_cast<unsigned long>(bitmapSize + rowSize));
    return false;
  }
  uint8_t* pageBuffer = thumbScratch.get();
  uint8_t* rowBuffer = thumbScratch.get() + bitmapSize;

  // Load first page (cover)
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page for thumb");
    return false;
  }

  // Create thumbnail BMP file - use 1-bit format for fast home screen rendering (no gray passes)
  HalFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", getThumbBmpPath(height), thumbBmp)) {
    LOG_DBG("XTC", "Failed to create thumb BMP file");
    return false;
  }

  xp_book_xtc_image(
      pageBuffer, bitmapSize, pageInfo.width, pageInfo.height, bitDepth, rowBuffer, rowSize, &plan, &thumbBmp,
      [](void* ctx, const uint8_t* bytes, size_t size) { return static_cast<HalFile*>(ctx)->write(bytes, size); },
      [](void*) { vTaskDelay(1); });
  LOG_DBG("XTC", "Generated thumb BMP (%dx%d): %s", thumbWidth, thumbHeight, getThumbBmpPath(height).c_str());
  return true;
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
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
