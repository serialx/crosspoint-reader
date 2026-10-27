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

#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
extern bool chapterTestFailPageAllocation();
#endif
namespace {
template <typename T, typename... Args>
std::unique_ptr<T> makePageObject(Args&&... args) {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
  const bool fail = chapterTestFailPageAllocation();
  auto result = fail ? nullptr : makeUniqueNoThrow<T>(std::forward<Args>(args)...);
#else
  auto result = makeUniqueNoThrow<T>(std::forward<Args>(args)...);
#endif
  if (!result) LOG_ERR("CHP", "OOM: page object %u bytes", static_cast<uint32_t>(sizeof(T)));
  return result;
}
}  // namespace

namespace {
BlockStyle decodedStyle(const XpSecRecord& record) {
  const auto* v = record.values;
  BlockStyle s;
  s.alignment = static_cast<CssTextAlign>(v[0]);
  s.textAlignDefined = v[1];
  s.marginTop = v[2];
  s.marginBottom = v[3];
  s.marginLeft = v[4];
  s.marginRight = v[5];
  s.paddingTop = v[6];
  s.paddingBottom = v[7];
  s.paddingLeft = v[8];
  s.paddingRight = v[9];
  s.textIndent = v[10];
  s.textIndentDefined = v[11];
  s.isRtl = v[12];
  s.directionDefined = v[13];
  s.characterSpacing = v[14];
  s.fontId = v[15];
  return s;
}
std::string pageString(XpPageSpan s) { return {reinterpret_cast<const char*>(s.data), s.len}; }
std::unique_ptr<TextBlock> decodedLine(const uint8_t* arena, size_t size, uint16_t element) {
  XpPageLine line{};
  if (xp_page_line(arena, size, element, &line)) return nullptr;
  const auto style = decodedStyle(line.block);
  std::vector<std::string> words, ruby;
  std::vector<int16_t> positions;
  std::vector<EpdFontFamily::Style> styles;
  std::vector<uint8_t> boundaries, slots;
  std::vector<uint16_t> suffixes;
  std::vector<TextBlock::SourceRange> sources;
  words.reserve(line.count);
  positions.reserve(line.count);
  styles.reserve(line.count);
  sources.reserve(line.count);
  if (line.has_ruby) ruby.reserve(line.count);
  if (line.has_focus) {
    boundaries.reserve(line.count);
    suffixes.reserve(line.count);
  }
  if (line.has_fonts) slots.reserve(line.count);
  for (uint16_t i = 0; i < line.count; ++i) {
    XpPageWord word{};
    if (xp_page_word(arena, size, element, i, 0, &word)) return nullptr;
    words.push_back(pageString(word.text));
    positions.push_back(word.x);
    styles.push_back(
        static_cast<EpdFontFamily::Style>((word.style & ~EpdFontFamily::SMALL_CAPS) |
                                          (word.discretionary_hyphen ? TextBlock::DISCRETIONARY_HYPHEN_FLAG : 0)));
    sources.push_back({word.source_start, word.source_end});
    if (line.has_ruby) ruby.push_back(pageString(word.ruby));
    if (line.has_focus) {
      boundaries.push_back(word.focus_boundary);
      suffixes.push_back(word.focus_suffix);
    }
    if (line.has_fonts) {
      uint8_t slot = 0;
      if (word.font != style.fontId) {
        for (uint8_t j = 0; j < TextBlock::MAX_WORD_FONTS; ++j)
          if (line.fonts[j] == word.font) {
            slot = j + 1;
            break;
          }
      }
      slots.push_back(slot | (word.small_caps ? TextBlock::SMALL_CAPS_SLOT_FLAG : 0));
    }
  }
  auto block = makeUniqueNoThrow<TextBlock>(words, positions, styles, boundaries, suffixes, style, std::move(ruby),
                                            std::vector<TextBlock::LinkSpan>{}, sources, line.paragraph_start);
  if (!block || !block->valid() || (line.has_fonts && !block->setWordFonts(slots.data(), line.fonts))) return nullptr;
  return block;
}
}  // namespace

