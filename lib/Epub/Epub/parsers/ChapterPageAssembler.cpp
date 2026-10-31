#include "ChapterPageAssembler.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <Utf8.h>
#include <xp_book.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <iterator>
#include <new>
#include <optional>

#include "ChapterTableConstants.h"
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/VisibleTextUtils.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageDimsProbe.h"
#include "Epub/converters/ImageToFramebufferDecoder.h"
#include "Epub/htmlEntities.h"

struct ChapterPageAssembler::ProbeContext {
  ChapterPageAssembler& owner;
  XpChapterSpan src;
  int32_t& counter;
  bool& popup;
  uint16_t& width;
  uint16_t& height;
  std::string resolved, cached;
  ImageDimensions dimensions{0, 0};
  std::optional<ImageDimsProbe> probe;
  std::optional<HalFile> file;
  ImageToFramebufferDecoder* decoder = nullptr;
  ProbeContext(ChapterPageAssembler& owner, XpChapterSpan src, int32_t& counter, bool& popup, uint16_t& width,
               uint16_t& height)
      : owner(owner), src(src), counter(counter), popup(popup), width(width), height(height) {}
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<ProbeContext*>(raw);
    auto& self = c.owner;
    switch (static_cast<XpBookProbeOperation>(operation)) {
      case XP_BOOK_PROBE_RESOLVE: {
        c.resolved = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(
            self.contentBase + std::string(reinterpret_cast<const char*>(c.src.data), c.src.len)));
        break;
      }
      case XP_BOOK_PROBE_SUPPORTED: {
        return ImageDecoderFactory::isFormatSupported(c.resolved);
      }
      case XP_BOOK_PROBE_PREPARE_CACHE: {
        std::string ext;
        const size_t dot = c.resolved.rfind('.');
        if (dot != std::string::npos) ext = c.resolved.substr(dot);
        c.cached = self.imageBasePath + std::to_string(c.counter++) + ext;
        break;
      }
      case XP_BOOK_PROBE_PROBE: {
        c.probe.emplace();
        self.epub->readItemContentsToStream(c.resolved, *c.probe, 1024, true);
        return c.probe->getDimensions(c.dimensions);
      }
      case XP_BOOK_PROBE_RETRY_PROBE: {
        GfxRenderer::FrameBufferLoan loan(self.renderer);
        ImageDimsProbe probe;
        self.epub->readItemContentsToStream(c.resolved, probe, 1024, true);
        return probe.getDimensions(c.dimensions);
      }
      case XP_BOOK_PROBE_HAS_POPUP: {
        return bool(self.popupFn);
      }
      case XP_BOOK_PROBE_POPUP_SHOWN: {
        return c.popup;
      }
      case XP_BOOK_PROBE_SET_POPUP: {
        c.popup = true;
        break;
      }
      case XP_BOOK_PROBE_POPUP: {
        self.popupFn();
        break;
      }
      case XP_BOOK_PROBE_OPEN: {
        c.file.emplace();
        return Storage.openFileForWrite("EHP", c.cached, *c.file);
      }
      case XP_BOOK_PROBE_EXTRACT: {
        GfxRenderer::FrameBufferLoan loan(self.renderer);
        return self.epub->readItemContentsToStream(c.resolved, *c.file, 4096);
      }
      case XP_BOOK_PROBE_FLUSH: {
        c.file->flush();
        break;
      }
      case XP_BOOK_PROBE_CLOSE: {
        c.file->close();
        break;
      }
      case XP_BOOK_PROBE_DECODER: {
        c.decoder = ImageDecoderFactory::getDecoder(c.cached);
        return c.decoder != nullptr;
      }
      case XP_BOOK_PROBE_DIMENSIONS: {
        return c.decoder->getDimensions(c.cached, c.dimensions);
      }
      case XP_BOOK_PROBE_DELAY: {
        delay(argument);
        break;
      }
      case XP_BOOK_PROBE_STORE_SIZE: {
        c.width = c.dimensions.width;
        c.height = c.dimensions.height;
        break;
      }
      case XP_BOOK_PROBE_ACCEPT: {
        self.imageSource_ = std::move(c.resolved);
        self.imageCache_ = std::move(c.cached);
        break;
      }
      case XP_BOOK_PROBE_REMOVE: {
        return Storage.remove(c.cached.c_str());
      }
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t) {
    auto& c = *static_cast<ProbeContext*>(raw);
    switch (static_cast<XpBookProbeEvent>(event)) {
      case XP_BOOK_PROBE_EVENT_FOUND:
        LOG_DBG("EHP", "Found image: src=%.*s", static_cast<int>(c.src.len), reinterpret_cast<const char*>(c.src.data));
        break;
      case XP_BOOK_PROBE_EVENT_EXTRACT_FAILED:
        LOG_ERR("EHP", "Failed to extract image");
        break;
      case XP_BOOK_PROBE_EVENT_DIMENSIONS:
        LOG_DBG("EHP", "Image dimensions: %dx%d", c.dimensions.width, c.dimensions.height);
        break;
      case XP_BOOK_PROBE_EVENT_DIMENSIONS_FAILED:
        LOG_ERR("EHP", "Failed to get image dimensions");
        break;
    }
  }
};
uint8_t ChapterPageAssembler::probeImage(XpChapterSpan src, int32_t& counter, bool& popup, uint16_t& width,
                                         uint16_t& height) {
  ProbeContext context{*this, src, counter, popup, width, height};
  const XpBookServices io{&context, ProbeContext::operation, nullptr, ProbeContext::diagnostic};
  return xp_book_probe_image(&io);
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
