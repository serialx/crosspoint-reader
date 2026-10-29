#include "DictHtmlPages.h"

#include <Arduino.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <xp_ffi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"

namespace {
constexpr const char* TMP_HTML_PATH = "/.crosspoint/dicthtml.tmp";
constexpr size_t MAX_STYLED_PAGES = 64;

bool writeNormalizedXhtml(const std::string& html, HalFile& file) {
  return xp_dictionary_html(
      reinterpret_cast<const uint8_t*>(html.data()), html.size(), &file,
      [](void* ctx, const uint8_t* data, size_t size) { return static_cast<HalFile*>(ctx)->write(data, size); });
}

}  // namespace

bool buildDictionaryHtmlPages(GfxRenderer& renderer, const std::string& definition, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, std::vector<std::unique_ptr<Page>>& pagesOut) {
  if (!xp_dictionary_html_ready(ESP.getFreeHeap(), ESP.getMaxAllocHeap())) {
    LOG_ERR("DHTML", "Low heap for styled definition (%u free, %u max block)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return false;
  }

  {
    HalFile tmp = Storage.open(TMP_HTML_PATH, O_WRITE | O_CREAT | O_TRUNC);
    if (!tmp) {
      LOG_ERR("DHTML", "Cannot create %s", TMP_HTML_PATH);
      return false;
    }
    if (!writeNormalizedXhtml(definition, tmp)) {
      LOG_ERR("DHTML", "Short write to %s", TMP_HTML_PATH);
      return false;
    }
  }  // destructor closes the file before the parser reopens the same path

  pagesOut.clear();
  pagesOut.reserve(MAX_STYLED_PAGES);

  bool ok = false;
  const char* limitReason = nullptr;
  size_t retainedElements = 0;
  {
    const std::string tmpPath = TMP_HTML_PATH;  // the parser stores a reference
    std::unique_ptr<ChapterHtmlSlimParser> parser;
    parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
        nullptr, tmpPath, renderer, SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
        SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
        SETTINGS.hyphenationEnabled, SETTINGS.focusReadingEnabled,
        [&pagesOut, &parser, &retainedElements, &limitReason](std::unique_ptr<Page> page, uint16_t, uint16_t,
                                                              uint32_t) {
          if (limitReason) return;
          const size_t pageElements = page->elementCount();
          static constexpr const char* reasons[] = {nullptr, "page count", "element count", "free heap"};
          limitReason = reasons[xp_dictionary_html_limit(pagesOut.size(), retainedElements, pageElements,
                                                         ESP.getFreeHeap(), ESP.getMaxAllocHeap())];
          if (limitReason != nullptr) {
            LOG_ERR("DHTML", "Styled definition stopped on %s (pages=%u elements=%u free=%u contig=%u)", limitReason,
                    static_cast<unsigned>(pagesOut.size()), static_cast<unsigned>(retainedElements + pageElements),
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            pagesOut.clear();
            parser->requestStop();
            return;
          }
          retainedElements += pageElements;
          pagesOut.push_back(std::move(page));
        },
        /*embeddedStyle=*/false, /*contentBase=*/"", /*imageBasePath=*/"", /*imageRendering=*/2);
    if (!parser) {
      LOG_ERR("DHTML", "OOM: ChapterHtmlSlimParser");
    } else {
      parser->setTextSpacing(SETTINGS.getCharacterSpacing(), SETTINGS.wordSpacing);
      parser->setParagraphIndentSpaces(SETTINGS.paragraphIndentSpaces);
      ok = parser->parseAndBuildPages();  // closes the file on both outcomes
    }
  }
  Storage.remove(TMP_HTML_PATH);

  if (limitReason) {
    LOG_ERR("DHTML", "Styled definition exceeded page heap budget");
  }
  if (!ok || limitReason || pagesOut.empty()) {
    if (!ok && !limitReason) {
      LOG_ERR("DHTML", "Definition parse/layout failed");
    } else if (!limitReason && pagesOut.empty()) {
      LOG_ERR("DHTML", "Definition produced no styled pages");
    }
    pagesOut.clear();
    return false;
  }
  return true;
}
