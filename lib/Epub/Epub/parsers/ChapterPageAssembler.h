#pragma once

#include <Memory.h>
#include <xp_ffi.h>

#include <array>
#include <climits>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/Page.h"
#include "Epub/Paragraph.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"

class Page;
class GfxRenderer;
class Epub;

class ChapterPageAssembler {
  friend class ChapterHtmlSlimParser;

 public:
  static constexpr size_t MAX_DROP_CAP_BYTES = 12;
  static constexpr size_t MAX_GRID_TABLE_COLUMNS = 4;
  using TableColumnMeasure = XpChapterMeasure;
  enum class ImageResult : uint8_t { Fallback, Displayed, Aborted };

 private:
  XpChapter* chapter_ = nullptr;
  XpChapterState* activeSemantics_ = nullptr;
  bool stopRequested = false;
  uint8_t paragraphAlignment;
  bool embeddedStyle;
  uint8_t imageRendering;
  const CssParser* cssParser;
  std::string pendingAnchorId;
  std::vector<std::string> tocAnchors;
  std::deque<std::pair<std::string, uint16_t>> anchorData;
  static const XpChapterServices services_;
  struct SemanticBorrow {
    ChapterPageAssembler& owner;
    XpChapterState* previous;
    SemanticBorrow(ChapterPageAssembler& owner, XpChapterState* state)
        : owner(owner), previous(owner.activeSemantics_) {
      owner.activeSemantics_ = state;
    }
    ~SemanticBorrow() { owner.activeSemantics_ = previous; }
  };
  bool ensureSemantics();
  XpChapterState* semanticState() const { return activeSemantics_ ? activeSemantics_ : xp_chapter_state(chapter_); }
  uint32_t semanticQuery(XpChapterQuery query) const { return xp_chapter_query(semanticState(), query); }
  uint32_t semanticNotify(XpChapterNotify notice) { return xp_chapter_notify(semanticState(), notice); }
  bool semanticStopped() const { return stopRequested; }
  bool semanticOom() const { return semanticQuery(XP_CHAPTER_QUERY_OOM); }
  const bool* semanticStopFlag() const { return &stopRequested; }
  void semanticFailLayout() { semanticNotify(XP_CHAPTER_NOTIFY_FAIL_LAYOUT); }
  int semanticDepth() const { return static_cast<int32_t>(semanticQuery(XP_CHAPTER_QUERY_DEPTH)); }
  int semanticTableDepth() const { return static_cast<int32_t>(semanticQuery(XP_CHAPTER_QUERY_TABLE_DEPTH)); }
  uint32_t semanticVisibleOffset() const { return semanticQuery(XP_CHAPTER_QUERY_VISIBLE_OFFSET); }
  uint16_t semanticParagraphIndex() const { return semanticQuery(XP_CHAPTER_QUERY_PARAGRAPH_INDEX); }
  uint16_t semanticListIndex() const { return semanticQuery(XP_CHAPTER_QUERY_LIST_INDEX); }
  uint8_t semanticAlignment() const { return paragraphAlignment; }
  bool semanticPreservesWhitespace() const { return semanticQuery(XP_CHAPTER_QUERY_PRESERVE_WHITESPACE); }
  bool semanticMarkerOnly() const { return semanticQuery(XP_CHAPTER_QUERY_MARKER_ONLY); }
  void semanticConsumeMarker() { semanticNotify(XP_CHAPTER_NOTIFY_CONSUME_MARKER); }
  void semanticClearContinuation() { semanticNotify(XP_CHAPTER_NOTIFY_CLEAR_CONTINUATION); }
  void semanticResetInlineSize() { semanticNotify(XP_CHAPTER_NOTIFY_RESET_INLINE_SIZE); }
  float semanticInlineScale(float fallback) const;
  bool semanticHasWord() const { return semanticQuery(XP_CHAPTER_QUERY_HAS_WORD); }
  void semanticFlushWord() { semanticNotify(XP_CHAPTER_NOTIFY_FLUSH_WORD); }
  BlockStyle semanticParentBlock() const;
  uint32_t semanticBlockDepth() const { return semanticQuery(XP_CHAPTER_QUERY_BLOCK_DEPTH); }
  XpChapterDropCap semanticDropCap() const;
  void semanticClearDropCap() { semanticNotify(XP_CHAPTER_NOTIFY_CLEAR_DROP_CAP); }
  void semanticDisarmDropCap() { semanticNotify(XP_CHAPTER_NOTIFY_DISARM_DROP_CAP); }
  bool semanticPendingPageBreak() const { return semanticQuery(XP_CHAPTER_QUERY_PAGE_BREAK); }
  bool semanticConsumePageBreak() { return semanticNotify(XP_CHAPTER_NOTIFY_CONSUME_PAGE_BREAK); }
  bool semanticCellOpen() const { return semanticQuery(XP_CHAPTER_QUERY_CELL_OPEN); }
  int16_t semanticCloseCell() { return semanticNotify(XP_CHAPTER_NOTIFY_CLOSE_CELL); }
  uint8_t semanticColumnCount() const { return semanticQuery(XP_CHAPTER_QUERY_COLUMN_COUNT); }
  uint16_t semanticColumnWidth(uint8_t column) const {
    return column < 4 ? xp_chapter_query(semanticState(), XP_CHAPTER_QUERY_COLUMN0 + column) : 0;
  }
  CssBorderSide semanticTableBorder() const;
  bool semanticHasAnchor() const { return !pendingAnchorId.empty(); }
  bool semanticAnchorStartsChapter() const;
  void semanticRecordAnchor(uint16_t page);
  const char* semanticPendingAnchor() const { return pendingAnchorId.c_str(); }
  void semanticBeginBlocks() {
    stopRequested = false;
    semanticNotify(XP_CHAPTER_NOTIFY_BEGIN_BLOCKS);
  }
  void semanticBeginLists() { semanticNotify(XP_CHAPTER_NOTIFY_BEGIN_LISTS); }
  virtual void measureTableAtCurrentPosition() = 0;
  virtual uint8_t* entityBuffer(uint32_t length) = 0;

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
  const Paragraph* layoutParagraph = nullptr;
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
  int prepareBlockFont(Paragraph& text, float scale);
  size_t linesToCarry(size_t& paragraphLines) const;
  size_t keepWithNextCarry() const;
  bool canCarry(size_t carry) const;