// Dictionary definitions retain Page objects; section builds consume bytes directly.
std::unique_ptr<Page> ChapterPageAssembler::decodePage(XpChapterSpan bytes) {
  XpPageInfo info{};
  if (xp_page_arena_size(bytes.data, bytes.len, &info)) return nullptr;
  auto arena = makeUniqueNoThrow<uint8_t[]>(info.arena_size);
  if (!arena || xp_page_decode(bytes.data, bytes.len, 0, arena.get(), info.arena_size, &info)) return nullptr;
  auto page = makePageObject<Page>();
  if (!page) return nullptr;
  page->elements.reserve(info.elements);
  for (uint16_t i = 0; i < info.elements; ++i) {
    XpPageElement e{};
    if (xp_page_element(arena.get(), info.arena_size, i, &e)) return nullptr;
    const auto* v = e.record.values;
    const int16_t x = v[0], y = v[1];
    std::unique_ptr<PageElement> element;
    switch (static_cast<PageElementTag>(e.tag)) {
      case TAG_PageLine: {
        auto line = decodedLine(arena.get(), info.arena_size, i);
        if (!line) return nullptr;
        element = makePageObject<PageLine>(std::move(line), x, y);
        break;
      }
      case TAG_PageImage: {
        auto image = makeUniqueNoThrow<ImageBlock>(pageString(e.first), pageString(e.second), e.width, e.height);
        if (!image) return nullptr;
        element = makePageObject<PageImage>(std::move(image), x, y);
        break;
      }
      case TAG_PageHorizontalRule:
        element = makePageObject<PageHorizontalRule>(v[2], v[3], x, y);
        break;
      case TAG_PageDropCap: {
        char text[13]{};
        memcpy(text, e.first.data, std::min(e.first.len, sizeof(text) - 1));
        element = makePageObject<PageDropCap>(static_cast<int32_t>(v[2]), v[3], static_cast<EpdFontFamily::Style>(v[4]),
                                              text, x, y);
        break;
      }
      case TAG_PageBorderBox: {
        CssBorderSide sides[4];
        for (size_t side = 0; side < 4; ++side)
          sides[side] = {static_cast<uint8_t>(v[4 + side * 2]), static_cast<CssBorderStyle>(v[5 + side * 2])};
        element = makePageObject<PageBorderBox>(v[2], v[3], sides, v[12] != 0, x, y);
        break;
      }
    }
    if (!element) return nullptr;
    page->elements.push_back(std::move(element));
  }
  page->footnotes.reserve(info.footnotes);
  for (uint16_t i = 0; i < info.footnotes; ++i) {
    XpPageSpan number{}, href{};
    if (xp_page_footnote(arena.get(), info.arena_size, i, &number, &href)) return nullptr;
    FootnoteEntry note{};
    memcpy(note.number, number.data, sizeof(note.number));
    memcpy(note.href, href.data, sizeof(note.href));
    page->footnotes.push_back(note);
  }
  page->links.reserve(info.links);
  for (uint16_t i = 0; i < info.links; ++i) {
    XpPageLink value{};
    if (xp_page_link(arena.get(), info.arena_size, i, &value)) return nullptr;
    PageLink link{};
    memcpy(link.href, value.href.data, sizeof(link.href));
    link.x = value.x;
    link.y = value.y;
    link.width = value.width;
    link.height = value.height;
    page->links.push_back(link);
  }
  return page;
}

uint8_t ChapterPageAssembler::completeBytes(XpChapterSpan bytes, XpChapterPosition position) {
  if (completePageBytesFn) {
    completePageBytesFn(bytes, position.paragraph, position.list, position.visible);
  } else {
    auto page = decodePage(bytes);
    if (!page) {
      LOG_ERR("CHP", "OOM: decoded page");
      return 2;
    }
    completePageFn(std::move(page), position.paragraph, position.list, position.visible);
  }
  return stopRequested ? 1 : 0;
}

ChapterPageAssembler::~ChapterPageAssembler() = default;
