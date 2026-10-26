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

static_assert(ChapterPageAssembler::MAX_DROP_CAP_BYTES == PageDropCap::MAX_TEXT_BYTES);

// Minimum file size (in bytes) to show indexing popup - smaller chapters don't benefit from it

bool isWhitespace(char c);

void ChapterPageAssembler::flushPendingAnchor() {
  if (semanticStopped()) return;
  if (!semanticHasAnchor()) return;

  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  if (semanticAnchorStartsChapter()) {
    if (currentPage && !currentPage->elements.empty()) {
      completeCurrentPage();
      if (semanticStopped()) return;
    }
  }

  // Record deferred anchor after previous block is flushed (and any TOC page break)
  semanticRecordAnchor(static_cast<uint16_t>(completedPageCount));
}

void ChapterPageAssembler::emitCurrentPage() {
  // Open boxes are sliced at the page break and continue on the next page.
  for (BoxScope& box : boxScopes) {
    emitBoxSegment(box, false);
    if (box.top >= 0) box.continued = true;
    box.top = box.bottom = -1;
  }
  completePageFn(std::move(currentPage), semanticParagraphIndex(), semanticListIndex(), currentPageVisibleOffset);
  recentLineCount = 0;
  keepWithNextLines = 0;
  paragraphLinesOnPage = 0;
}

void ChapterPageAssembler::openBoxScope(BlockStyle& ownStyle, const CssStyle& cssStyle) {
  const bool shaded = cssStyle.defined.shaded && cssStyle.shaded;
  if (semanticTableDepth() > 0 || boxScopes.size() >= MAX_BOX_SCOPES || (!cssStyle.hasVisibleBorder() && !shaded))
    return;

  if (semanticHasWord()) semanticFlushWord();
  if (currentTextBlock && !currentTextBlock->isEmpty() && !semanticMarkerOnly()) {
    startNewTextBlock(semanticParentBlock().withoutTop().withoutBottom());
  }

  BoxScope box;
  box.depth = semanticDepth();
  box.shaded = shaded;
  const CssBorderSide* sides[4] = {&cssStyle.borderTop, &cssStyle.borderRight, &cssStyle.borderBottom,
                                   &cssStyle.borderLeft};
  for (size_t i = 0; i < 4; ++i) {
    if (sides[i]->visible()) box.sides[i] = *sides[i];
  }
  const BlockStyle& parent = semanticParentBlock();
  box.left = static_cast<int16_t>(parent.leftInset() + ownStyle.marginLeft);
  box.right = static_cast<int16_t>(viewportWidth - parent.rightInset() - ownStyle.marginRight);
  if (box.right - box.left < 8) return;

  // The border sits between margin and padding, so it joins the padding. Text also
  // keeps a small gap from the frame even when the book sets no padding.
  constexpr int16_t MIN_FRAME_GAP = 4;
  const auto pad = [&](int16_t& padding, const CssBorderSide& side) {
    if (side.width == 0 && !box.shaded) return;
    padding = static_cast<int16_t>(std::max(padding, MIN_FRAME_GAP) + side.width);
  };
  pad(ownStyle.paddingTop, box.sides[0]);
  pad(ownStyle.paddingRight, box.sides[1]);
  pad(ownStyle.paddingBottom, box.sides[2]);
  pad(ownStyle.paddingLeft, box.sides[3]);
  box.padTop = ownStyle.paddingTop;
  box.padBottom = ownStyle.paddingBottom;
  boxScopes.push_back(box);
}

void ChapterPageAssembler::closeBoxScope() {
  BoxScope box = boxScopes.back();
  boxScopes.pop_back();
  if (box.top < 0 && !box.continued) {
    // An element without content (a rule drawn with borders) gets its own height.
    // Its spacing still sits on the empty text block; consume it here instead.
    const int height = box.padTop + box.padBottom;
    if (height <= 0) return;
    int above = 0;
    int below = 0;
    if (currentTextBlock && currentTextBlock->isEmpty()) {
      BlockStyle pending = currentTextBlock->getBlockStyle();
      above = pending.marginTop;
      below = pending.marginBottom;
      pending.marginTop = pending.paddingTop = pending.marginBottom = pending.paddingBottom = 0;
      currentTextBlock->setBlockStyle(pending);
    }
    if (!currentPage) {
      currentPage.reset(new Page());
      currentPageNextY = 0;
      currentPageVisibleOffsetSet = false;
    }
    if (!currentPage->elements.empty() && currentPageNextY + above + height > viewportHeight) {
      completeCurrentPage();
      if (semanticStopped()) return;
      above = 0;
    }
    const int top = currentPageNextY + above;
    box.top = static_cast<int16_t>(top + box.padTop);
    box.bottom = box.top;
    currentPageNextY = static_cast<int16_t>(top + height + below);
    noteContent(top, top + height);
  }
  emitBoxSegment(box, true);
}

// Returns false (and restores the captured text to the word buffer) once the
// capture is too long to be an initial letter.

void ChapterPageAssembler::layoutCurrentBlock(const bool includeLastLine) {
  if (semanticStopped()) return;
  const int layoutFontId =
      prepareBlockFont(*currentTextBlock, semanticInlineScale(currentTextBlock->getBlockStyle().fontScale));
  const int horizontalInset = currentTextBlock->getBlockStyle().totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;
  bool topSpacingApplied = wordsExtractedInBlock != 0;
  const auto applyTopSpacing = [this, &topSpacingApplied]() {
    if (topSpacingApplied) return;
    const auto style = currentTextBlock->getBlockStyle();
    if (style.marginTop > 0) currentPageNextY += style.marginTop;
    if (style.paddingTop > 0) currentPageNextY += style.paddingTop;
    topSpacingApplied = true;
  };
  const auto emitLine = [this, &applyTopSpacing](std::unique_ptr<TextBlock> textBlock, const uint32_t offset) {
    if (semanticStopped()) return;
    applyTopSpacing();
    addLineToPage(std::move(textBlock), offset);
  };
  layoutParagraph = currentTextBlock.get();
  if (semanticDropCap().length > 0 && semanticDropCap().span_depth < 0 && !currentTextBlock->isEmpty()) {
    // Lines beside the letter must stay on its page.
    layoutParagraphHasDropCap = true;
    applyTopSpacing();
    layoutDropCapLines(layoutFontId, effectiveWidth, emitLine, includeLastLine);
  }
  currentTextBlock->layoutAndExtractLines(renderer, layoutFontId, effectiveWidth, emitLine, includeLastLine,
                                          characterSpacing, wordSpacingPercent, SIZE_MAX, semanticStopFlag());
  layoutParagraph = nullptr;
}

