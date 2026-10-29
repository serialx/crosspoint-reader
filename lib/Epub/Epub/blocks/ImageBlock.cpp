#include "ImageBlock.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <cstring>
#include <new>

#include "../SectionCodec.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImagePipeline.h"

// Cache file format:
// - uint16_t width
// - uint16_t height
// - uint8_t pixels[...] - 2 bits per pixel, packed (4 pixels per byte), row-major order

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : ownedImagePath(imagePath),
      ownedSrcPath(srcPath),
      imagePath(ownedImagePath),
      srcPath(ownedSrcPath),
      width(width),
      height(height) {}

void* ImageBlock::extractCtx = nullptr;
ImageBlock::ExtractFn ImageBlock::extractFn = nullptr;

void ImageBlock::setExtractor(void* ctx, ExtractFn fn) {
  extractCtx = ctx;
  extractFn = fn;
}

namespace {

// Version 2 rebuilds pixels decoded by PNGdec's faulty zlib fast-copy path.
constexpr char PIXEL_CACHE_EXTENSION[] = ".px2";
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
uint8_t* renderReadWindow = nullptr;
size_t renderReadWindowSize = 0;
#endif

std::string getCachePath(const std::string_view imagePath) {
  // The filename versions decoded pixels without changing the packed format.
  size_t dotPos = imagePath.rfind('.');
  if (dotPos != std::string::npos) {
    return std::string(imagePath.substr(0, dotPos)) + PIXEL_CACHE_EXTENSION;
  }
  return std::string(imagePath) + PIXEL_CACHE_EXTENSION;
}

bool readValidCacheHeader(HalFile& cacheFile, const int expectedWidth, const int expectedHeight, uint16_t& cachedWidth,
                          uint16_t& cachedHeight) {
  if (cacheFile.read(&cachedWidth, 2) != 2 || cacheFile.read(&cachedHeight, 2) != 2) {
    return false;
  }

  const int widthDiff = abs(cachedWidth - expectedWidth);
  const int heightDiff = abs(cachedHeight - expectedHeight);
  if (widthDiff > 1 || heightDiff > 1) {
    return false;
  }

  const size_t bytesPerRow = (cachedWidth + 3) / 4;
  const size_t expectedSize = 4 + bytesPerRow * cachedHeight;
  return cacheFile.size() >= expectedSize;
}

// Suppress repeated failures across the BW/grayscale passes of one page render.
// Clear before the next page render so transient memory/storage failures retry.
constexpr size_t MAX_RENDER_IMAGE_FAILURES = 16;
uint64_t failedImageHashes[MAX_RENDER_IMAGE_FAILURES];
size_t failedImageCount = 0;

uint64_t imagePathHash(const std::string_view path) {
  uint64_t hash = 14695981039346656037ull;
  for (const char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool imageFailedThisRender(const std::string_view path) {
  const uint64_t hash = imagePathHash(path);
  for (size_t i = 0; i < failedImageCount; i++) {
    if (failedImageHashes[i] == hash) return true;
  }
  return false;
}

void rememberImageFailure(const std::string_view path) {
  if (failedImageCount == MAX_RENDER_IMAGE_FAILURES || imageFailedThisRender(path)) return;
  failedImageHashes[failedImageCount++] = imagePathHash(path);
}

// --- Per-page-render RAM slot for the pixel cache ----------------------------
// The tiled grayscale flow re-renders an image page once for the BW
// double-refresh and again for every band of both gray planes, and each pass
// re-read the whole .pxc off SD (~100 ms for a full-page image, ~13 passes).
// Column clipping cannot reduce the SD traffic: the row stride (~100 B) is
// smaller than an SD sector, so every sector is touched regardless of the band
// window. Instead the first pass loads the payload into RAM and later passes
// render from it. Chunked allocation because a single full-image block (up to
// 96 KB) rarely fits the fragmented mid-render heap; each chunk is heap-gated
// and any failure falls back to the streaming path unchanged. The reader
// releases the slot when the page render completes, so nothing stays resident
// across page turns.
constexpr size_t PXC_CHUNK_SHIFT = 14;  // 16 KB chunks
constexpr size_t PXC_CHUNK_SIZE = 1u << PXC_CHUNK_SHIFT;
constexpr size_t PXC_MAX_CHUNKS = 6;  // 96 KB: a full-screen 2bpp image
constexpr size_t PXC_HEAP_RESERVE = 24 * 1024;
constexpr size_t PXC_MAX_ALLOC_RESERVE = 8 * 1024;
// Rows can straddle a chunk boundary; they are reassembled into a stack
// buffer. (screenWidth + 3) / 4 caps at 200 B for an 800px panel.
constexpr int PXC_MAX_BYTES_PER_ROW = 208;

std::unique_ptr<uint8_t[]> pxcChunks[PXC_MAX_CHUNKS];
uint64_t pxcSlotHash = 0;
uint16_t pxcSlotWidth = 0;
uint16_t pxcSlotHeight = 0;

void releasePxcSlot() {
  for (auto& chunk : pxcChunks) chunk.reset();
  pxcSlotHash = 0;
  pxcSlotWidth = 0;
  pxcSlotHeight = 0;
}

const uint8_t* pxcRowPtr(size_t rowStart, int bytesPerRow, uint8_t* tempRow) {
  const size_t chunk = rowStart >> PXC_CHUNK_SHIFT;
  const size_t offset = rowStart & (PXC_CHUNK_SIZE - 1);
  if (offset + bytesPerRow <= PXC_CHUNK_SIZE) {
    return pxcChunks[chunk].get() + offset;
  }
  const size_t firstPart = PXC_CHUNK_SIZE - offset;
  memcpy(tempRow, pxcChunks[chunk].get() + offset, firstPart);
  memcpy(tempRow + firstPart, pxcChunks[chunk + 1].get(), bytesPerRow - firstPart);
  return tempRow;
}

// cacheFile is positioned just past the header. True when the slot holds the
// full pixel payload for this cache path afterward.
bool loadPxcSlot(uint64_t cacheHash, HalFile& cacheFile, uint16_t cachedWidth, uint16_t cachedHeight, int bytesPerRow) {
  releasePxcSlot();
  if (bytesPerRow > PXC_MAX_BYTES_PER_ROW) {
    return false;
  }
  size_t remaining = (size_t)bytesPerRow * cachedHeight;
  const size_t chunkCount = (remaining + PXC_CHUNK_SIZE - 1) >> PXC_CHUNK_SHIFT;
  if (chunkCount == 0 || chunkCount > PXC_MAX_CHUNKS) {
    return false;
  }
  for (size_t i = 0; i < chunkCount; i++) {
    const size_t want = remaining < PXC_CHUNK_SIZE ? remaining : PXC_CHUNK_SIZE;
    if (ESP.getFreeHeap() < remaining + PXC_HEAP_RESERVE || ESP.getMaxAllocHeap() < want + PXC_MAX_ALLOC_RESERVE) {
      releasePxcSlot();
      return false;
    }
    pxcChunks[i] = makeUniqueNoThrow<uint8_t[]>(want);
    if (!pxcChunks[i] || cacheFile.read(pxcChunks[i].get(), want) != static_cast<int>(want)) {
      releasePxcSlot();
      return false;
    }
    remaining -= want;
  }
  pxcSlotHash = cacheHash;
  pxcSlotWidth = cachedWidth;
  pxcSlotHeight = cachedHeight;
  return true;
}

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
// Native bands clip logical rows only in landscape; portrait retains the full row range.
std::pair<int, int> visibleCacheRows(GfxRenderer& renderer, int x, int y, int width, int height) {
  int first = 0, end = height;
  if (renderer.getWriteRows() < renderer.getDisplayHeight()) {
    while (first < end && !renderer.glyphIntersectsStrip(x, y + first, x + width - 1, y + first)) ++first;
    while (end > first && !renderer.glyphIntersectsStrip(x, y + end - 1, x + width - 1, y + end - 1)) --end;
  }
  return {first, end};
}
#endif

void renderRowsFromPxcSlot(GfxRenderer& renderer, int x, int y) {
  const int bytesPerRow = (pxcSlotWidth + 3) / 4;
  uint8_t tempRow[PXC_MAX_BYTES_PER_ROW];

  const auto target = imageWriteTarget(renderer);

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  const auto [firstRow, endRow] = visibleCacheRows(renderer, x, y, pxcSlotWidth, pxcSlotHeight);
  for (int row = firstRow; row < endRow; row++) {
#else
  for (int row = 0; row < pxcSlotHeight; row++) {
#endif
    const uint8_t* rowBuffer = pxcRowPtr((size_t)row * bytesPerRow, bytesPerRow, tempRow);
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
    if (target.rows < target.height) {
      xp_strip_packed_row(&target, x, y + row, pxcSlotWidth, rowBuffer, bytesPerRow);
    } else
#endif
    {
      xp_image_packed_row(&target, x, y + row, pxcSlotWidth, rowBuffer, bytesPerRow);
    }
  }
}

bool renderFromCache(GfxRenderer& renderer, const std::string& cachePath, int x, int y, int expectedWidth,
                     int expectedHeight) {
  // A later pass of the same page render: the payload is already in RAM, skip
  // the file entirely.
  const uint64_t cacheHash = imagePathHash(cachePath);
  if (pxcSlotHash == cacheHash && pxcSlotWidth != 0) {
    renderRowsFromPxcSlot(renderer, x, y);
    return true;
  }

  HalFile cacheFile;
  if (!Storage.openFileForRead("IMG", cachePath, cacheFile)) {
    return false;
  }

  uint16_t cachedWidth, cachedHeight;
  if (!readValidCacheHeader(cacheFile, expectedWidth, expectedHeight, cachedWidth, cachedHeight)) {
    LOG_ERR("IMG", "Invalid image cache: %s", cachePath.c_str());
    return false;
  }

  LOG_DBG("IMG", "Loading from cache: %s (%dx%d)", cachePath.c_str(), cachedWidth, cachedHeight);

  const int bytesPerRow = (cachedWidth + 3) / 4;  // 2 bits per pixel, 4 pixels per byte

  // First pass of a page render: try to pull the payload into the RAM slot so
  // the remaining ~12 passes skip SD entirely. Only an EMPTY slot is claimed:
  // the slot lives until the page render completes, so a populated slot with a
  // different hash means another image on this same page owns it. Evicting it
  // here would make 2+ image pages reload each other from SD on every pass
  // (all the SD traffic of streaming plus the slot alloc churn); instead later
  // images take the streaming path below, unchanged from pre-cache behavior.
  if (pxcSlotHash == 0 && loadPxcSlot(cacheHash, cacheFile, cachedWidth, cachedHeight, bytesPerRow)) {
    renderRowsFromPxcSlot(renderer, x, y);
    LOG_DBG("IMG", "Cache render complete (payload now in RAM)");
    return true;
  }

  // Streaming fallback (slot didn't fit). A failed slot load may have consumed
  // part of the payload; rewind to just past the header.
  cacheFile.seek(4);

  // Read several rows per SD access. A one-row-per-read loop here means
  // cachedHeight (~728) tiny reads through the storage mutex + SdFat; batching
  // rows into a ~4KB buffer cuts that to ~20 reads per pass without holding the
  // whole image.
  int rowsPerRead = 4096 / bytesPerRow;
  if (rowsPerRead < 1) rowsPerRead = 1;
  if (rowsPerRead > cachedHeight) rowsPerRead = cachedHeight;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  const bool borrowed = renderReadWindow && renderReadWindowSize >= static_cast<size_t>(bytesPerRow);
  if (borrowed) rowsPerRead = std::min(rowsPerRead, static_cast<int>(renderReadWindowSize / bytesPerRow));
  uint8_t* readBuffer = borrowed ? renderReadWindow : (uint8_t*)malloc((size_t)rowsPerRead * bytesPerRow);
#else
  constexpr bool borrowed = false;
  uint8_t* readBuffer = (uint8_t*)malloc((size_t)rowsPerRead * bytesPerRow);
#endif
  if (!readBuffer) {
    // Fall back to a single-row buffer under memory pressure.
    rowsPerRead = 1;
    readBuffer = (uint8_t*)malloc(bytesPerRow);
  }
  if (!readBuffer) {
    LOG_ERR("IMG", "Failed to allocate row buffer");
    return false;
  }

  const auto target = imageWriteTarget(renderer);

  int rowsInBuffer = 0;
  int bufferRow = 0;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  const auto [firstRow, endRow] = visibleCacheRows(renderer, x, y, cachedWidth, cachedHeight);
  if (!cacheFile.seek(4 + static_cast<size_t>(firstRow) * bytesPerRow)) {
    if (!borrowed) free(readBuffer);
    return false;
  }
  for (int row = firstRow; row < endRow; row++) {
#else
  for (int row = 0; row < cachedHeight; row++) {
#endif
    if (bufferRow >= rowsInBuffer) {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
      const int toRead = (endRow - row < rowsPerRead) ? (endRow - row) : rowsPerRead;
#else
      const int toRead = (cachedHeight - row < rowsPerRead) ? (cachedHeight - row) : rowsPerRead;
#endif
      const size_t bytes = (size_t)toRead * bytesPerRow;
      if (cacheFile.read(readBuffer, bytes) != static_cast<int>(bytes)) {
        LOG_ERR("IMG", "Cache read error at row %d", row);
        if (!borrowed) free(readBuffer);
        return false;
      }
      rowsInBuffer = toRead;
      bufferRow = 0;
    }

    const uint8_t* rowBuffer = readBuffer + (size_t)bufferRow * bytesPerRow;
    bufferRow++;

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
    if (target.rows < target.height) {
      xp_strip_packed_row(&target, x, y + row, cachedWidth, rowBuffer, bytesPerRow);
    } else
#endif
    {
      xp_image_packed_row(&target, x, y + row, cachedWidth, rowBuffer, bytesPerRow);
    }
  }

  if (!borrowed) free(readBuffer);
  LOG_DBG("IMG", "Cache render complete");
  return true;
}

}  // namespace

bool ImageBlock::hasValidCache() const {
  const auto cachePath = getCachePath(imagePath);
  HalFile cacheFile;
  if (!Storage.openFileForRead("IMG", cachePath, cacheFile)) {
    return false;
  }

  uint16_t cachedWidth, cachedHeight;
  return readValidCacheHeader(cacheFile, width, height, cachedWidth, cachedHeight);
}

bool ImageBlock::needsDecode() const { return !imageFailedThisRender(imagePath) && !hasValidCache(); }

void ImageBlock::clearRenderFailures() { failedImageCount = 0; }

void ImageBlock::releaseRenderCache() { releasePxcSlot(); }
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
void ImageBlock::setRenderReadWindow(uint8_t* data, size_t size) {
  renderReadWindow = data;
  renderReadWindowSize = size;
}
#endif

void ImageBlock::renderPlaceholder(GfxRenderer& renderer, const int x, const int y) const {
  renderer.fillRect(x, y, width, height, true);
  if (width > 2 && height > 2) {
    renderer.fillRect(x + 1, y + 1, width - 2, height - 2, false);
  }
}

void ImageBlock::render(GfxRenderer& renderer, const int x, const int y) {
  // The font-prewarm scan pass only accumulates glyphs; an image contributes
  // none, and its raw image output bypasses the renderer's scan-mode
  // suppression, so it would otherwise do a full (discarded) cache render every
  // page view. Skip it here. The image still draws in the real BW/grayscale
  // passes; on first view this just moves the one-time decode to the BW pass.
  FontCacheManager* fcm = renderer.getFontCacheManager();
  if (fcm && fcm->isScanning()) return;

  LOG_DBG("IMG", "Rendering image at %d,%d: %s (%dx%d)", x, y, imagePath.data(), width, height);

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // Bounds check render position using logical screen dimensions
  if (x < 0 || y < 0 || x + width > screenWidth || y + height > screenHeight) {
    LOG_ERR("IMG", "Invalid render position: (%d,%d) size (%dx%d) screen (%dx%d)", x, y, width, height, screenWidth,
            screenHeight);
    return;
  }

  // Tiled grayscale (#2190): skip the whole image when it doesn't touch the
  // active band. The per-pixel writer already clips off-band pixels, but without
  // this each of the ~7 bands per plane re-ran the full cache load / pixel walk
  // and discarded the result — the dominant cost of AA on image pages. The check
  // is orientation-aware and returns true when no strip is active, so the BW
  // pass and non-tiled controllers render the image exactly as before.
  if (!renderer.glyphIntersectsStrip(x, y, x + width - 1, y + height - 1)) {
    return;
  }

  if (imageFailedThisRender(imagePath)) {
    renderPlaceholder(renderer, x, y);
    return;
  }

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  if (renderReadWindow && pxcSlotWidth) {
    // Test the resident slot before constructing a temporary cache pathname.
    uint64_t hash = imagePathHash(imagePath.substr(0, imagePath.rfind('.')));
    for (const char c : std::string_view(PIXEL_CACHE_EXTENSION)) {
      hash = (hash ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    }
    if (hash == pxcSlotHash) {
      renderRowsFromPxcSlot(renderer, x, y);
      renderer.preserveImagePolarity(x, y, width, height);
      return;
    }
  }
#endif
  // Try to render from cache first
  std::string cachePath = getCachePath(imagePath);
  if (renderFromCache(renderer, cachePath, x, y, width, height)) {
    renderer.preserveImagePolarity(x, y, width, height);
    return;  // Successfully rendered from cache
  }

  // The build only header-probed the image for dimensions; pull the actual
  // file out of the book now, on first visit to the page.
  if (!srcPath.empty() && extractFn && !Storage.exists(imagePath.data())) {
    LOG_DBG("IMG", "Lazy-extracting %s -> %s", srcPath.data(), imagePath.data());
    if (!extractFn(extractCtx, srcPath.data(), imagePath.data())) {
      LOG_ERR("IMG", "Lazy extraction failed: %s", srcPath.data());
    }
  }

  // No cache - need to decode the image
  // Check if image file exists
  HalFile file;
  if (!Storage.openFileForRead("IMG", imagePath.data(), file)) {
    LOG_ERR("IMG", "Image file not found: %s", imagePath.data());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }
  size_t fileSize = file.size();
  file.close();

  if (fileSize == 0) {
    LOG_ERR("IMG", "Image file is empty: %s", imagePath.data());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Decoding and caching: %s", imagePath.data());

  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = width;
  config.maxHeight = height;
  config.useGrayscale = true;
  config.useDithering = true;
  config.performanceMode = false;
  config.useExactDimensions = true;  // Use pre-calculated dimensions to avoid rounding mismatches
  config.cachePath = cachePath;      // Enable caching during decode

  const std::string decodePath(imagePath);
  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(decodePath);
  if (!decoder) {
    LOG_ERR("IMG", "No decoder found for image: %s", imagePath.data());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  LOG_DBG("IMG", "Using %s decoder", decoder->getFormatName());

  bool success = decoder->decodeToFramebuffer(decodePath, renderer, config);
  if (!success) {
    LOG_ERR("IMG", "Failed to decode image: %s", imagePath.data());
    rememberImageFailure(imagePath);
    renderPlaceholder(renderer, x, y);
    return;
  }

  renderer.preserveImagePolarity(x, y, width, height);
  LOG_DBG("IMG", "Decode successful");
}
