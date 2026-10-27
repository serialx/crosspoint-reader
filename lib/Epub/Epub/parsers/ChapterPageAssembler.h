#pragma once
#include <xp_ffi.h>

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/Page.h"
#include "Epub/Paragraph.h"
#include "Epub/css/CssParser.h"

class Epub;
class GfxRenderer;

// Hardware and Page sink. Semantic and page-assembly state live in chapter_.
class ChapterPageAssembler {
  friend class ChapterHtmlSlimParser;

 public:
  using TableColumnMeasure = XpChapterMeasure;

 private:
  XpChapter* chapter_ = nullptr;
  XpChapterState* activeSemantics_ = nullptr;
  static const XpParaMetrics metrics_;
  static const XpChapterSink sink_;
  static const GfxRenderer& metricRenderer(void* context) {
    return static_cast<ChapterPageAssembler*>(context)->renderer;
  }
  static int32_t textAdvance(void*, int32_t, XpParaSpan, uint8_t, int8_t);
  static void prepareText(void*, int32_t, const XpParaSpan*, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t);
  bool stopRequested = false;
  uint8_t paragraphAlignment;
  bool embeddedStyle;
  uint8_t imageRendering;
  const CssParser* cssParser;
  std::string pendingAnchorId;
  std::vector<std::string> tocAnchors;
  std::deque<std::pair<std::string, uint16_t>> anchorData;
  GfxRenderer& renderer;
  int fontId;
  float lineCompression;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  std::shared_ptr<Epub> epub;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> completePageFn;
  std::function<void()> popupFn;
  bool extraParagraphSpacing;
  uint8_t paragraphIndentSpaces = 2;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  std::string contentBase;
  std::string imageBasePath;
  std::string imageSource_;
  std::string imageCache_;
  std::function<void(XpChapterSpan, uint16_t, uint16_t, uint32_t)> completePageBytesFn;

  bool ensureSemantics();
  XpChapterOptions chapterOptions() const;
  XpChapterState* semanticState() const { return activeSemantics_ ? activeSemantics_ : xp_chapter_state(chapter_); }
  uint32_t semanticQuery(XpChapterQuery query) const { return xp_chapter_query(semanticState(), query); }
  bool semanticOom() const { return semanticQuery(XP_CHAPTER_QUERY_OOM); }
  void beginAssembly() {
    stopRequested = false;
    xp_chapter_begin(chapter_);
  }
  bool finishAssembly() { return xp_chapter_finish(chapter_) != 0; }
  virtual void measureTableAtCurrentPosition() = 0;
  virtual uint8_t* entityBuffer(uint32_t length) = 0;
  uint8_t probeImage(XpChapterSpan src, int32_t& counter, bool& popup, uint16_t& width, uint16_t& height);
  uint8_t completeBytes(XpChapterSpan bytes, XpChapterPosition position);
  explicit ChapterPageAssembler(
      std::shared_ptr<Epub> epub, GfxRenderer& renderer, const int fontId, const float lineCompression,
      const bool extraParagraphSpacing, const uint8_t paragraphAlignment, const uint16_t viewportWidth,
      const uint16_t viewportHeight, const bool hyphenationEnabled, const bool focusReadingEnabled,
      const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>& completePageFn,
      const bool embeddedStyle, const std::string& contentBase, const std::string& imageBasePath,
      const uint8_t imageRendering = 0, std::vector<std::string> tocAnchors = {},
      const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr)

      : paragraphAlignment(paragraphAlignment),
        embeddedStyle(embeddedStyle),
        imageRendering(imageRendering),
        cssParser(cssParser),
        tocAnchors(std::move(tocAnchors)),
        renderer(renderer),
        fontId(fontId),
        lineCompression(lineCompression),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        epub(epub),
        completePageFn(completePageFn),
        popupFn(popupFn),
        extraParagraphSpacing(extraParagraphSpacing),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        contentBase(contentBase),
        imageBasePath(imageBasePath) {}
  ~ChapterPageAssembler();
};