void ChapterPageAssembler::layoutDropCapLines(const int layoutFontId, const uint16_t effectiveWidth,
                                              const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& emitLine,
                                              const bool includeLastLine) {
  const uint8_t lines = semanticDropCap().lines;
  char text[MAX_DROP_CAP_BYTES + 1];
  memcpy(text, semanticDropCap().text, sizeof(text));
  const EpdFontFamily::Style capStyle = semanticDropCap().bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  semanticClearDropCap();

  // Enlarging the family's largest built-in size keeps outlines smoothest.
  const int capFontId = fontIdForScale(3.0f);
  const int lineHeight = renderer.getLineHeight(layoutFontId, lineCompression);
  const int ascender = renderer.getFontAscenderSize(layoutFontId);
  int32_t advanceFP = 0;
  int bodyCapTop = 0;
  int sourceCapTop = 0;
  if (!renderer.getCodepointMetrics(layoutFontId, 'H', EpdFontFamily::REGULAR, advanceFP, bodyCapTop)) {
    bodyCapTop = ascender * 7 / 10;
  }
  if (!renderer.getCodepointMetrics(capFontId, 'H', capStyle, advanceFP, sourceCapTop) || sourceCapTop <= 0) {
    sourceCapTop = bodyCapTop > 0 ? bodyCapTop : 1;
  }
  // The letter's top aligns with the first line's capitals, its baseline with the last spanned line.
  const int capHeight = (lines - 1) * lineHeight + bodyCapTop;
  const int scale256 = std::clamp(capHeight * 256 / sourceCapTop, 256, 4096);

  int capWidth = renderer.getSpaceWidth(layoutFontId, EpdFontFamily::REGULAR);
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&cursor)) {
    int top = 0;
    if (renderer.getCodepointMetrics(capFontId, cp, capStyle, advanceFP, top)) {
      capWidth += fp4::toPixel(advanceFP * scale256 / 256);
    }
  }
  capWidth = std::min(capWidth, effectiveWidth / 2);

  // Keep every spanned line on the letter's page.
  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
  }
  if (!currentPage->elements.empty() && currentPageNextY + lines * lineHeight > viewportHeight) {
    breakPageCarryingLines(keepWithNextCarry(), 0, semanticVisibleOffset());
    if (semanticStopped()) return;
  }
  BlockStyle blockStyle = currentTextBlock->getBlockStyle();
  const int top = currentPageNextY;
  auto element =
      makeUniqueNoThrow<PageDropCap>(capFontId, static_cast<uint16_t>(scale256), capStyle, text, blockStyle.leftInset(),
                                     static_cast<int16_t>(top + (lines - 1) * lineHeight + ascender));
  if (element) {
    currentPage->elements.push_back(std::move(element));
    noteContent(top, top + lines * lineHeight);
  } else {
    LOG_ERR("EHP", "OOM: PageDropCap");
  }

  // The spanned lines wrap beside the letter; a drop-cap paragraph has no first-line indent.
  currentTextBlock->suppressFirstLineIndent();
  blockStyle.textIndent = 0;
  blockStyle.textIndentDefined = true;
  blockStyle.marginLeft = static_cast<int16_t>(blockStyle.marginLeft + capWidth);
  currentTextBlock->setBlockStyle(blockStyle);
  currentTextBlock->layoutAndExtractLines(renderer, layoutFontId, static_cast<uint16_t>(effectiveWidth - capWidth),
                                          emitLine, includeLastLine, characterSpacing, wordSpacingPercent, lines,
                                          semanticStopFlag());
  blockStyle = currentTextBlock->getBlockStyle();
  blockStyle.marginLeft = static_cast<int16_t>(blockStyle.marginLeft - capWidth);
  currentTextBlock->setBlockStyle(blockStyle);
  currentPageNextY = static_cast<int16_t>(std::max(static_cast<int>(currentPageNextY), top + lines * lineHeight));
}

void ChapterPageAssembler::completeCurrentPage() {
  emitCurrentPage();
  completedPageCount++;
  if (semanticStopped()) return;
  currentPage.reset(new Page());
  currentPageNextY = 0;
  currentPageVisibleOffsetSet = false;
}

// Honors a pending page-break-after and the current block's page-break-before,
// once, before the block (or image) places its first content.
void ChapterPageAssembler::applyPendingPageBreak() {
  bool requested = semanticConsumePageBreak();
  if (currentTextBlock) {
    BlockStyle blockStyle = currentTextBlock->getBlockStyle();
    requested = requested || blockStyle.pageBreakBefore;
    blockStyle.pageBreakBefore = false;
    currentTextBlock->setBlockStyle(blockStyle);
  }
  if (requested && currentPage && !currentPage->elements.empty()) {
    completeCurrentPage();
  }
}

void ChapterPageAssembler::setCurrentPageVisibleOffset(const uint32_t offset) {
  if (currentPageVisibleOffsetSet) return;
  // The first page always begins at the start of the body, even when the XHTML
  // contains leading formatting whitespace before its first rendered word.
  currentPageVisibleOffset = completedPageCount == 0 ? 0 : offset;
  currentPageVisibleOffsetSet = true;
}

// flush the contents of partWordBuffer to currentTextBlock

void ChapterPageAssembler::breakTextLine(const BlockStyle& style) {
  const bool hasText = currentTextBlock && !currentTextBlock->isEmpty();
  if (!hasText && currentPage && !currentPage->elements.empty()) {
    const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
    currentPageNextY = static_cast<int16_t>(std::min<int>(viewportHeight, currentPageNextY + lineHeight));
  }
  startNewTextBlock(style, false);
  if (currentTextBlock && (hasText || semanticPreservesWhitespace())) currentTextBlock->suppressFirstLineIndent();
}

// start a new text block if needed
void ChapterPageAssembler::startNewTextBlock(const BlockStyle& blockStyle, const bool paragraphEnd) {
  if (semanticStopped()) return;
  semanticClearContinuation();  // New block = new paragraph, no continuation
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      if (paragraphEnd) currentTextBlock->resetFirstLineIndent();
      // The stack accumulates horizontal margins and text properties from ancestors.
      // Vertical margins are per-element and not inherited through the stack, but
      // container elements deposit their vertical margins on the empty block when they
      // open. Merge those into the new style so the first child in a container inherits
      // the container's vertical spacing.
      const auto style = currentTextBlock->getBlockStyle();
      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Vertical));
      semanticResetInlineSize();

      flushPendingAnchor();
      return;
    }

    // <li> added a bullet as the first word, making the block non-empty. When a nested
    // block-level child (<p>, <div>, etc.) opens, reuse the block instead of flushing
    // the bullet to its own line. The bullet stays inline with the child's text.
    if (semanticMarkerOnly()) {
      const auto style = currentTextBlock->getBlockStyle();
      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Vertical));
      semanticConsumeMarker();
      flushPendingAnchor();
      return;
    }

    makePages(true, paragraphEnd);
    if (semanticStopped()) return;
  }
  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  flushPendingAnchor();
  if (semanticStopped()) return;
  semanticDisarmDropCap();
  semanticResetInlineSize();
  currentTextBlock =
      makeUniqueNoThrow<Paragraph>(hyphenationEnabled, focusReadingEnabled, blockStyle, paragraphIndentSpaces);
  bindParagraph();
  if (!currentTextBlock) {
    // Evict rebuildable caches and retry once before failing the build.
    freeink::MemoryManager::instance().ensureFree(4 * 1024);
    currentTextBlock =
        makeUniqueNoThrow<Paragraph>(hyphenationEnabled, focusReadingEnabled, blockStyle, paragraphIndentSpaces);
    bindParagraph();
  }
  if (!currentTextBlock) {
    LOG_ERR("EHP", "OOM: Paragraph");
    semanticFailLayout();  // parseStep() turns this into ParseStatus::Error
  }
  wordsExtractedInBlock = 0;
  semanticConsumeMarker();
}