  std::shared_ptr<Epub> epub;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
  bool imagePopupFired = false;   // popupFn fired for the first image probe (single-shot)
  std::unique_ptr<Paragraph> currentTextBlock = nullptr;
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
  std::vector<std::unique_ptr<Paragraph>> tableRowCells;
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

  void startNewTextBlock(const BlockStyle& blockStyle, bool paragraphEnd = true);
  void flushPendingAnchor();
  void applyParagraphDirection(bool defined, bool rtl);
  bool paragraphEmpty() const { return currentTextBlock->isEmpty(); }
  size_t paragraphSize() const { return currentTextBlock->size(); }
  BlockStyle paragraphStyle() { return currentTextBlock->getBlockStyle(); }
  void suppressFirstLineIndent() { return currentTextBlock->suppressFirstLineIndent(); }
  void ensureRubyCapacity() { return currentTextBlock->ensureRubyCapacity(); }
  uint8_t addLinkTarget(const char* href) { return currentTextBlock->addLinkTarget(href); }
  bool linkTargetMatches(uint8_t id, const char* href) const { return currentTextBlock->linkTargetMatches(id, href); }
  uint8_t sizeSlotFor(float scale) { return currentTextBlock->sizeSlotFor(scale); }
  void setParagraphStyle(const BlockStyle& style) { return currentTextBlock->setBlockStyle(style); }
  EpdFontFamily::Style getWordStyleAt(size_t index) const { return currentTextBlock->getWordStyleAt(index); }
  std::string getRubyTextAt(size_t index) const { return currentTextBlock->getRubyTextAt(index); }
  void setRubyGroupAt(size_t index, size_t count, const std::string& text) {
    return currentTextBlock->setRubyGroupAt(index, count, text);
  }
  void setRubyForWordAt(size_t index, const std::string& text) {
    return currentTextBlock->setRubyForWordAt(index, text);
  }
  void addWord(std::string word, EpdFontFamily::Style style, bool underline, bool attach, uint32_t offset,
               uint8_t link = 0, uint8_t slot = 0, bool noSpace = false) {
    return currentTextBlock->addWord(std::move(word), style, underline, attach, offset, link, slot, noSpace);
  }
  bool paragraphExists() const { return currentTextBlock != nullptr; }
  void createParagraph(const BlockStyle& style) {
    currentTextBlock =
        makeUniqueNoThrow<Paragraph>(hyphenationEnabled, focusReadingEnabled, style, paragraphIndentSpaces);
  }
  void resetParagraph() { currentTextBlock.reset(); }
  bool bodyIsSdFont() const;
  int bodySpaceWidth() const;
  void prepareBodyText(const char* text, uint8_t styleMask) const;
  int bodyTextAdvance(const char* text, EpdFontFamily::Style style) const;
  int bodyAscender() const;
  uint16_t pageWidth() const { return viewportWidth; }
  uint16_t pageHeight() const { return viewportHeight; }
  void clearTableCells() { tableRowCells.clear(); }
  void reserveTableCells() { tableRowCells.reserve(MAX_GRID_TABLE_COLUMNS); }
  size_t bufferedTableWords() const {
    size_t words = 0;
    for (const auto& cell : tableRowCells) words += cell->size();
    return words;
  }
  bool tableStacked() const { return tableRowStacked; }
  void setTableStacked(bool value) { tableRowStacked = value; }
  void setTableRtl(bool value) { tableRowRtl = value; }
  void resetTableBytes() { tableRowTextBytes = 0; }
  size_t tableBytes() const { return tableRowTextBytes; }
  void addTableBytes(size_t count) { tableRowTextBytes += count; }
  void resetExtractedWords() { wordsExtractedInBlock = 0; }
  int extractedWords() const { return wordsExtractedInBlock; }
  void addPendingFootnote(int wordIndex, const FootnoteEntry& entry) {
    pendingFootnotes.emplace_back(wordIndex, entry);
  }
  bool boxAtDepth(int atDepth) const { return !boxScopes.empty() && boxScopes.back().depth == atDepth; }
  ImageResult processImage(const std::string& src, const CssStyle& cssStyle);

  void completeCurrentPage();
  void emitCurrentPage();
  void openBoxScope(BlockStyle& ownStyle, const CssStyle& cssStyle);
  void closeBoxScope();
  void layoutCurrentBlock(bool includeLastLine);
  void layoutDropCapLines(int layoutFontId, uint16_t effectiveWidth,
                          const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& emitLine,
                          bool includeLastLine);
  void applyPendingPageBreak();
  void breakTextLine(const BlockStyle& style);
  void fallbackTableRowToStacked();
  void closeTableCell();
  void finishTableRow();
  void addTableRowSeparator();
  void setCurrentPageVisibleOffset(uint32_t offset);
  void makePages(bool includeLastLine = true, bool paragraphEnd = true);
  void emitHorizontalRule(const BlockStyle& blockStyle);
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
