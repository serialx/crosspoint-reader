#pragma once

#include <EpdFontFamily.h>

#include <array>
#include <climits>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/css/CssParser.h"

#define MAX_WORD_SIZE 200

#include "Epub/css/CssStyle.h"

class ChapterTableSource {
 public:
  virtual size_t read(char* bytes, size_t capacity) = 0;
};

class ChapterSemantics {
 public:
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool setsParagraphDirection = false;
    bool hasTextAlign = false;
    CssTextAlign textAlign = CssTextAlign::Left;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
    bool hasSmallCaps = false, smallCaps = false;
    bool hasWhiteSpace = false, preserveWhitespace = false;
    // Inline font size: a multiple of the parent size, or of the body size when rem.
    bool hasFontScale = false, fontScaleRem = false;
    float fontScale = 1.0f;
  };
  static constexpr size_t MAX_DROP_CAP_BYTES = 12;
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyInlinePresentationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css);
  static uint8_t dropCapLines(const CssStyle& style);

 private:
  friend class ChapterHtmlSlimParser;

 public:
  static constexpr size_t MAX_GRID_TABLE_ROW_BYTES = 2048;
  static constexpr size_t MAX_GRID_TABLE_ROW_WORDS = 160;
  static constexpr size_t MAX_GRID_TABLE_COLUMNS = 4;
  struct TableColumnMeasure {
    uint16_t minWidth[MAX_GRID_TABLE_COLUMNS] = {};   // longest word
    uint16_t prefWidth[MAX_GRID_TABLE_COLUMNS] = {};  // longest unwrapped line
    uint8_t columns = 0;
  };
  enum class ImageResult { Fallback, Displayed, Aborted };

 private:
  std::array<uint16_t, MAX_GRID_TABLE_COLUMNS> tableColumnWidths{};
  int skipUntilDepth = INT_MAX;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  bool inRuby = false;
  int rubyStartWordIndex = -1;
  bool collectingRubyText = false;
  std::string rubyTextBuffer;
  uint8_t paragraphAlignment;
  bool embeddedStyle;
  uint8_t imageRendering;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet
  bool insideTableCell = false;
  uint16_t tableRowsSpannedRemaining = 0;
  CssBorderSide tableBorder;  // grid lines for the current table; invisible when unbordered
  uint8_t tableColumnCount = 0;
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;
  // Source body codepoints, before paragraph normalization and synthetic alt text.
  uint32_t visibleTextOffset = 0;
  uint32_t partWordVisibleOffset = 0;
  bool insideBody = false;
  bool htmlEnded_ = false;
  bool syntheticCharacterData = false;
  uint16_t nonVisibleTextDepth = 0;
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  uint8_t currentFootnoteLinkId = 0;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  // Assembly failures fail the section instead of caching incomplete text.
  bool layoutOom = false;
  bool stopRequested = false;
  bool pendingPageBreak = false;        // set when a page-break-after element closes
  std::string pendingAnchorId;          // deferred until after previous text block is flushed
  std::vector<std::string> tocAnchors;  // the list of anchors that are TOC chapter boundaries
  // Chunked storage avoids a large contiguous allocation for many navigation anchors.
  std::deque<std::pair<std::string, uint16_t>> anchorData;
  struct InlineSizeState {
    float scale = 1.0f;
    int depth = -1;  // outermost sizing span
    bool valid = false;
    bool open = false;
  } inlineSize;
  struct ListContext {
    bool ordered = false;    // true for <ol>, false for <ul>
    bool styleNone = false;  // true when list-style-type: none is set on this list
    int counter = 0;         // incremented before each direct <li>; used as its number when ordered
    int depth = 0;           // parser depth at open time; matches the depth seen in endElement
                             // for the same tag, so a hidden nested list's close can't pop
                             // an outer list's context
  };
  std::vector<ListContext> listStack;
  static void startElement(void* userData, const char* name, const char* const* atts);
  static void characterData(void* userData, const char* s, int len);
  static void defaultHandlerExpand(void* userData, const char* s, int len);
  static void endElement(void* userData, const char* name);
  void flushPartWordBuffer();
  virtual bool paragraphEmpty() const = 0;
  virtual size_t paragraphSize() const = 0;
  virtual BlockStyle& paragraphStyle() = 0;
  virtual void suppressFirstLineIndent() = 0;
  virtual void ensureRubyCapacity() = 0;
  virtual uint8_t addLinkTarget(const char* href) = 0;
  virtual bool linkTargetMatches(uint8_t id, const char* href) const = 0;
  virtual uint8_t sizeSlotFor(float scale) = 0;
  virtual void setParagraphStyle(const BlockStyle& style) = 0;
  virtual EpdFontFamily::Style getWordStyleAt(size_t index) const = 0;
  virtual std::string getRubyTextAt(size_t index) const = 0;
  virtual void setRubyGroupAt(size_t index, size_t count, const std::string& text) = 0;
  virtual void setRubyForWordAt(size_t index, const std::string& text) = 0;
  virtual void addWord(const char* word, EpdFontFamily::Style style, bool underline, bool attach, uint32_t offset,
                       uint8_t link = 0, uint8_t slot = 0, bool noSpace = false) = 0;
  virtual bool paragraphExists() const = 0;
  virtual void createParagraph(const BlockStyle& style) = 0;
  virtual void resetParagraph() = 0;
  virtual bool bodyIsSdFont() const = 0;
  virtual int bodySpaceWidth() const = 0;
  virtual void prepareBodyText(const char* text, uint8_t styleMask) const = 0;
  virtual int bodyTextAdvance(const char* text, EpdFontFamily::Style style) const = 0;
  virtual int bodyAscender() const = 0;
  virtual uint16_t pageWidth() const = 0;
  virtual uint16_t pageHeight() const = 0;
  virtual void clearTableCells() = 0;
  virtual void reserveTableCells() = 0;
  virtual size_t bufferedTableWords() const = 0;
  virtual bool tableStacked() const = 0;
  virtual void setTableStacked(bool value) = 0;
  virtual void setTableRtl(bool value) = 0;
  virtual void resetTableBytes() = 0;
  virtual size_t tableBytes() const = 0;
  virtual void addTableBytes(size_t count) = 0;
  virtual void resetExtractedWords() = 0;
  virtual int extractedWords() const = 0;
  virtual void addPendingFootnote(int wordIndex, const FootnoteEntry& entry) = 0;
  virtual bool boxAtDepth(int atDepth) const = 0;
  virtual ImageResult processImage(const std::string& src, const CssStyle& cssStyle) = 0;
  bool measureTableColumns(ChapterTableSource& file, TableColumnMeasure& out) const;
  void planTableColumns(const TableColumnMeasure& measure);
  virtual void measureTableAtCurrentPosition() = 0;
  virtual void startNewTextBlock(const BlockStyle& blockStyle, bool paragraphEnd = true) = 0;
  virtual void flushPendingAnchor() = 0;
  virtual void openBoxScope(BlockStyle& ownStyle, const CssStyle& cssStyle) = 0;
  virtual void closeBoxScope() = 0;
  virtual void breakTextLine(const BlockStyle& style) = 0;
  virtual void fallbackTableRowToStacked() = 0;
  virtual void closeTableCell() = 0;
  virtual void finishTableRow() = 0;
  virtual void makePages(bool includeLastLine = true, bool paragraphEnd = true) = 0;
  virtual void emitHorizontalRule(const BlockStyle& blockStyle) = 0;

  int depth = 0;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  static constexpr size_t MAX_CSS_ANCESTORS = 16;
  std::array<CssAncestor, MAX_CSS_ANCESTORS> cssAncestors{};
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveTextAlignDefined = false;
  CssTextAlign effectiveTextAlign = CssTextAlign::Left;
  bool effectiveSup = false;
  bool effectiveSub = false;
  bool effectiveSmallCaps = false;
  bool effectivePreserveWhitespace = false;
  int tableDepth = 0;
  const CssParser* cssParser;
  struct DropCapState {
    char text[MAX_DROP_CAP_BYTES + 1] = {};
    uint8_t length = 0;
    uint8_t codepoints = 0;
    uint8_t lines = 0;  // text lines the letter spans
    bool bold = false;
    bool firstLetterPending = false;  // capture the current block's first letter
    int spanDepth = -1;               // capturing the text of the span at this depth
  } dropCap;
  void pushBlockTextStyleEntry(const CssStyle& cssStyle);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void updateEffectiveInlineStyle();
  void pushBlockStyle(const BlockStyle& accumulated);
  void armFirstLetterDropCap(const char* tagName, const std::string& classAttr, const char* idAttr);
  bool captureDropCapCodepoint(const char* bytes, int length);
  void cancelDropCapToWord();
  void applyBlockFontScale(BlockStyle& blockStyle, const CssStyle& cssStyle, const char* tagName) const;

 protected:
  ChapterSemantics(const CssParser* css, uint8_t alignment, bool embedded, uint8_t images,
                   std::vector<std::string> anchors)
      : paragraphAlignment(alignment),
        embeddedStyle(embedded),
        imageRendering(images),
        tocAnchors(std::move(anchors)),
        cssParser(css) {}

  // Synchronous assembly feedback. No container or mutable semantic state escapes.
  bool semanticStopped() const { return stopRequested; }
  bool semanticOom() const { return layoutOom; }
  const bool* semanticStopFlag() const { return &stopRequested; }
  void semanticFailLayout() { layoutOom = true; }
  int semanticDepth() const { return depth; }
  int semanticTableDepth() const { return tableDepth; }
  uint32_t semanticVisibleOffset() const { return visibleTextOffset; }
  uint16_t semanticParagraphIndex() const { return xpathParagraphIndex; }
  uint16_t semanticListIndex() const { return xpathListItemIndex; }
  uint8_t semanticAlignment() const { return paragraphAlignment; }
  bool semanticPreservesWhitespace() const { return effectivePreserveWhitespace; }
  bool semanticMarkerOnly() const { return listItemBulletOnly; }
  void semanticConsumeMarker() { listItemBulletOnly = false; }
  void semanticClearContinuation() { nextWordContinues = false; }
  void semanticResetInlineSize() { inlineSize = InlineSizeState{}; }
  float semanticInlineScale(float fallback) const { return inlineSize.valid ? inlineSize.scale : fallback; }
  bool semanticHasWord() const { return partWordBufferIndex > 0; }
  void semanticFlushWord() { flushPartWordBuffer(); }
  BlockStyle semanticParentBlock() const { return blockStyleStack.back(); }
  uint32_t semanticBlockDepth() const { return static_cast<uint32_t>(blockStyleStack.size()); }
  DropCapState semanticDropCap() const { return dropCap; }
  void semanticClearDropCap() { dropCap = DropCapState{}; }
  void semanticDisarmDropCap() { dropCap.firstLetterPending = false; }
  bool semanticPendingPageBreak() const { return pendingPageBreak; }
  bool semanticConsumePageBreak() {
    const bool pending = pendingPageBreak;
    pendingPageBreak = false;
    return pending;
  }
  bool semanticCellOpen() const { return insideTableCell; }
  int16_t semanticCloseCell() {
    insideTableCell = false;
    const auto padding = blockStyleStack.back().paddingBottom;
    blockStyleStack.pop_back();
    return padding;
  }
  uint8_t semanticColumnCount() const { return tableColumnCount; }
  uint16_t semanticColumnWidth(uint8_t column) const { return tableColumnWidths[column]; }
  CssBorderSide semanticTableBorder() const { return tableBorder; }
  bool semanticHasAnchor() const { return !pendingAnchorId.empty(); }
  bool semanticAnchorStartsChapter() const;
  void semanticRecordAnchor(uint16_t page);
  // Borrowed until the next semantic operation; used by the host-only oracle.
  const char* semanticPendingAnchor() const { return pendingAnchorId.c_str(); }
  void semanticBeginBlocks();
  void semanticBeginLists();
  virtual void applyParagraphDirection(bool defined, bool rtl) = 0;
};