void ChapterPageAssembler::emitHorizontalRule(const BlockStyle& blockStyle) {
  if (semanticStopped()) return;
  if (semanticHasWord()) {
    semanticFlushWord();
  }

  if (currentTextBlock) {
    const BlockStyle parentBlockStyle = currentTextBlock->getBlockStyle();
    startNewTextBlock(parentBlockStyle);
  }

  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      LOG_ERR("EHP", "Failed to create page for horizontal rule");
      return;
    }
    currentPageNextY = 0;
  }

  const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId, lineCompression));
  const int16_t defaultVerticalSpacing = static_cast<int16_t>(lineHeight / 2);
  const int16_t topSpacing =
      static_cast<int16_t>((blockStyle.marginTop > 0 ? blockStyle.marginTop : defaultVerticalSpacing) +
                           (blockStyle.paddingTop > 0 ? blockStyle.paddingTop : 0));
  const int16_t bottomSpacing =
      static_cast<int16_t>((blockStyle.marginBottom > 0 ? blockStyle.marginBottom : defaultVerticalSpacing) +
                           (blockStyle.paddingBottom > 0 ? blockStyle.paddingBottom : 0));
  constexpr uint8_t ruleThickness = 2;
  const int16_t availableWidth =
      std::max<int16_t>(1, static_cast<int16_t>(viewportWidth - blockStyle.totalHorizontalInset()));
  const int16_t width = std::max<int16_t>(1, static_cast<int16_t>(availableWidth / 4));
  const int16_t xPos = static_cast<int16_t>(blockStyle.leftInset() + ((availableWidth - width) / 2));
  const int16_t totalHeight = static_cast<int16_t>(topSpacing + ruleThickness + bottomSpacing);

  if (!currentPage->elements.empty() && currentPageNextY + totalHeight > viewportHeight) {
    setCurrentPageVisibleOffset(semanticVisibleOffset());
    emitCurrentPage();
    completedPageCount++;
    if (semanticStopped()) return;
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      LOG_ERR("EHP", "Failed to create page after horizontal-rule page break");
      return;
    }
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
  }

  currentPageNextY += topSpacing;

  auto pageRule = makeUniqueNoThrow<PageHorizontalRule>(width, ruleThickness, xPos, currentPageNextY);
  if (!pageRule) {
    LOG_ERR("EHP", "Failed to create PageHorizontalRule");
    return;
  }
  currentPage->elements.push_back(std::move(pageRule));
  setCurrentPageVisibleOffset(semanticVisibleOffset());
  noteContent(currentPageNextY, currentPageNextY + ruleThickness);
  keepWithNextLines = 0;
  currentPageNextY = static_cast<int16_t>(currentPageNextY + ruleThickness + bottomSpacing);

  if (semanticHasAnchor()) {
    semanticRecordAnchor(static_cast<uint16_t>(completedPageCount));
  }
}

void ChapterPageAssembler::fallbackTableRowToStacked() {
  if (tableRowStacked) {
    return;
  }

  auto activeCell = std::move(currentTextBlock);
  bindParagraph();
  tableRowStacked = true;

  for (auto& cell : tableRowCells) {
    currentTextBlock = std::move(cell);
    bindParagraph();
    wordsExtractedInBlock = 0;
    if (currentTextBlock && !currentTextBlock->isEmpty()) {
      makePages();
    }
  }
  tableRowCells.clear();
  currentTextBlock = std::move(activeCell);
  bindParagraph();
  wordsExtractedInBlock = 0;
}

void ChapterPageAssembler::closeTableCell() {
  if (!semanticCellOpen()) {
    return;
  }
  const int bottomPadding = semanticCloseCell();

  if (!currentTextBlock) {
    return;
  }

  // Latch before the cell leaves currentTextBlock: parseStep()'s dropped-word
  // check only inspects currentTextBlock, so a cell parsed and moved (or reset
  // while empty) within one XML buffer would otherwise lose its OOM flag.
  if (currentTextBlock->hadDroppedWords()) {
    semanticFailLayout();
  }

  if (!tableRowStacked && tableRowCells.size() >= MAX_GRID_TABLE_COLUMNS) {
    fallbackTableRowToStacked();
  }

  if (tableRowStacked) {
    wordsExtractedInBlock = 0;
    if (!currentTextBlock->isEmpty()) {
      makePages();
    } else {
      currentPageNextY += bottomPadding;
    }
    currentTextBlock.reset();
    bindParagraph();
    return;
  }

  tableRowCells.push_back(std::move(currentTextBlock));
  bindParagraph();
}

void ChapterPageAssembler::addTableRowSeparator() {
  if (!currentPage || currentPage->elements.empty() || viewportWidth == 0 ||
      currentPageNextY + TABLE_ROW_SEPARATOR_GAP > viewportHeight) {
    return;
  }

  auto separator =
      makeUniqueNoThrow<PageHorizontalRule>(viewportWidth, TABLE_ROW_SEPARATOR_THICKNESS, 0, currentPageNextY + 1);
  if (!separator) {
    LOG_ERR("EHP", "OOM: table row separator");
    return;
  }
  if (currentPage->elements.capacity() == currentPage->elements.size()) {
    currentPage->elements.reserve(currentPage->elements.size() + 1);
  }
  currentPage->elements.push_back(std::move(separator));
  currentPageNextY += TABLE_ROW_SEPARATOR_GAP;
}

// Streams a table's raw markup from its opening tag and measures each column's
// longest word and longest unwrapped line, so every row can share one set of
// widths. Returns false when the table is too long or too wide to plan.

