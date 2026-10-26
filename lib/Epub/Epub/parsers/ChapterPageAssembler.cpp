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

uint8_t ChapterPageAssembler::completePage(const XpChapterElement* elements, uint32_t count, const XpChapterLink* links,
                                           uint32_t linkCount, const XpChapterFootnote* footnotes,
                                           uint32_t footnoteCount, XpChapterPosition position) {
  auto page = makeUniqueNoThrow<Page>();
  if (page) page->elements.reserve(count);
  bool failed = !page;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& element = elements[i];
    const auto& data = element.content.data;
    std::unique_ptr<PageElement> output;
    switch (element.content.kind) {
      case 0: {
        std::unique_ptr<TextBlock> block(static_cast<TextBlock*>(data.payload.pointer));
        if (!failed) output = makeUniqueNoThrow<PageLine>(std::move(block), element.x, element.y);
        break;
      }
      case 1: {
        std::unique_ptr<ImageBlock> block(static_cast<ImageBlock*>(data.payload.pointer));
        if (!failed) output = makeUniqueNoThrow<PageImage>(std::move(block), element.x, element.y);
        break;
      }
      case 2:
        if (!failed)
          output = makeUniqueNoThrow<PageHorizontalRule>(data.rule.width, data.rule.thickness, element.x, element.y);
        break;
      case 3:
        if (!failed)
          output = makeUniqueNoThrow<PageDropCap>(data.cap.font, data.cap.scale256,
                                                  static_cast<EpdFontFamily::Style>(data.cap.style),
                                                  reinterpret_cast<const char*>(data.cap.text), element.x, element.y);
        break;
      case 4: {
        CssBorderSide sides[4];
        for (size_t side = 0; side < 4; ++side)
          sides[side] = {data.box.sides[side].width, static_cast<CssBorderStyle>(data.box.sides[side].style)};
        if (!failed)
          output = makeUniqueNoThrow<PageBorderBox>(data.box.width, data.box.height, sides, data.box.shaded, element.x,
                                                    element.y);
        break;
      }
    }
    if (output)
      page->elements.push_back(std::move(output));
    else
      failed = true;
  }
  if (failed) return 2;
  static_assert(sizeof(PageLink) == sizeof(XpChapterLink));
  static_assert(sizeof(FootnoteEntry) == sizeof(XpChapterFootnote));
  page->links.resize(linkCount);
  page->footnotes.resize(footnoteCount);
  if (linkCount) memcpy(page->links.data(), links, linkCount * sizeof(PageLink));
  if (footnoteCount) memcpy(page->footnotes.data(), footnotes, footnoteCount * sizeof(FootnoteEntry));
  completePageFn(std::move(page), position.paragraph, position.list, position.visible);
  return stopRequested ? 1 : 0;
}
ChapterPageAssembler::~ChapterPageAssembler() = default;
