#pragma once

#include <Memory.h>

#include <array>
#include <climits>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ChapterSemantics.h"
#include "Epub/FootnoteEntry.h"
#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"

class Page;
class GfxRenderer;
class Epub;

class ChapterPageAssembler : private ChapterSemantics {
  friend class ChapterHtmlSlimParser;
  static constexpr size_t MAX_CARRIED_LINES = 6;
  struct BoxScope {
    int depth = 0;
    int16_t left = 0;
    int16_t right = 0;
    int16_t padTop = 0;  // outer edge to content, border included
    int16_t padBottom = 0;
    CssBorderSide sides[4];  // top, right, bottom, left
    bool shaded = false;
    int16_t top = -1;  // content extent on the current page
    int16_t bottom = -1;
    bool continued = false;  // sliced onto an earlier page
  };
  GfxRenderer& renderer;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  const ParsedText* layoutParagraph = nullptr;
  bool layoutParagraphHasDropCap = false;
  uint8_t paragraphLinesOnPage = 0;
  uint8_t keepWithNextLines = 0;                       // trailing heading lines on this page
  uint32_t recentLineOffsets[MAX_CARRIED_LINES] = {};  // visible offsets of this page's last lines, newest last
  uint8_t recentLineCount = 0;
  static constexpr size_t MAX_BOX_SCOPES = 8;
  std::vector<BoxScope> boxScopes;
  void noteContent(int top, int bottom);
  void emitBoxSegment(const BoxScope& box, bool closing);
  int fontIdForScale(float scale) const;
  int prepareBlockFont(ParsedText& text, float scale);
  size_t linesToCarry(size_t& paragraphLines) const;
  size_t keepWithNextCarry() const;
  bool canCarry(size_t carry) const;

  std::shared_ptr<Epub> epub;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
  bool imagePopupFired = false;   // popupFn fired for the first image probe (single-shot)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  bool extraParagraphSpacing;
  uint8_t paragraphIndentSpaces = 2;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;

  // Page-break carry-over: a paragraph's orphan/widow lines and a heading kept with what
  // follows move to the next page instead of being left behind.
  void breakPageCarryingLines(size_t carry, size_t paragraphLines, uint32_t visibleOffset);
  // ponytail: buffer at most 160 tokens / 2 KB per row; larger rows use stacked flow.
  bool tableRowStacked = false;
  bool tableRowRtl = false;
  size_t tableRowTextBytes = 0;
  std::vector<std::unique_ptr<ParsedText>> tableRowCells;
  std::array<std::vector<std::unique_ptr<TextBlock>>, MAX_GRID_TABLE_COLUMNS> tableCellLines;
  std::vector<uint32_t> tableLineVisibleOffsets;

  int completedPageCount = 0;
  // Canonical reading-position counter: zero-based Unicode codepoints in visible
  // <body> text. Token offsets flow through line breaking so every completed page
  // records the first source character it renders.
  uint32_t currentPageVisibleOffset = 0;
  bool currentPageVisibleOffsetSet = false;

  // Footnote link tracking
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;