// Distributes the viewport across columns the way CSS auto table layout does:
// natural widths when they fit, otherwise each column keeps its longest word and
// shares the rest in proportion to how much wrapping it would otherwise need.

void ChapterPageAssembler::finishTableRow() {
  if (semanticStopped()) return;
  closeTableCell();

  if (tableRowCells.empty()) {
    if (tableRowStacked) {
      addTableRowSeparator();
    }
    tableRowStacked = false;
    return;
  }

  const int16_t lineHeight =
      std::max<int16_t>(1, static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression));
  const size_t columnCount = tableRowCells.size();
  const bool planned = semanticColumnCount() == columnCount;
  uint16_t columnWidths[MAX_GRID_TABLE_COLUMNS] = {};
  bool fitsGrid = columnCount >= 2;
  int16_t topPadding[MAX_GRID_TABLE_COLUMNS] = {};
  int maxTopPadding = 0;
  int maxBottomPadding = 0;
  for (size_t column = 0; column < columnCount; ++column) {
    columnWidths[column] = planned ? semanticColumnWidth(column) : static_cast<uint16_t>(viewportWidth / columnCount);
    // Equal columns keep enough width for a few glyphs so ordinary three-column tables stay
    // tabular in portrait; planned columns already fit their longest word.
    const auto& style = tableRowCells[column]->getBlockStyle();
    topPadding[column] = style.paddingTop;
    maxTopPadding = std::max<int>(maxTopPadding, style.paddingTop);
    maxBottomPadding = std::max<int>(maxBottomPadding, style.paddingBottom);
    fitsGrid = fitsGrid && columnWidths[column] > style.paddingLeft + style.paddingRight &&
               (planned || columnWidths[column] >= lineHeight * TABLE_MIN_CELL_WIDTH_LINE_HEIGHTS);
  }
  if (!fitsGrid) {
    fallbackTableRowToStacked();
    addTableRowSeparator();
    tableRowStacked = false;
    return;
  }
  const bool rowIsRtl = tableRowRtl;
  // Left edge of a logical column; RTL rows mirror the column order.
  const auto columnX = [&](const size_t column) {
    int x = 0;
    for (size_t other = 0; other < columnCount; ++other) {
      if (rowIsRtl ? other > column : other < column) x += columnWidths[other];
    }
    return x;
  };
  for (auto& lines : tableCellLines) {
    lines.clear();
  }
  tableLineVisibleOffsets.clear();
  size_t maxLineCount = 0;

  for (size_t column = 0; column < columnCount; ++column) {
    auto& lines = tableCellLines[column];
    // Reserve from this cell's size, rather than the full row budget for every column.
    const size_t estimatedLines = tableRowCells[column]->size() * 2;
    lines.reserve(estimatedLines);
    tableLineVisibleOffsets.reserve(estimatedLines);
    const int cellFontId = prepareBlockFont(*tableRowCells[column], tableRowCells[column]->getBlockStyle().fontScale);
    tableRowCells[column]->layoutAndExtractLines(
        renderer, cellFontId,
        static_cast<uint16_t>(columnWidths[column] - tableRowCells[column]->getBlockStyle().paddingLeft -
                              tableRowCells[column]->getBlockStyle().paddingRight),
        [this, &lines](std::unique_ptr<TextBlock> line, const uint32_t offset) {
          const size_t lineIndex = lines.size();
          lines.push_back(std::move(line));
          if (tableLineVisibleOffsets.size() <= lineIndex) {
            tableLineVisibleOffsets.resize(lineIndex + 1, UINT32_MAX);
          }
          tableLineVisibleOffsets[lineIndex] = std::min(tableLineVisibleOffsets[lineIndex], offset);
        },
        true, characterSpacing, wordSpacingPercent);
    maxLineCount = std::max(maxLineCount, lines.size());
  }
  // Cell layout itself can drop lines (TextBlock arena OOM in extractLine);
  // latch that before the cells are destroyed.
  for (const auto& cell : tableRowCells) {
    if (cell && cell->hadDroppedWords()) semanticFailLayout();
  }
  tableRowCells.clear();
  const auto clearLayoutLines = [this]() {
    for (auto& lines : tableCellLines) {
      lines.clear();
    }
    tableLineVisibleOffsets.clear();
  };

  // Bordered tables frame every cell. Boxes overlap their neighbours by one pixel so
  // shared edges draw as a single line; a row split by a page break is framed per page.
  CssBorderSide cellBorder = semanticTableBorder();
  cellBorder.width = std::min<uint8_t>(cellBorder.width, 3);
  const bool bordered = cellBorder.visible();
  const int16_t cellGap = bordered ? static_cast<int16_t>(cellBorder.width + 2) : 0;
  int sliceTop = -1;
  const auto frameCells = [&](const int top, const int bottom) {
    const CssBorderSide sides[4] = {cellBorder, cellBorder, cellBorder, cellBorder};
    const int height = std::min(bottom + 1, static_cast<int>(viewportHeight)) - top;
    if (height <= 0) return;
    for (size_t column = 0; column < columnCount; ++column) {
      const int x = columnX(column);
      const int width = x + columnWidths[column] >= viewportWidth ? viewportWidth - x : columnWidths[column] + 1;
      auto box = makeUniqueNoThrow<PageBorderBox>(static_cast<uint16_t>(width), static_cast<uint16_t>(height), sides,
                                                  false, static_cast<int16_t>(x), static_cast<int16_t>(top));
      if (!box) {
        LOG_ERR("EHP", "OOM: table cell border");
        return;
      }
      currentPage->elements.push_back(std::move(box));
    }
  };

  for (size_t lineIndex = 0; lineIndex < maxLineCount; ++lineIndex) {
    const uint32_t lineVisibleOffset =
        lineIndex < tableLineVisibleOffsets.size() ? tableLineVisibleOffsets[lineIndex] : semanticVisibleOffset();
    int16_t rowLineHeight = lineHeight;
    for (size_t column = 0; column < columnCount; ++column) {
      if (lineIndex < tableCellLines[column].size()) {
        const TextBlock& line = *tableCellLines[column][lineIndex];
        const int lineFontId = line.blockFontId(fontId);
        const int height = renderer.getLineHeight(lineFontId, lineCompression) + line.extraAscent(renderer, fontId) +
                           line.getRubyShift(renderer.getFontAscenderSize(lineFontId));
        rowLineHeight = std::max<int16_t>(rowLineHeight, static_cast<int16_t>(height));
      }
    }

    const int topGap = sliceTop < 0 ? maxTopPadding + cellGap : 0;
    const int bottomGap = maxBottomPadding + cellGap;
    const bool pageFull = currentPage && !currentPage->elements.empty() &&
                          currentPageNextY + topGap + rowLineHeight + bottomGap > viewportHeight;
    if (!currentPage || pageFull) {
      if (pageFull) {
        if (bordered && sliceTop >= 0) frameCells(sliceTop, currentPageNextY + bottomGap);
        sliceTop = -1;
        setCurrentPageVisibleOffset(lineVisibleOffset);
        emitCurrentPage();
        completedPageCount++;
        if (semanticStopped()) {
          clearLayoutLines();
          return;
        }
      }
      currentPage = makeUniqueNoThrow<Page>();
      if (!currentPage) {
        LOG_ERR("EHP", "OOM: page for table row");
        clearLayoutLines();
        return;
      }
      currentPageNextY = 0;
      currentPageVisibleOffsetSet = false;
    }

    if (sliceTop < 0) {
      sliceTop = currentPageNextY;
      currentPageNextY = static_cast<int16_t>(currentPageNextY + cellGap + maxTopPadding);
    }
    const int16_t rowY = currentPageNextY;
    const size_t requiredCapacity = currentPage->elements.size() + columnCount;
    if (currentPage->elements.capacity() < requiredCapacity) {
      const size_t linesThatFit =
          std::max<size_t>(1, static_cast<size_t>((viewportHeight - currentPageNextY) / rowLineHeight));
      const size_t linesToReserve = std::min(maxLineCount - lineIndex, linesThatFit);
      currentPage->elements.reserve(currentPage->elements.size() + linesToReserve * columnCount + 1);
    }
    for (size_t column = 0; column < columnCount; ++column) {
      if (lineIndex >= tableCellLines[column].size()) {
        continue;
      }

      auto& line = tableCellLines[column][lineIndex];
      auto style = line->getBlockStyle();
      style.marginLeft = static_cast<int16_t>(columnX(column));
      line->setBlockStyle(style);

      currentPageNextY = static_cast<int16_t>(rowY - maxTopPadding + topPadding[column]);
      addLineToPage(std::move(line), lineVisibleOffset);
    }
    currentPageNextY = static_cast<int16_t>(rowY + rowLineHeight);
  }

  currentPageNextY = static_cast<int16_t>(std::min<int>(currentPageNextY + maxBottomPadding, viewportHeight));
  keepWithNextLines = 0;
  if (bordered && sliceTop >= 0 && currentPage) {
    currentPageNextY = static_cast<int16_t>(std::min<int>(currentPageNextY + cellGap, viewportHeight));
    frameCells(sliceTop, currentPageNextY);
  } else {
    addTableRowSeparator();
  }
  tableRowStacked = false;
  clearLayoutLines();
}

