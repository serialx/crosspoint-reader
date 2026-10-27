#include "ChapterPageAssembler.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <iterator>
#include <new>

#include "ChapterTableConstants.h"
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/VisibleTextUtils.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageDimsProbe.h"
#include "Epub/converters/ImageToFramebufferDecoder.h"
#include "Epub/htmlEntities.h"

uint8_t ChapterPageAssembler::probeImage(XpChapterSpan src, int32_t& counter, bool& popup, uint16_t& width,
                                         uint16_t& height) {
  auto* self = this;
  // Resolve the image path relative to the HTML file
  std::string resolvedPath = FsHelpers::normalisePath(
      FsHelpers::decodeUriEscapes(self->contentBase + std::string(reinterpret_cast<const char*>(src.data), src.len)));

  if (ImageDecoderFactory::isFormatSupported(resolvedPath)) {
    // Create a unique filename for the cached image
    std::string ext;
    size_t extPos = resolvedPath.rfind('.');
    if (extPos != std::string::npos) {
      ext = resolvedPath.substr(extPos);
    }
    std::string cachedImagePath = self->imageBasePath + std::to_string(counter++) + ext;

    {
      // Probe the dimensions from the entry's first bytes (early-aborted
      // inflate, a few KB) instead of extracting the whole image now —
      // extraction is deferred to the first render of the page (see
      // ImageBlock's lazy extractor). This is what keeps first-open of an
      // image-heavy chapter from stalling for seconds per image.
      ImageDimensions dims = {0, 0};
      ImageDimsProbe headerProbe;
      self->epub->readItemContentsToStream(resolvedPath, headerProbe, 1024, /*allowEarlyStop=*/true);
      bool gotDimensions = headerProbe.getDimensions(dims);

      if (!gotDimensions) {
        // Retry with framebuffer scratch when the heap cannot fit the inflate window.
        GfxRenderer::FrameBufferLoan probeLoan(self->renderer);
        ImageDimsProbe retryProbe;
        self->epub->readItemContentsToStream(resolvedPath, retryProbe, 1024, /*allowEarlyStop=*/true);
        gotDimensions = retryProbe.getDimensions(dims);
      }

      if (!gotDimensions) {
        // No header within the stream (rare) — fall back to extracting the
        // whole image and probing the file. That can take seconds, so
        // surface the indexing popup first (single-shot per parser).
        if (self->popupFn && !popup) {
          popup = true;
          self->popupFn();
        }
        HalFile cachedImageFile;
        bool extractSuccess = false;
        if (Storage.openFileForWrite("EHP", cachedImagePath, cachedImageFile)) {
          {
            // Same 32 KB inflate window as the probe; the popup is already up.
            GfxRenderer::FrameBufferLoan extractLoan(self->renderer);
            extractSuccess = self->epub->readItemContentsToStream(resolvedPath, cachedImageFile, 4096);
          }
          cachedImageFile.flush();
          cachedImageFile.close();
        }
        if (extractSuccess) {
          // Retry to absorb SD-card sync latency on slow cards, and to close
          // the silent-drop bug where a single getDimensions failure was fatal.
          ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(cachedImagePath);
          for (int attempt = 0; attempt < 3 && !gotDimensions; attempt++) {
            if (attempt > 0) {
              delay(50);  // Give a slow SD card time to finish syncing before retrying
            }
            gotDimensions = decoder && decoder->getDimensions(cachedImagePath, dims);
          }
        } else {
          LOG_ERR("EHP", "Failed to extract image");
        }
      }

      width = dims.width;
      height = dims.height;
      if (gotDimensions) {
        imageSource_ = std::move(resolvedPath);
        imageCache_ = std::move(cachedImagePath);
        return 2;
      }
      LOG_ERR("EHP", "Failed to get image dimensions");
      Storage.remove(cachedImagePath.c_str());
      return 1;
    }
  }
  return 0;
}

uint8_t ChapterPageAssembler::completeBytes(XpChapterSpan bytes, XpChapterPosition position) {
  if (completePageBytesFn) {
    completePageBytesFn(bytes, position.paragraph, position.list, position.visible);
  } else {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
    extern bool chapterTestFailPageAllocation();
    if (chapterTestFailPageAllocation()) return 2;
#endif
    auto page = Page::fromBytes(bytes.data, bytes.len, position.visible);
    if (!page) {
      LOG_ERR("CHP", "OOM: decoded page");
      return 2;
    }
    completePageFn(std::move(page), position.paragraph, position.list, position.visible);
  }
  return stopRequested ? 1 : 0;
}

ChapterPageAssembler::~ChapterPageAssembler() = default;