  void startNewTextBlock(const BlockStyle& blockStyle, bool paragraphEnd = true) override;
  void flushPendingAnchor() override;
  void applyParagraphDirection(bool defined, bool rtl) override;
  bool paragraphEmpty() const override { return currentTextBlock->isEmpty(); }
  size_t paragraphSize() const override { return currentTextBlock->size(); }
  BlockStyle& paragraphStyle() override { return currentTextBlock->getBlockStyle(); }
  void suppressFirstLineIndent() override { return currentTextBlock->suppressFirstLineIndent(); }
  void ensureRubyCapacity() override { return currentTextBlock->ensureRubyCapacity(); }
  uint8_t addLinkTarget(const char* href) override { return currentTextBlock->addLinkTarget(href); }
  bool linkTargetMatches(uint8_t id, const char* href) const override {
    return currentTextBlock->linkTargetMatches(id, href);
  }
  uint8_t sizeSlotFor(float scale) override { return currentTextBlock->sizeSlotFor(scale); }
  void setParagraphStyle(const BlockStyle& style) override { return currentTextBlock->setBlockStyle(style); }
  EpdFontFamily::Style getWordStyleAt(size_t index) const override { return currentTextBlock->getWordStyleAt(index); }
  std::string getRubyTextAt(size_t index) const override { return currentTextBlock->getRubyTextAt(index); }
  void setRubyGroupAt(size_t index, size_t count, const std::string& text) override {
    return currentTextBlock->setRubyGroupAt(index, count, text);
  }
  void setRubyForWordAt(size_t index, const std::string& text) override {
    return currentTextBlock->setRubyForWordAt(index, text);
  }
  void addWord(const char* word, EpdFontFamily::Style style, bool underline, bool attach, uint32_t offset, uint8_t link,
               uint8_t slot, bool noSpace) override {
    return currentTextBlock->addWord(word, style, underline, attach, offset, link, slot, noSpace);
  }
  bool paragraphExists() const override { return currentTextBlock != nullptr; }
  void createParagraph(const BlockStyle& style) override {
    currentTextBlock =
        makeUniqueNoThrow<ParsedText>(hyphenationEnabled, focusReadingEnabled, style, paragraphIndentSpaces);
  }
  void resetParagraph() override { currentTextBlock.reset(); }
  bool bodyIsSdFont() const override;
  int bodySpaceWidth() const override;
  void prepareBodyText(const char* text, uint8_t styleMask) const override;
  int bodyTextAdvance(const char* text, EpdFontFamily::Style style) const override;
  int bodyAscender() const override;
  uint16_t pageWidth() const override { return viewportWidth; }
  uint16_t pageHeight() const override { return viewportHeight; }
  void clearTableCells() override { tableRowCells.clear(); }
  void reserveTableCells() override { tableRowCells.reserve(MAX_GRID_TABLE_COLUMNS); }
  size_t bufferedTableWords() const override {
    size_t words = 0;
    for (const auto& cell : tableRowCells) words += cell->size();
    return words;
  }
  bool tableStacked() const override { return tableRowStacked; }
  void setTableStacked(bool value) override { tableRowStacked = value; }
  void setTableRtl(bool value) override { tableRowRtl = value; }
  void resetTableBytes() override { tableRowTextBytes = 0; }
  size_t tableBytes() const override { return tableRowTextBytes; }
  void addTableBytes(size_t count) override { tableRowTextBytes += count; }
  void resetExtractedWords() override { wordsExtractedInBlock = 0; }
  int extractedWords() const override { return wordsExtractedInBlock; }
  void addPendingFootnote(int wordIndex, const FootnoteEntry& entry) override {
    pendingFootnotes.push_back({wordIndex, entry});
  }
  bool boxAtDepth(int atDepth) const override { return !boxScopes.empty() && boxScopes.back().depth == atDepth; }
  ImageResult processImage(const std::string& src, const CssStyle& cssStyle) override;

  void completeCurrentPage();
  void emitCurrentPage();
  void openBoxScope(BlockStyle& ownStyle, const CssStyle& cssStyle) override;
  void closeBoxScope() override;
  void layoutCurrentBlock(bool includeLastLine);
  void layoutDropCapLines(int layoutFontId, uint16_t effectiveWidth,
                          const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& emitLine,
                          bool includeLastLine);
  void applyPendingPageBreak();
  void breakTextLine(const BlockStyle& style) override;
  void fallbackTableRowToStacked() override;
  void closeTableCell() override;
  void finishTableRow() override;
  void addTableRowSeparator();
  void setCurrentPageVisibleOffset(uint32_t offset);
  void makePages(bool includeLastLine = true, bool paragraphEnd = true) override;
  void emitHorizontalRule(const BlockStyle& blockStyle) override;
  void beginAssembly();
  bool finishAssembly();
  void addLineToPage(std::unique_ptr<TextBlock> line, uint32_t visibleOffset);
  explicit ChapterPageAssembler(
      std::shared_ptr<Epub> epub, GfxRenderer& renderer, const int fontId, const float lineCompression,
      const bool extraParagraphSpacing, const uint8_t paragraphAlignment, const uint16_t viewportWidth,
      const uint16_t viewportHeight, const bool hyphenationEnabled, const bool focusReadingEnabled,
      const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>& completePageFn,
      const bool embeddedStyle, const std::string& contentBase, const std::string& imageBasePath,
      const uint8_t imageRendering = 0, std::vector<std::string> tocAnchors = {},
      const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr)

      : ChapterSemantics(cssParser, paragraphAlignment, embeddedStyle, imageRendering, std::move(tocAnchors)),
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