// Only the page's trailing text lines move, and never everything on the page.

// Completes the page, moving its last `carry` lines (validated by canCarry) and their
// link areas to the top of the next page with their spacing intact.
void ChapterPageAssembler::breakPageCarryingLines(const size_t carry, const size_t paragraphLines,
                                                  const uint32_t visibleOffset) {
  auto nextPage = makeUniqueNoThrow<Page>();
  if (!nextPage) {
    LOG_ERR("EHP", "OOM: Page (%zu bytes)", sizeof(Page));
    semanticFailLayout();
    return;
  }
  std::unique_ptr<PageElement> carried[MAX_CARRIED_LINES];
  uint32_t carriedOffsets[MAX_CARRIED_LINES] = {};
  std::vector<PageLink> carriedLinks;
  int16_t carriedTop = 0;
  const int16_t carriedBottom = currentPageNextY;
  for (size_t i = 0; i < carry; ++i) {
    carried[carry - 1 - i] = std::move(currentPage->elements.back());
    currentPage->elements.pop_back();
    carriedOffsets[carry - 1 - i] = recentLineOffsets[recentLineCount - 1 - i];
  }
  if (carry > 0) {
    carriedTop = carried[0]->yPos;
    auto& links = currentPage->links;
    for (auto it = links.begin(); it != links.end();) {
      if (it->y + it->height > carriedTop) {
        if (carriedLinks.empty()) carriedLinks.reserve(links.size());
        carriedLinks.push_back(*it);
        it = links.erase(it);
      } else {
        ++it;
      }
    }
    auto& footnotes = currentPage->footnotes;
    for (auto it = footnotes.begin(); it != footnotes.end();) {
      const auto refersToNote = [&it](const PageLink& link) { return strcmp(link.href, it->href) == 0; };
      if (std::any_of(carriedLinks.begin(), carriedLinks.end(), refersToNote)) {
        if (nextPage->footnotes.empty()) nextPage->footnotes.reserve(footnotes.size());
        nextPage->addFootnote(it->number, it->href);
        if (std::none_of(links.begin(), links.end(), refersToNote)) {
          it = footnotes.erase(it);
          continue;
        }
      }
      ++it;
    }
  }

  setCurrentPageVisibleOffset(visibleOffset);
  emitCurrentPage();
  completedPageCount++;
  if (semanticStopped()) return;
  currentPage = std::move(nextPage);
  currentPageNextY = 0;
  currentPageVisibleOffsetSet = false;

  for (size_t i = 0; i < carry; ++i) {
    carried[i]->yPos = static_cast<int16_t>(carried[i]->yPos - carriedTop);
    setCurrentPageVisibleOffset(carriedOffsets[i]);
    currentPage->elements.push_back(std::move(carried[i]));
    recentLineOffsets[recentLineCount++] = carriedOffsets[i];
  }
  if (carry > 0) {
    currentPageNextY = static_cast<int16_t>(carriedBottom - carriedTop);
    noteContent(0, currentPageNextY);
  }
  for (const PageLink& link : carriedLinks) {
    currentPage->addLink(link.href, link.x, static_cast<int16_t>(link.y - carriedTop), link.width, link.height);
  }
  paragraphLinesOnPage = static_cast<uint8_t>(paragraphLines);
}

