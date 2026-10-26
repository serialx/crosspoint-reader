#pragma once

#include <HalStorage.h>
#include <xp_ffi.h>

#include "ChapterPageAssembler.h"

class ChapterHtmlSlimParser final : private ChapterPageAssembler {
  const std::string& filepath;
  std::unique_ptr<uint8_t[]> parseBuffer_;
  size_t parseBufferCapacity_ = 0;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;
  bool measureTableColumns(HalFile& file, TableColumnMeasure& out) const;
  void measureTableAtCurrentPosition() override;
  uint8_t* entityBuffer(uint32_t length) override;

 public:
  explicit ChapterHtmlSlimParser(
      std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer, const int fontId,
      const float lineCompression, const bool extraParagraphSpacing, const uint8_t paragraphAlignment,
      const uint16_t viewportWidth, const uint16_t viewportHeight, const bool hyphenationEnabled,
      const bool focusReadingEnabled,
      const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>& completePageFn,
      const bool embeddedStyle, const std::string& contentBase, const std::string& imageBasePath,
      const uint8_t imageRendering = 0, std::vector<std::string> tocAnchors = {},
      const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr)

      : ChapterPageAssembler(epub, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment,
                             viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, completePageFn,
                             embeddedStyle, contentBase, imageBasePath, imageRendering, std::move(tocAnchors), popupFn,
                             cssParser),
        filepath(filepath) {}
  ~ChapterHtmlSlimParser();
  void setTextSpacing(const int8_t character, const uint8_t wordPercent) {
    characterSpacing = character;
    wordSpacingPercent = wordPercent;
  }
  void setParagraphIndentSpaces(const uint8_t spaces) { paragraphIndentSpaces = spaces; }

  // One-shot parse: builds every page before returning (begin + step* + finish).
  bool parseAndBuildPages();

  // Resumable parse, for the incremental section builder. Drive as:
  //   if (!beginParse()) fail;
  //   loop: switch (parseStep()) { More: keep going / yield; Done: finishParse(); Error: abortParse(); }
  // Pages are emitted via completePageFn as they complete during parseStep(), so
  // the caller can stop once enough pages are built and resume on a later tick.
  enum class ParseStatus { More, Done, Error };
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();  // flush the trailing page and tear down; false on OOM/stop
  void abortParse();   // tear down without flushing (error / abandon)
  // Safe from completePageFn; cleanup waits until the active callback returns.
  void requestStop();

  void addLineToPage(std::unique_ptr<TextBlock> line, uint32_t visibleOffset) {
    ChapterPageAssembler::addLineToPage(std::move(line), visibleOffset);
  }
  const std::deque<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }

  // Byte progress of the in-flight parse, used to estimate a still-building section's total page
  // count (a giant single-spine book never fully lays out, so its real count is unknown). Valid
  // between beginParse() and finishParse()/abortParse().
  size_t parseBytesConsumed() { return parseFile_ ? parseFile_.position() : 0; }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
};