void ChapterPageAssembler::addLineToPage(std::unique_ptr<TextBlock> line, const uint32_t visibleOffset) {
  if (semanticStopped()) return;
  const int lineFontId = line->blockFontId(fontId);
  const int rubyShift = line->getRubyShift(renderer.getFontAscenderSize(lineFontId));
  // Taller inline words push the shared baseline down.
  const int baseLineHeight = renderer.getLineHeight(lineFontId, lineCompression) + line->extraAscent(renderer, fontId);
  const int lineHeight = baseLineHeight + rubyShift;

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
  }

  if (currentPageNextY + lineHeight > viewportHeight) {
    size_t paragraphLines = 0;
    const size_t carry = linesToCarry(paragraphLines);
    breakPageCarryingLines(carry, paragraphLines, visibleOffset);
    if (semanticStopped()) return;
  }
  setCurrentPageVisibleOffset(visibleOffset);

  // Track cumulative words to assign footnotes to the page containing their anchor
  wordsExtractedInBlock += line->wordCount();
  auto footnoteIt = pendingFootnotes.begin();
  while (footnoteIt != pendingFootnotes.end() && footnoteIt->first <= wordsExtractedInBlock) {
    currentPage->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes.erase(pendingFootnotes.begin(), footnoteIt);

  // Apply horizontal left inset (margin + padding) as x position offset
  const int16_t xOffset = line->getBlockStyle().leftInset();
  for (const auto& link : line->takeLinkSpans()) {
    if (!currentPage->addLink(link.href, static_cast<int16_t>(xOffset + link.x),
                              static_cast<int16_t>(currentPageNextY + rubyShift - link.topLift), link.width,
                              static_cast<int16_t>(baseLineHeight + link.topLift))) {
      LOG_DBG("EHP", "Dropped page link: %.48s", link.href);
    }
  }
  auto pageLine = makeUniqueNoThrow<PageLine>(std::move(line), xOffset, currentPageNextY);
  if (!pageLine) {
    LOG_ERR("EHP", "OOM: PageLine");
    return;
  }
  currentPage->elements.push_back(std::move(pageLine));
  noteContent(currentPageNextY, currentPageNextY + lineHeight);
  currentPageNextY += lineHeight;
  if (layoutParagraph && paragraphLinesOnPage < UINT8_MAX) paragraphLinesOnPage++;
  if (recentLineCount == MAX_CARRIED_LINES) {
    memmove(recentLineOffsets, recentLineOffsets + 1, sizeof(recentLineOffsets[0]) * (MAX_CARRIED_LINES - 1));
    recentLineCount--;
  }
  recentLineOffsets[recentLineCount++] = visibleOffset;
}

void ChapterPageAssembler::makePages(const bool includeLastLine, const bool paragraphEnd) {
  if (semanticStopped()) return;
  if (!currentTextBlock) {
    LOG_ERR("EHP", "!! No text block to make pages for !!");
    return;
  }

  // Latch before layout: startNewTextBlock() replaces the block right after
  // this returns, which would otherwise lose its dropped-words flag before
  // parseStep()/finishParse() get to check it.
  if (currentTextBlock->hadDroppedWords()) {
    semanticFailLayout();
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
  }
  if (!currentTextBlock->isEmpty()) applyPendingPageBreak();
  if (semanticStopped()) return;

  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);

  const BlockStyle blockStyle = currentTextBlock->getBlockStyle();

  layoutCurrentBlock(includeLastLine);
  if (semanticStopped()) return;
  if (includeLastLine) {
    // A heading's lines on this page wait to move with what follows it.
    keepWithNextLines =
        blockStyle.keepWithNext
            ? static_cast<uint8_t>(std::min<size_t>(MAX_CARRIED_LINES, keepWithNextLines + paragraphLinesOnPage))
            : 0;
    paragraphLinesOnPage = 0;
    layoutParagraphHasDropCap = false;
  }

  // Latch again after layout: extractLine can drop a whole line (TextBlock
  // arena OOM) during the call above, after the pre-layout latch ran, and the
  // block is replaced right after this returns.
  if (currentTextBlock->hadDroppedWords()) {
    semanticFailLayout();
  }

  // Trailing spacing and footnotes only apply when the block is finalized
  if (includeLastLine) {
    // Fallback: transfer any remaining pending footnotes to current page.
    // Normally addLineToPage handles this via word-index tracking, but this catches
    // edge cases where a footnote's word index equals the exact block size.
    if (!pendingFootnotes.empty() && currentPage) {
      for (const auto& [idx, fn] : pendingFootnotes) {
        currentPage->addFootnote(fn.number, fn.href);
      }
      pendingFootnotes.clear();
    }

    if (!paragraphEnd) return;

    // Apply bottom spacing after the paragraph (stored in pixels)
    if (blockStyle.marginBottom > 0) {
      currentPageNextY += blockStyle.marginBottom;
    }
    if (blockStyle.paddingBottom > 0) {
      currentPageNextY += blockStyle.paddingBottom;
    }

    // Extra paragraph spacing if enabled (default behavior)
    if (extraParagraphSpacing) {
      currentPageNextY += lineHeight / 2;
    }
  }
}

void ChapterPageAssembler::applyParagraphDirection(const bool defined, const bool rtl) {
  if (currentTextBlock && currentTextBlock->isEmpty()) {
    auto style = currentTextBlock->getBlockStyle();
    style.directionDefined = defined;
    style.isRtl = rtl;
    currentTextBlock->setBlockStyle(style);
  }
}

ChapterPageAssembler::ImageResult ChapterPageAssembler::processImage(const std::string& src, const CssStyle& cssStyle) {
  auto* self = this;

  LOG_DBG("EHP", "Found image: src=%s", src.c_str());

  {
    // Resolve the image path relative to the HTML file
    std::string resolvedPath = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->contentBase + src));

    if (ImageDecoderFactory::isFormatSupported(resolvedPath)) {
      // Create a unique filename for the cached image
      std::string ext;
      size_t extPos = resolvedPath.rfind('.');
      if (extPos != std::string::npos) {
        ext = resolvedPath.substr(extPos);
      }
      std::string cachedImagePath = self->imageBasePath + std::to_string(self->imageCounter++) + ext;

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
          if (self->popupFn && !self->imagePopupFired) {
            self->imagePopupFired = true;
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

        if (gotDimensions) {
          LOG_DBG("EHP", "Image dimensions: %dx%d", dims.width, dims.height);

          int displayWidth = 0;
          int displayHeight = 0;
          const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
          const CssStyle& imgStyle = cssStyle;
          const bool hasCssHeight = imgStyle.hasImageHeight();
          const bool hasCssWidth = imgStyle.hasImageWidth();

          // Compute effective container width for percentage-based image sizes.
          // If the image is inside a block with horizontal margins/padding (e.g.
          // <div style="margin: 1em 40%">), percentage widths like width:100%
          // should resolve against the container width, not the full viewport.
          int containerWidth = self->viewportWidth;
          if (self->currentTextBlock) {
            const int inset = self->currentTextBlock->getBlockStyle().totalHorizontalInset();
            if (inset > 0 && inset < self->viewportWidth) {
              containerWidth = self->viewportWidth - inset;
            }
          }

          if (hasCssHeight && hasCssWidth && dims.width > 0 && dims.height > 0) {
            // Both CSS height and width set: resolve both, then clamp to viewport preserving requested ratio
            displayHeight = static_cast<int>(
                imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
            displayWidth =
                static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
            if (displayHeight < 1) displayHeight = 1;
            if (displayWidth < 1) displayWidth = 1;
            if (displayWidth > containerWidth || displayHeight > self->viewportHeight) {
              float scaleX = (displayWidth > containerWidth) ? static_cast<float>(containerWidth) / displayWidth : 1.0f;
              float scaleY = (displayHeight > self->viewportHeight)
                                 ? static_cast<float>(self->viewportHeight) / displayHeight
                                 : 1.0f;
              float scale = (scaleX < scaleY) ? scaleX : scaleY;
              displayWidth = static_cast<int>(displayWidth * scale + 0.5f);
              displayHeight = static_cast<int>(displayHeight * scale + 0.5f);
              if (displayWidth < 1) displayWidth = 1;
              if (displayHeight < 1) displayHeight = 1;
            }
            LOG_DBG("EHP", "Display size from CSS height+width: %dx%d", displayWidth, displayHeight);
          } else if (hasCssHeight && !hasCssWidth && dims.width > 0 && dims.height > 0) {
            // Use CSS height (resolve % against viewport height) and derive width from aspect ratio
            displayHeight = static_cast<int>(
                imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
            if (displayHeight < 1) displayHeight = 1;
            displayWidth = static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
            if (displayHeight > self->viewportHeight) {
              displayHeight = self->viewportHeight;
              // Rescale width to preserve aspect ratio when height is clamped
              displayWidth = static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
              if (displayWidth < 1) displayWidth = 1;
            }
            if (displayWidth > containerWidth) {
              displayWidth = containerWidth;
              // Rescale height to preserve aspect ratio when width is clamped
              displayHeight = static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
              if (displayHeight < 1) displayHeight = 1;
            }
            if (displayWidth < 1) displayWidth = 1;
            LOG_DBG("EHP", "Display size from CSS height: %dx%d", displayWidth, displayHeight);
          } else if (hasCssWidth && !hasCssHeight && dims.width > 0 && dims.height > 0) {
            // Use CSS width (resolve % against container width) and derive height from aspect ratio
            displayWidth =
                static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
            if (displayWidth > containerWidth) displayWidth = containerWidth;
            if (displayWidth < 1) displayWidth = 1;
            displayHeight = static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
            if (displayHeight > self->viewportHeight) {
              displayHeight = self->viewportHeight;
              // Rescale width to preserve aspect ratio when height is clamped
              displayWidth = static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
              if (displayWidth < 1) displayWidth = 1;
            }
            if (displayHeight < 1) displayHeight = 1;
            LOG_DBG("EHP", "Display size from CSS width: %dx%d", displayWidth, displayHeight);
          } else {
            // Scale to fit container while maintaining aspect ratio
            int maxWidth = containerWidth;
            int maxHeight = self->viewportHeight;
            float scaleX = (dims.width > maxWidth) ? (float)maxWidth / dims.width : 1.0f;
            float scaleY = (dims.height > maxHeight) ? (float)maxHeight / dims.height : 1.0f;
            float scale = (scaleX < scaleY) ? scaleX : scaleY;
            if (scale > 1.0f) scale = 1.0f;

            displayWidth = (int)(dims.width * scale);
            displayHeight = (int)(dims.height * scale);
            LOG_DBG("EHP", "Display size: %dx%d (scale %.2f)", displayWidth, displayHeight, scale);
          }

          // Flush any pending text block so it appears before the image
          if (self->semanticHasWord()) {
            self->semanticFlushWord();
          }
          if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
            const BlockStyle parentBlockStyle = self->currentTextBlock->getBlockStyle();
            self->startNewTextBlock(parentBlockStyle);
          }

          // Apply vertical margins from the container to the image.
          // Top margin lives on the empty text block (deposited via vertical merge
          // in startNewTextBlock). Bottom margin was stripped by withoutBottom() for
          // deferred application at element close, so read it from the stack.
          int16_t imageMarginTop = 0;
          int16_t imageMarginBottom = 0;
          if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
            const auto& bs = self->currentTextBlock->getBlockStyle();
            imageMarginTop = bs.topInset();
            if (self->semanticBlockDepth() > 1) {
              imageMarginBottom = self->semanticParentBlock().bottomInset();
            }
          }

          self->applyPendingPageBreak();
          // Create page for image - only break if image won't fit remaining space
          if (self->currentPage && !self->currentPage->elements.empty() &&
              (self->currentPageNextY + imageMarginTop + displayHeight + imageMarginBottom > self->viewportHeight)) {
            // A heading introducing the image moves with it when both fit on one page.
            size_t carry = self->keepWithNextCarry();
            if (carry > 0) {
              const auto& elements = self->currentPage->elements;
              const int carriedHeight = self->currentPageNextY - elements[elements.size() - carry]->yPos;
              if (carriedHeight + imageMarginTop + displayHeight > self->viewportHeight) carry = 0;
            }
            self->breakPageCarryingLines(carry, 0, self->semanticVisibleOffset());
            if (self->semanticStopped()) return ImageResult::Aborted;
          } else if (!self->currentPage) {
            self->currentPage.reset(new Page());
            if (!self->currentPage) {
              LOG_ERR("EHP", "Failed to create initial page");
              return ImageResult::Aborted;
            }
            self->currentPageNextY = 0;
            self->currentPageVisibleOffsetSet = false;
          }

          // Apply top margin from container block. Clamp it so the image never
          // overflows the page bottom: a full-viewport-height image leaves no room
          // for the margin, and the break above only fires on non-empty pages, so a
          // fresh page would otherwise place the image at y=marginTop and run
          // marginTop pixels past viewportHeight. A large bottom reserve (status
          // bar / big screen margin) absorbs that overflow silently, but with a
          // thin reserve it crosses the physical screen edge and fails
          // ImageBlock::render's bounds check, dropping the image entirely.
          if (self->currentPageNextY + imageMarginTop + displayHeight > self->viewportHeight) {
            const int room = self->viewportHeight - displayHeight - self->currentPageNextY;
            imageMarginTop = static_cast<int16_t>(room > 0 ? room : 0);
          }
          self->currentPageNextY += imageMarginTop;

          auto imageBlock = makeUniqueNoThrow<ImageBlock>(cachedImagePath, resolvedPath, displayWidth, displayHeight);
          if (!imageBlock) {
            LOG_ERR("EHP", "Failed to create ImageBlock");
            return ImageResult::Aborted;
          }
          int xPos = (self->viewportWidth - displayWidth) / 2;
          auto pageImage = makeUniqueNoThrow<PageImage>(std::move(imageBlock), xPos, self->currentPageNextY);
          if (!pageImage) {
            LOG_ERR("EHP", "Failed to create PageImage");
            return ImageResult::Aborted;
          }
          self->currentPage->elements.push_back(std::move(pageImage));
          self->setCurrentPageVisibleOffset(self->semanticVisibleOffset());
          self->noteContent(self->currentPageNextY, self->currentPageNextY + displayHeight);
          self->keepWithNextLines = 0;
          self->currentPageNextY += displayHeight + imageMarginBottom;

          // The image consumed the empty block's accumulated vertical spacing.
          // Reset the block so the Vertical merge in startNewTextBlock doesn't
          // re-apply the same margins to the next text paragraph.
          if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
            BlockStyle resetStyle;
            resetStyle.alignment = (self->semanticAlignment() == static_cast<uint8_t>(CssTextAlign::None))
                                       ? CssTextAlign::Justify
                                       : static_cast<CssTextAlign>(self->semanticAlignment());
            self->currentTextBlock->setBlockStyle(resetStyle);
          }

          return ImageResult::Displayed;
        } else {
          LOG_ERR("EHP", "Failed to get image dimensions");
          Storage.remove(cachedImagePath.c_str());
        }
      }
    }  // isFormatSupported
  }

  return ImageResult::Fallback;
}

int ChapterPageAssembler::bodyAscender() const { return renderer.getFontAscenderSize(fontId); }

void ChapterPageAssembler::beginAssembly() {
  semanticBeginBlocks();
  const auto rootBlockStyle = semanticParentBlock();
  boxScopes.clear();
  boxScopes.reserve(MAX_BOX_SCOPES);
  semanticClearDropCap();

  semanticBeginLists();
  tableRowStacked = false;
  tableRowTextBytes = 0;
  tableRowCells.clear();
  for (auto& lines : tableCellLines) {
    lines.clear();
  }
  tableLineVisibleOffsets.clear();

  auto paragraphAlignmentBlockStyle = BlockStyle();
  paragraphAlignmentBlockStyle.textAlignDefined = true;
  const auto align = rootBlockStyle.alignment;
  paragraphAlignmentBlockStyle.alignment = align;
  startNewTextBlock(paragraphAlignmentBlockStyle);
}

bool ChapterPageAssembler::finishAssembly() {
  // Process last page if there is still text
  if (currentTextBlock) {
    makePages();
    if (semanticStopped()) return false;
    // Re-check: makePages() latches semanticOom() for lines dropped DURING this
    // final layout, which the entry check above cannot have seen.
    if (semanticOom()) {
      LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
      return false;
    }
    if (semanticHasAnchor()) {
      semanticRecordAnchor(static_cast<uint16_t>(completedPageCount));
    }
    setCurrentPageVisibleOffset(semanticVisibleOffset());
    emitCurrentPage();
    completedPageCount++;
    currentPage.reset();
    currentTextBlock.reset();
    bindParagraph();
  }

  return !semanticStopped();
}

void ChapterPageAssembler::noteContent(const int top, const int bottom) {
  for (BoxScope& box : boxScopes) {
    if (box.top < 0) box.top = static_cast<int16_t>(top);
    box.bottom = static_cast<int16_t>(std::max<int>(box.bottom, bottom));
  }
}

void ChapterPageAssembler::emitBoxSegment(const BoxScope& box, const bool closing) {
  if (box.top < 0 || !currentPage) return;
  const int top = std::max(0, box.top - box.padTop);
  const int bottom = std::min<int>(viewportHeight, box.bottom + (closing ? box.padBottom : 0));
  if (bottom <= top) return;
  // A page break slices the box: the continuing edges are left open.
  const CssBorderSide sides[4] = {box.continued ? CssBorderSide{} : box.sides[0], box.sides[1],
                                  closing ? box.sides[2] : CssBorderSide{}, box.sides[3]};
  auto element =
      makeUniqueNoThrow<PageBorderBox>(static_cast<uint16_t>(box.right - box.left), static_cast<uint16_t>(bottom - top),
                                       sides, box.shaded, box.left, static_cast<int16_t>(top));
  if (!element) {
    LOG_ERR("EHP", "OOM: PageBorderBox");
    return;
  }
  currentPage->elements.push_back(std::move(element));
}

int ChapterPageAssembler::fontIdForScale(const float scale) const {
  const int variant = renderer.resolveFontVariant(fontId, scale);
  return variant != 0 ? variant : fontId;
}

int ChapterPageAssembler::prepareBlockFont(Paragraph& text, const float scale) {
  BlockStyle blockStyle = text.getBlockStyle();
  const int layoutFontId = fontIdForScale(scale);
  for (uint8_t slot = 1; slot < text.sizeSlotsInUse(); ++slot) {
    text.setSizeSlotFontId(slot, fontIdForScale(text.sizeSlotScale(slot)));
  }
  blockStyle.fontId = layoutFontId == fontId ? 0 : layoutFontId;
  text.setBlockStyle(blockStyle);
  return layoutFontId;
}

size_t ChapterPageAssembler::linesToCarry(size_t& paragraphLines) const {
  paragraphLines = 0;
  if (layoutParagraph && !layoutParagraphHasDropCap) {
    if (paragraphLinesOnPage == 1) {
      paragraphLines = 1;  // the paragraph's first line would sit alone at the page bottom
    } else if (paragraphLinesOnPage >= 2 && layoutParagraph->linesAfterCurrentLine() == 0) {
      // The last line would open the next page alone; taking one line along must not
      // leave an orphan behind.
      paragraphLines = paragraphLinesOnPage >= 3 ? 1 : 2;
    }
  }
  // A heading moves too when none of what follows it would stay on this page.
  size_t carry = paragraphLines;
  if (keepWithNextLines > 0 && paragraphLines == paragraphLinesOnPage) carry += keepWithNextLines;
  if (canCarry(carry)) return carry;
  if (canCarry(paragraphLines)) return paragraphLines;
  paragraphLines = 0;
  return 0;
}

size_t ChapterPageAssembler::keepWithNextCarry() const { return canCarry(keepWithNextLines) ? keepWithNextLines : 0; }

bool ChapterPageAssembler::canCarry(const size_t carry) const {
  if (carry == 0) return true;
  if (!currentPage || carry > recentLineCount || carry > MAX_CARRIED_LINES) return false;
  const auto& elements = currentPage->elements;
  if (elements.size() <= carry) return false;
  for (size_t i = 0; i < carry; ++i) {
    if (elements[elements.size() - 1 - i]->getTag() != TAG_PageLine) return false;
  }
  return true;
}

ChapterPageAssembler::~ChapterPageAssembler() = default;

bool ChapterPageAssembler::bodyIsSdFont() const { return renderer.isSdCardFont(fontId); }
int ChapterPageAssembler::bodySpaceWidth() const { return renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR); }
void ChapterPageAssembler::prepareBodyText(const char* text, const uint8_t styleMask) const {
  renderer.ensureSdCardFontReady(fontId, text, styleMask);
}
int ChapterPageAssembler::bodyTextAdvance(const char* word, const EpdFontFamily::Style style) const {
  return renderer.getTextAdvanceX(fontId, word, style);
}
