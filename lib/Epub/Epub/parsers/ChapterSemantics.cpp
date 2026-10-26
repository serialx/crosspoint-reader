#include "ChapterSemantics.h"

#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "ChapterTableConstants.h"
#include "Epub/VisibleTextUtils.h"
#include "Epub/htmlEntities.h"

namespace {
// Opening quotes and other punctuation that lead into a drop cap letter.
bool isDropCapPunctuation(const uint32_t cp) {
  if (cp < 0x80) return std::ispunct(static_cast<int>(cp)) != 0;
  return cp == 0xA1 || cp == 0xAB || cp == 0xBB || cp == 0xBF || (cp >= 0x2010 && cp <= 0x205E) ||
         (cp >= 0x3008 && cp <= 0x3011);
}
}  // namespace

void ChapterSemantics::applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasDirection()) {
    entry.hasDirection = true;
    entry.direction = css.direction;
  }
}

EpdFontFamily::Style ChapterSemantics::fontStyleForTextDecoration(const CssTextDecoration decoration) {
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  if ((decoration & CssTextDecoration::Underline) != CssTextDecoration::None) {
    style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::UNDERLINE);
  }
  if ((decoration & CssTextDecoration::LineThrough) != CssTextDecoration::None) {
    style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::STRIKETHROUGH);
  }
  return style;
}

void ChapterSemantics::applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasTextDecoration()) {
    entry.hasTextDecoration = true;
    entry.textDecoration = css.textDecoration;
  }
}

void ChapterSemantics::applyInlinePresentationToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasFontSize()) {
    entry.hasFontScale = true;
    entry.fontScaleRem = css.fontSize.unit == CssUnit::Rem;
    entry.fontScale = css.fontSize.value;
  }
  if (css.defined.whiteSpace) {
    entry.hasWhiteSpace = true;
    entry.preserveWhitespace = css.preserveWhitespace;
  }
  if (css.hasSmallCaps()) {
    entry.hasSmallCaps = true;
    entry.smallCaps = css.smallCaps;
  }
}

void ChapterSemantics::applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (!css.hasVerticalAlign()) return;
  if (css.verticalAlign == CssVerticalAlign::Super) {
    entry.hasSup = true;
    entry.sup = true;
  } else if (css.verticalAlign == CssVerticalAlign::Sub) {
    entry.hasSub = true;
    entry.sub = true;
  }
}

uint8_t ChapterSemantics::dropCapLines(const CssStyle& style) {
  if (style.defined.initialLetter && style.initialLetter > 0) {
    return style.initialLetter >= 2 ? style.initialLetter : 0;
  }
  const bool large = style.hasFontSize() && style.fontSize.value >= 1.5f;
  if (!large && !(style.defined.floatLeft && style.floatLeft)) return 0;
  // A floated letter without a size spans three lines; a sized one about its size in ems.
  return large ? static_cast<uint8_t>(std::clamp(static_cast<int>(style.fontSize.value + 0.5f), 2, 4)) : 3;
}

void ChapterSemantics::pushBlockTextStyleEntry(const CssStyle& cssStyle) {
  const bool sized =
      cssStyle.hasFontSize() || std::any_of(inlineStyleStack.begin(), inlineStyleStack.end(),
                                            [](const StyleStackEntry& entry) { return entry.hasFontScale; });
  if (!cssStyle.hasFontWeight() && !cssStyle.hasFontStyle() && !cssStyle.hasTextDecoration() &&
      !cssStyle.hasSmallCaps() && !cssStyle.defined.whiteSpace && !cssStyle.hasDirection() &&
      !cssStyle.hasTextAlign() && !sized) {
    return;
  }

  StyleStackEntry entry;
  entry.depth = depth;
  if (cssStyle.hasFontWeight()) {
    entry.hasBold = true;
    entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
  }
  if (cssStyle.hasFontStyle()) {
    entry.hasItalic = true;
    entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
  }
  applyTextDecorationToEntry(entry, cssStyle);
  applyInlinePresentationToEntry(entry, cssStyle);
  if (sized) {
    // The block has already resolved its ancestors and its own declaration.
    entry.hasFontScale = true;
    entry.fontScaleRem = true;
    entry.fontScale = blockStyleStack.back().fontScale;
  }
  applyDirectionToEntry(entry, cssStyle);
  entry.setsParagraphDirection = true;
  if (cssStyle.hasTextAlign()) {
    entry.hasTextAlign = true;
    entry.textAlign = cssStyle.textAlign;
  }
  inlineStyleStack.push_back(entry);
  updateEffectiveInlineStyle();
}

void ChapterSemantics::pushDecorationStyleEntry(const CssTextDecoration defaultDecoration, const CssStyle& cssStyle) {
  StyleStackEntry entry;
  entry.depth = depth;
  entry.hasTextDecoration = true;
  entry.textDecoration = cssStyle.hasTextDecoration() ? cssStyle.textDecoration : defaultDecoration;
  if (cssStyle.hasFontWeight()) {
    entry.hasBold = true;
    entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
  }
  if (cssStyle.hasFontStyle()) {
    entry.hasItalic = true;
    entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
  }
  applyInlinePresentationToEntry(entry, cssStyle);
  applyDirectionToEntry(entry, cssStyle);
  inlineStyleStack.push_back(entry);
  updateEffectiveInlineStyle();
}

void ChapterSemantics::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveBold = currentCssStyle.hasFontWeight() && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveItalic = currentCssStyle.hasFontStyle() && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveTextDecoration =
      currentCssStyle.hasTextDecoration() ? currentCssStyle.textDecoration : CssTextDecoration::None;
  effectiveSmallCaps = currentCssStyle.hasSmallCaps() && currentCssStyle.smallCaps;
  effectivePreserveWhitespace = currentCssStyle.defined.whiteSpace && currentCssStyle.preserveWhitespace;
  bool paragraphDirectionDefined = false;
  bool paragraphIsRtl = false;
  if (!blockStyleStack.empty()) {
    const auto& blockStyle = blockStyleStack.back();
    paragraphDirectionDefined = blockStyle.directionDefined;
    paragraphIsRtl = blockStyle.isRtl;
  }
  effectiveDirectionDefined = paragraphDirectionDefined;
  effectiveDirection = paragraphIsRtl ? CssTextDirection::Rtl : CssTextDirection::Ltr;
  effectiveTextAlignDefined = currentCssStyle.hasTextAlign();
  effectiveTextAlign = currentCssStyle.textAlign;
  effectiveSup = false;
  effectiveSub = false;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveItalic = entry.italic;
    }
    // CSS line decorations propagate through descendants; child entries add
    // their own lines but cannot cancel an ancestor's already active line.
    if (entry.hasTextDecoration) {
      effectiveTextDecoration = effectiveTextDecoration | entry.textDecoration;
    }
    if (entry.hasWhiteSpace) effectivePreserveWhitespace = entry.preserveWhitespace;
    if (entry.hasSmallCaps) {
      effectiveSmallCaps = entry.smallCaps;
    }
    if (entry.hasDirection) {
      effectiveDirectionDefined = true;
      effectiveDirection = entry.direction;
      if (entry.setsParagraphDirection) {
        paragraphDirectionDefined = true;
        paragraphIsRtl = entry.direction == CssTextDirection::Rtl;
      }
    }
    if (entry.hasTextAlign) {
      effectiveTextAlignDefined = true;
      effectiveTextAlign = entry.textAlign;
    }
    if (entry.hasSup) {
      effectiveSup = entry.sup;
      if (entry.sup) effectiveSub = false;
    }
    if (entry.hasSub) {
      effectiveSub = entry.sub;
      if (entry.sub) effectiveSup = false;
    }
  }

  // Keep flow direction in the active empty text block. Inline direction remains
  // available for CSS inheritance without replacing the paragraph's base direction.
  applyParagraphDirection(paragraphDirectionDefined, paragraphIsRtl);
}

void ChapterSemantics::pushBlockStyle(const BlockStyle& accumulated) {
  // The break belongs to the element's first block only; blocks restarted from
  // the stack after a child closes must not break again.
  BlockStyle entry = accumulated;
  entry.pageBreakBefore = false;
  blockStyleStack.push_back(entry);
}

void ChapterSemantics::armFirstLetterDropCap(const char* tagName, const std::string& classAttr, const char* idAttr) {
  if (!cssParser || !cssParser->hasFirstLetterRules() || tableDepth > 0 || dropCap.length > 0) return;
  const auto elementDepth = static_cast<size_t>(depth);
  const CssStyle firstLetter = cssParser->resolveStyle(tagName, classAttr, idAttr, cssAncestors.data(),
                                                       elementDepth <= MAX_CSS_ANCESTORS ? elementDepth : 0, true);
  const uint8_t lines = dropCapLines(firstLetter);
  if (lines == 0) return;
  dropCap.firstLetterPending = true;
  dropCap.lines = lines;
  dropCap.bold = firstLetter.hasFontWeight() && firstLetter.fontWeight == CssFontWeight::Bold;
}

bool ChapterSemantics::captureDropCapCodepoint(const char* bytes, const int length) {
  constexpr uint8_t MAX_DROP_CAP_CODEPOINTS = 3;
  if (dropCap.length + length > static_cast<int>(MAX_DROP_CAP_BYTES) || dropCap.codepoints >= MAX_DROP_CAP_CODEPOINTS) {
    cancelDropCapToWord();
    return false;
  }
  memcpy(dropCap.text + dropCap.length, bytes, static_cast<size_t>(length));
  dropCap.length = static_cast<uint8_t>(dropCap.length + length);
  dropCap.text[dropCap.length] = '\0';
  dropCap.codepoints++;
  if (dropCap.spanDepth < 0) {
    // ::first-letter takes leading punctuation plus the first letter.
    const auto* cursor = reinterpret_cast<const unsigned char*>(bytes);
    if (!isDropCapPunctuation(utf8NextCodepoint(&cursor))) dropCap.firstLetterPending = false;
  }
  return true;
}

void ChapterSemantics::cancelDropCapToWord() {
  memcpy(partWordBuffer + partWordBufferIndex, dropCap.text, dropCap.length);
  partWordBufferIndex += dropCap.length;
  dropCap = DropCapState{};
}

void ChapterSemantics::applyBlockFontScale(BlockStyle& blockStyle, const CssStyle& cssStyle,
                                           const char* tagName) const {
  float parentScale = blockStyleStack.empty() ? 1.0f : blockStyleStack.back().fontScale;
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasFontScale) parentScale = entry.fontScaleRem ? entry.fontScale : parentScale * entry.fontScale;
  }
  float scale = parentScale;
  if (cssStyle.hasFontSize()) {
    scale = cssStyle.fontSize.unit == CssUnit::Rem ? cssStyle.fontSize.value : parentScale * cssStyle.fontSize.value;
  } else if (tagName[0] == 'h' && tagName[1] >= '1' && tagName[1] <= '6' && tagName[2] == '\0') {
    // Browser default heading sizes, h1..h6.
    static constexpr float HEADING_SCALES[] = {2.0f, 1.5f, 1.17f, 1.0f, 0.83f, 0.67f};
    scale = parentScale * HEADING_SCALES[tagName[1] - '1'];
  }
  blockStyle.fontScale = std::clamp(scale, 0.5f, 3.0f);
  blockStyle.fontScaleDefined = true;
}

// This number comes from PR #73
// If we have > 750 words buffered up, perform the layout and consume out all but the last line
// There should be enough here to build out 1-2 full pages and doing this will free up a lot of
// memory.
// Spotted when reading Intermezzo, there are some really long text blocks in there.
constexpr size_t TEXT_BLOCK_SOFT_FLUSH_WORDS = 750;

// When CSS is enabled, flush earlier to save RAM. 320 is still more than enough to build a CJK
// page at font size 14
constexpr size_t TEXT_BLOCK_SOFT_FLUSH_WORDS_WITH_CSS = 320;

// Hard cap on the number of anchor IDs recorded per chapter. Legitimate navigation
// anchors (TOC entries, footnotes, cross-references) rarely exceed a few hundred per
// chapter. A runaway count usually means a converter injected machine-generated IDs on
// every text fragment (e.g. Kobo KePub spans). The cap prevents unbounded heap growth
// on resource-constrained devices (~380KB heap). TOC anchors bypass this cap.
constexpr size_t MAX_ANCHORS_PER_CHAPTER = 1024;

// Reuse serializable PageLine/PageHorizontalRule elements for a small grid.

constexpr const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote", "ul", "ol", "pre", "figure"};
constexpr const char* BOLD_TAGS[] = {"b", "strong"};
constexpr const char* ITALIC_TAGS[] = {"i", "em"};
constexpr const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr const char* LINETHROUGH_TAGS[] = {"del", "s", "strike"};
constexpr const char* IMAGE_TAGS[] = {"img", "image"};
bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

std::string trimAndNormalize(const std::string& str) {
  if (str.empty()) return "";
  size_t start = 0;
  while (start < str.size() && isWhitespace(str[start])) {
    start++;
  }
  if (start == str.size()) return "";
  size_t end = str.size() - 1;
  while (end > start && isWhitespace(str[end])) {
    end--;
  }
  std::string result;
  result.reserve(end - start + 1);
  bool inSpace = false;
  for (size_t i = start; i <= end; i++) {
    if (isWhitespace(str[i])) {
      if (!inSpace) {
        result.push_back(' ');
        inSpace = true;
      }
    } else {
      result.push_back(str[i]);
      inSpace = false;
    }
  }
  return result;
}

bool matches(const char* tag_name, const char* const* possible_tags, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool isNonVisibleTextTag(const char* name) { return VisibleTextUtils::isNonVisibleElement(name); }

const char* getAttribute(const char* const* atts, const char* attrName) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) return atts[i + 1];
  }
  return nullptr;
}

uint16_t parseTableSpan(const char* value) {
  if (!value || value[0] == '\0') return 1;

  uint32_t span = 0;
  for (const char* current = value; *current != '\0'; ++current) {
    if (*current < '0' || *current > '9') return 1;
    const uint32_t digit = static_cast<uint32_t>(*current - '0');
    if (span > (UINT16_MAX - digit) / 10) return UINT16_MAX;
    span = span * 10 + digit;
  }
  return span == 0 ? UINT16_MAX : static_cast<uint16_t>(span);
}

// Returns true if the HTML element is a purely inline, non-navigable wrapper.
// IDs on these elements are never meaningful navigation targets in epub content.
// Reading-system converters (Kobo KePub, Calibre, etc.) frequently inject thousands
// of such IDs for progress tracking or internal bookkeeping, and recording each one
// as a navigation anchor exhausts the heap on memory-constrained devices.
// Block-level, sectioning, and structural elements are always considered navigable.
bool isNonNavigableInlineElement(const char* name) { return strcmp(name, "span") == 0; }

bool isInternalEpubLink(const char* href) {
  if (!href || href[0] == '\0') return false;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) return false;
  if (strncmp(href, "mailto:", 7) == 0) return false;
  if (strncmp(href, "ftp://", 6) == 0) return false;
  if (strncmp(href, "tel:", 4) == 0) return false;
  if (strncmp(href, "javascript:", 11) == 0) return false;
  return true;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, std::size(HEADER_TAGS)) || matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS));
}

bool isTableStructuralTag(const char* name) {
  return strcmp(name, "table") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

// Update effective bold/italic/decorations based on block style and inline style stack

void ChapterSemantics::startElement(void* userData, const char* name, const char* const* atts) {
  auto* self = static_cast<ChapterSemantics*>(userData);
  if (self->stopRequested) return;
  if (strcasecmp(name, "body") == 0) {
    // Case-insensitive to match ParagraphStreamer's tag matching (ProgressMapper). A case
    // mismatch here would leave visibleTextOffset at 0 for the whole section, so every page
    // would record offset 0 while the sync resolver still counts a non-zero offset.
    self->insideBody = true;
  }
  if (self->insideBody && (self->nonVisibleTextDepth > 0 || isNonVisibleTextTag(name))) {
    self->nonVisibleTextDepth++;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  if (strcmp(name, "p") == 0) {
    self->xpathParagraphIndex++;
  }
  if (strcmp(name, "li") == 0) {
    self->xpathListItemIndex++;
  }

  // Extract class, style, id, dir and hidden attributes for CSS/RTL processing
  std::string classAttr;
  std::string styleAttr;
  std::string dirAttr;
  const char* idAttr = "";
  bool hasHiddenAttr = false;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        // Defer both anchor recording and TOC page breaks until startNewTextBlock,
        // after the previous block is flushed to pages via makePages().
        //
        // Skip IDs on non-navigable inline elements (e.g. <span>): these are never
        // link targets in epub content, but reading-system converters can inject tens
        // of thousands of them per chapter, exhausting the heap. TOC anchors are
        // always recorded regardless of element type, since they drive page breaks.
        const char* idValue = atts[i + 1];
        idAttr = idValue;
        const bool isTocAnchor =
            std::find(self->tocAnchors.begin(), self->tocAnchors.end(), idValue) != self->tocAnchors.end();
        if (isTocAnchor || (!isNonNavigableInlineElement(name) && self->anchorData.size() < MAX_ANCHORS_PER_CHAPTER)) {
          // Flush a displaced anchor before overwriting. Consecutive non-block elements
          // (e.g. <aside id="fn1">text</aside><aside id="fn2">) with no intervening block
          // never trigger startNewTextBlock, so fn1 gets silently overwritten. That leaves
          // fn1 missing from the anchor map -> getPageForAnchor returns nullopt -> reader
          // lands at page 0 (section start) instead of the footnote.
          if (!self->pendingAnchorId.empty()) {
            self->flushPendingAnchor();
          }
          self->pendingAnchorId = idValue;
        }
      } else if (strcmp(atts[i], "dir") == 0) {
        dirAttr = atts[i + 1];
      } else if (strcmp(atts[i], "hidden") == 0) {
        hasHiddenAttr = true;
      }
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Compute CSS style for this element early so display:none can short-circuit
  // before tag-specific branches emit any content or metadata.
  CssStyle cssStyle;
  if (self->cssParser) {
    const auto elementDepth = static_cast<size_t>(self->depth);
    cssStyle = self->cssParser->resolveStyle(name, classAttr, idAttr, self->cssAncestors.data(),
                                             elementDepth <= MAX_CSS_ANCESTORS ? elementDepth : 0);
    if (elementDepth < MAX_CSS_ANCESTORS) {
      self->cssAncestors[elementDepth] = CssParser::makeAncestor(name, classAttr, idAttr);
    }
    if (!styleAttr.empty()) {
      CssStyle inlineStyle = CssParser::parseInlineStyle(styleAttr);
      cssStyle.applyOver(inlineStyle);
    }
  }

  // Resolve explicit inheritance after the cascade, before this element changes the active style.
  switch (cssStyle.fontWeight) {
    case CssFontWeight::Normal:
    case CssFontWeight::Bold:
      break;
    case CssFontWeight::Inherit:
      cssStyle.fontWeight =
          (self->effectiveBold || self->boldUntilDepth < self->depth) ? CssFontWeight::Bold : CssFontWeight::Normal;
      break;
  }
  switch (cssStyle.fontStyle) {
    case CssFontStyle::Normal:
    case CssFontStyle::Italic:
      break;
    case CssFontStyle::Inherit:
      cssStyle.fontStyle =
          (self->effectiveItalic || self->italicUntilDepth < self->depth) ? CssFontStyle::Italic : CssFontStyle::Normal;
      break;
  }

  if (strcmp(name, "pre") == 0 && !cssStyle.defined.whiteSpace) {
    cssStyle.preserveWhitespace = true;
    cssStyle.defined.whiteSpace = 1;
  }

  // HTML hidden attribute overrides CSS display.
  if (hasHiddenAttr) {
    cssStyle.display = CssDisplay::None;
    cssStyle.defined.display = 1;
  }

  // HTML dir attribute overrides CSS direction (case-insensitive per HTML spec)
  if (!dirAttr.empty()) {
    if (strcasecmp(dirAttr.c_str(), "rtl") == 0) {
      cssStyle.direction = CssTextDirection::Rtl;
      cssStyle.defined.direction = 1;
    } else if (strcasecmp(dirAttr.c_str(), "ltr") == 0) {
      cssStyle.direction = CssTextDirection::Ltr;
      cssStyle.defined.direction = 1;
    }
  }

  // Direction is inherited in HTML/CSS. If this element does not define one, carry
  // the currently active inherited direction into its computed style.
  if (!cssStyle.hasDirection() && self->effectiveDirectionDefined) {
    cssStyle.direction = self->effectiveDirection;
    cssStyle.defined.direction = 1;
  }

  // Skip elements with display:none before all fast paths (tables, links, etc.).
  if (cssStyle.hasDisplay() && cssStyle.display == CssDisplay::None) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Buffer one simple row; oversized rows fall back to full-width flow.
  if (strcmp(name, "table") == 0) {
    // Flatten nested content without allocating a recursive row buffer.
    if (self->tableDepth > 0) {
      if (self->tableDepth == 1 && self->insideTableCell && self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->nextWordContinues = false;
      self->tableDepth += 1;
      self->depth += 1;
      return;
    }

    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    if (self->paragraphExists() && !self->paragraphEmpty()) {
      self->makePages();
      self->resetParagraph();
    }
    self->flushPendingAnchor();
    self->inlineSize = InlineSizeState{};
    self->dropCap.firstLetterPending = false;
    self->pushBlockTextStyleEntry(cssStyle);
    self->tableDepth = 1;
    self->insideTableCell = false;
    self->setTableStacked(false);
    self->setTableRtl(cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl);
    self->tableRowsSpannedRemaining = 0;
    self->resetTableBytes();
    self->clearTableCells();
    self->reserveTableCells();
    self->tableBorder = CssBorderSide{};
    self->tableColumnCount = 0;
    self->measureTableAtCurrentPosition();
    for (const CssBorderSide* side :
         {&cssStyle.borderTop, &cssStyle.borderRight, &cssStyle.borderBottom, &cssStyle.borderLeft}) {
      if (!self->tableBorder.visible() && side->visible()) self->tableBorder = *side;
    }
    const char* borderAttr = getAttribute(atts, "border");
    if (!self->tableBorder.visible() && borderAttr && atoi(borderAttr) > 0) {
      self->tableBorder = CssBorderSide{1, CssBorderStyle::Solid};
    }
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && strcmp(name, "tr") == 0) {
    self->finishTableRow();
    if (self->paragraphExists() && !self->paragraphEmpty()) {
      // Text before the first row is typically a <caption>.
      self->makePages();
    }
    self->resetParagraph();
    self->resetTableBytes();
    self->setTableStacked(self->tableRowsSpannedRemaining > 0);
    self->setTableRtl(cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl);
    if (self->tableRowsSpannedRemaining != UINT16_MAX && self->tableRowsSpannedRemaining > 0) {
      self->tableRowsSpannedRemaining--;
    }
    self->pushBlockTextStyleEntry(cssStyle);
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->closeTableCell();
    if (self->paragraphExists() && !self->paragraphEmpty()) {
      self->makePages();
    }
    self->resetParagraph();

    const uint16_t columnSpan = parseTableSpan(getAttribute(atts, "colspan"));
    const uint16_t rowSpan = parseTableSpan(getAttribute(atts, "rowspan"));
    if (columnSpan > 1 || rowSpan > 1) {
      self->fallbackTableRowToStacked();
    }
    if (rowSpan > 1) {
      const uint16_t remaining = rowSpan == UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(rowSpan - 1);
      self->tableRowsSpannedRemaining = std::max(self->tableRowsSpannedRemaining, remaining);
    }

    auto tableCellBlockStyle = BlockStyle();
    self->applyBlockFontScale(tableCellBlockStyle, cssStyle, name);
    const float emSize = static_cast<float>(self->bodyAscender());
    // Bound author padding so even a stacked cell leaves room for content.
    const auto padding = [&](const CssLength& length, const int limit) {
      return static_cast<int16_t>(std::clamp<int>(length.toPixelsInt16(emSize, self->pageWidth()), 0, limit));
    };
    tableCellBlockStyle.paddingLeft = cssStyle.hasPaddingLeft() ? padding(cssStyle.paddingLeft, self->pageWidth() / 4)
                                                                : TABLE_CELL_HORIZONTAL_PADDING;
    tableCellBlockStyle.paddingRight = cssStyle.hasPaddingRight()
                                           ? padding(cssStyle.paddingRight, self->pageWidth() / 4)
                                           : TABLE_CELL_HORIZONTAL_PADDING;
    tableCellBlockStyle.paddingTop = padding(cssStyle.paddingTop, self->pageHeight() / 4);
    tableCellBlockStyle.paddingBottom = padding(cssStyle.paddingBottom, self->pageHeight() / 4);
    tableCellBlockStyle.textAlignDefined = true;
    tableCellBlockStyle.alignment =
        cssStyle.hasTextAlign()
            ? cssStyle.textAlign
            : (self->effectiveTextAlignDefined
                   ? self->effectiveTextAlign
                   : (cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl ? CssTextAlign::Right
                                                                                             : CssTextAlign::Left));
    if (cssStyle.hasDirection()) {
      tableCellBlockStyle.directionDefined = true;
      tableCellBlockStyle.isRtl = cssStyle.direction == CssTextDirection::Rtl;
    }

    self->createParagraph(tableCellBlockStyle);
    if (!self->paragraphExists()) {
      LOG_ERR("EHP", "OOM: table cell");
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
    for (const CssBorderSide* side :
         {&cssStyle.borderTop, &cssStyle.borderRight, &cssStyle.borderBottom, &cssStyle.borderLeft}) {
      if (!self->tableBorder.visible() && side->visible()) self->tableBorder = *side;
    }
    self->pushBlockStyle(tableCellBlockStyle);
    self->insideTableCell = true;
    self->resetExtractedWords();
    self->flushPendingAnchor();
    self->pushBlockTextStyleEntry(cssStyle);

    if (strcmp(name, "th") == 0 && (!cssStyle.hasFontWeight() || cssStyle.fontWeight == CssFontWeight::Bold)) {
      self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    }

    self->depth += 1;
    return;
  }

  if (self->tableDepth >= 1 && self->insideTableCell &&
      (isHeaderOrBlock(name) || strcmp(name, "hr") == 0 || matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)))) {
    // ponytail: rich cells use full-width flow; keep a grid only for plain inline content.
    if (self->partWordBufferIndex > 0) self->flushPartWordBuffer();
    self->fallbackTableRowToStacked();
  }

  if (matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS))) {
    std::string src;
    std::string alt;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          src = atts[i + 1];
        } else if (src.empty() && (strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0)) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }

      const size_t fragmentPos = src.find('#');
      if (fragmentPos != std::string::npos) {
        src.resize(fragmentPos);
      }

      // imageRendering: 0=display, 1=placeholder (alt text only), 2=suppress entirely
      if (self->imageRendering == 2) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      if (!src.empty() && self->imageRendering != 1) {
        switch (self->processImage(src, cssStyle)) {
          case ImageResult::Displayed:
            self->depth += 1;
            return;
          case ImageResult::Aborted:
            return;
          case ImageResult::Fallback:
            break;
        }
      }

      // Fallback to alt text if image processing fails
      if (!alt.empty()) {
        alt = "[Image: " + alt + "]";
        self->startNewTextBlock(self->blockStyleStack.back()
                                    .getCombinedBlockStyle(centeredBlockStyle, BlockStyle::CombineAxis::Horizontal)
                                    .withoutBottom());
        self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
        self->depth += 1;
        self->syntheticCharacterData = true;
        self->characterData(userData, alt.c_str(), alt.length());
        self->syntheticCharacterData = false;
        // Skip any child content (skip until parent as we pre-advanced depth above)
        self->skipUntilDepth = self->depth - 1;
        return;
      }

      // No alt text, skip
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  // Ruby tag handling
  if (strcmp(name, "ruby") == 0) {
    // <ruby> is an inline element: a base that follows text with no whitespace between them
    // continues the same visual word, exactly like <b>/<i> handling in endElement().
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->inRuby = true;
    self->rubyStartWordIndex = self->paragraphExists() ? static_cast<int>(self->paragraphSize()) : 0;
    if (self->paragraphExists()) {
      self->ensureRubyCapacity();
    }
    self->rubyTextBuffer.clear();
    self->depth += 1;
    return;
  }
  if (strcmp(name, "rt") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->collectingRubyText = true;
    self->depth += 1;
    return;
  }

  if (VisibleTextUtils::isNonVisibleElement(name)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  // Detect internal <a href="..."> links (footnotes, cross-references)
  // Note: <aside epub:type="footnote"> elements are rendered as normal content
  // without special handling. Links pointing to them are collected as footnotes.
  if (strcmp(name, "a") == 0) {
    const char* href = getAttribute(atts, "href");

    bool isInternalLink = isInternalEpubLink(href);

    // Special case: javascript:void(0) links with data attributes
    // Example: <a href="javascript:void(0)"
    // data-xyz="{&quot;name&quot;:&quot;OPS/ch2.xhtml&quot;,&quot;frag&quot;:&quot;id46&quot;}">
    if (href && strncmp(href, "javascript:", 11) == 0) {
      isInternalLink = false;
      // TODO: Parse data-* attributes to extract actual href
    }

    if (isInternalLink) {
      // Footnote indices are block-relative, so linked rows use ordinary flow.
      if (self->tableDepth >= 1 && self->insideTableCell && !self->tableStacked()) {
        self->fallbackTableRowToStacked();
      }

      // Flush buffer before style change
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      self->insideFootnoteLink = true;
      self->footnoteLinkDepth = self->depth;
      self->currentFootnoteLinkId = self->paragraphExists() ? self->addLinkTarget(href) : 0;
      self->currentFootnote.href[0] = '\0';
      if (self->currentFootnoteLinkId != 0) strcpy(self->currentFootnote.href, href);
      self->currentFootnote.number[0] = '\0';
      self->currentFootnoteLinkTextLen = 0;

      // Apply underline style to visually indicate the link.
      StyleStackEntry entry;
      entry.depth = self->depth;
      entry.hasTextDecoration = true;
      entry.textDecoration = CssTextDecoration::Underline;
      applyInlinePresentationToEntry(entry, cssStyle);
      applyDirectionToEntry(entry, cssStyle);
      applyVerticalAlignToEntry(entry, cssStyle);
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();

      // Skip CSS resolution — we already handled styling for this <a> tag
      self->depth += 1;
      return;
    }
  }

  const float emSize = static_cast<float>(self->bodyAscender());
  const auto userAlignmentBlockStyle = BlockStyle::fromCssStyle(
      cssStyle, emSize, static_cast<CssTextAlign>(self->paragraphAlignment), self->pageWidth());

  if (strcmp(name, "hr") == 0) {
    auto hrBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Left, self->pageWidth());
    if (!self->embeddedStyle) {
      hrBlockStyle.marginLeft = 0;
      hrBlockStyle.marginRight = 0;
      hrBlockStyle.marginTop = 0;
      hrBlockStyle.marginBottom = 0;
      hrBlockStyle.paddingLeft = 0;
      hrBlockStyle.paddingRight = 0;
      hrBlockStyle.paddingTop = 0;
      hrBlockStyle.paddingBottom = 0;
      hrBlockStyle.textIndentDefined = false;
      hrBlockStyle.textIndent = 0;
    }
    self->emitHorizontalRule(hrBlockStyle);
    self->depth += 1;
    return;
  }

  if (matches(name, HEADER_TAGS, std::size(HEADER_TAGS))) {
    // Text running straight into a block ("text<h3>…") is still in the part-word
    // buffer; flush it into the block it belongs to before that block is replaced.
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->currentCssStyle = cssStyle;
    auto headerBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Center, self->pageWidth());
    headerBlockStyle.textAlignDefined = true;
    if (self->embeddedStyle && cssStyle.hasTextAlign()) {
      headerBlockStyle.alignment = cssStyle.textAlign;
    }
    self->applyBlockFontScale(headerBlockStyle, cssStyle, name);
    headerBlockStyle.keepWithNext = true;
    self->openBoxScope(headerBlockStyle, cssStyle);
    const auto accumulated =
        self->blockStyleStack.back().getCombinedBlockStyle(headerBlockStyle, BlockStyle::CombineAxis::Horizontal);
    self->pushBlockStyle(accumulated);
    self->startNewTextBlock(accumulated.withoutBottom());
    self->pushBlockTextStyleEntry(cssStyle);
    self->armFirstLetterDropCap(name, classAttr, idAttr);
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS))) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      self->breakTextLine(self->blockStyleStack.back().withoutTop().withoutBottom());
    } else {
      // Flush pending text before the new block, as the header and <br/> branches do.
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->currentCssStyle = cssStyle;
      auto blockStyle = userAlignmentBlockStyle;
      self->applyBlockFontScale(blockStyle, cssStyle, name);
      self->openBoxScope(blockStyle, cssStyle);
      const auto accumulated =
          self->blockStyleStack.back().getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Horizontal);
      self->pushBlockStyle(accumulated);
      self->startNewTextBlock(accumulated.withoutBottom());
      if (!self->paragraphExists()) {
        // OOM: layoutOom is latched; bail before the <li> marker path below
        // dereferences the missing block. parseStep() fails the build.
        return;
      }
      self->pushBlockTextStyleEntry(cssStyle);
      self->updateEffectiveInlineStyle();
      if (strcmp(name, "li") != 0) self->armFirstLetterDropCap(name, classAttr, idAttr);

      if (strcmp(name, "li") == 0) {
        // Innermost open <ul>/<ol> (if any) decides whether this item gets a bullet,
        // a number, or no marker at all (list-style-type: none). A malformed <li>
        // with no enclosing list falls back to the plain bullet.
        if (!self->listStack.empty() && self->listStack.back().styleNone) {
          // No marker: leave the block empty so it behaves like a normal paragraph.
        } else if (!self->listStack.empty() && self->listStack.back().ordered) {
          self->listStack.back().counter += 1;
          char marker[16];
          snprintf(marker, sizeof(marker), "%d.", self->listStack.back().counter);
          self->addWord(marker, EpdFontFamily::REGULAR, false, false, self->visibleTextOffset);
          self->listItemBulletOnly = true;
        } else {
          self->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR, false, false, self->visibleTextOffset);
          self->listItemBulletOnly = true;
        }
      } else if (strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0) {
        // <ul>/<ol> container accumulation: pushing the block-style stack here includes
        // the container's own margin/padding in the inset children combine with, so a
        // hanging text-indent on <li><p> children keeps the first line on the page.
        ChapterSemantics::ListContext ctx;
        ctx.ordered = strcmp(name, "ol") == 0;
        ctx.styleNone = cssStyle.hasListStyleType() && cssStyle.listStyleType == CssListStyleType::None;
        ctx.depth = self->depth;
        self->listStack.push_back(ctx);
      }
    }
  } else if (matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->pushDecorationStyleEntry(CssTextDecoration::Underline, cssStyle);
  } else if (matches(name, LINETHROUGH_TAGS, std::size(LINETHROUGH_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->pushDecorationStyleEntry(CssTextDecoration::LineThrough, cssStyle);
  } else if (matches(name, BOLD_TAGS, std::size(BOLD_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    // Push inline style entry for bold tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasBold = true;
    entry.bold = true;
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    applyTextDecorationToEntry(entry, cssStyle);
    applyInlinePresentationToEntry(entry, cssStyle);
    applyDirectionToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    // Push inline style entry for italic tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasItalic = true;
    entry.italic = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    applyTextDecorationToEntry(entry, cssStyle);
    applyInlinePresentationToEntry(entry, cssStyle);
    applyDirectionToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "sup") == 0 || strcmp(name, "sub") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    StyleStackEntry entry;
    entry.depth = self->depth;
    if (strcmp(name, "sup") == 0) {
      entry.hasSup = true;
      entry.sup = true;
    } else {
      entry.hasSub = true;
      entry.sub = true;
    }
    applyInlinePresentationToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name)) {
    if (cssStyle.hasFontSize() && self->tableDepth == 0 && self->partWordBufferIndex == 0 && self->paragraphExists() &&
        self->paragraphEmpty() && (!self->inlineSize.valid || self->inlineSize.open)) {
      const float base = self->inlineSize.valid ? self->inlineSize.scale : self->paragraphStyle().fontScale;
      self->inlineSize.scale =
          std::clamp(cssStyle.fontSize.unit == CssUnit::Rem ? cssStyle.fontSize.value : base * cssStyle.fontSize.value,
                     0.5f, 3.0f);
      if (!self->inlineSize.valid) self->inlineSize.depth = self->depth;
      self->inlineSize.valid = true;
      self->inlineSize.open = true;
    }
    // A floated or oversized span opening a paragraph is a hand-made drop cap.
    if (self->tableDepth == 0 && self->dropCap.length == 0 && self->dropCap.spanDepth < 0 &&
        self->partWordBufferIndex == 0 && self->paragraphExists() && self->paragraphEmpty()) {
      const uint8_t lines = dropCapLines(cssStyle);
      if (lines > 0) {
        self->dropCap.spanDepth = self->depth;
        self->dropCap.lines = lines;
        self->dropCap.firstLetterPending = false;
        self->dropCap.bold =
            self->effectiveBold || (cssStyle.hasFontWeight() && cssStyle.fontWeight == CssFontWeight::Bold);
      }
    }
    // Handle span and other inline elements for CSS styling.
    const bool inheritedTableTextAlign = self->tableDepth >= 1 && cssStyle.hasTextAlign();
    // <small>/<big> default to the browser's relative sizes.
    const float tagScale = strcmp(name, "small") == 0 ? 0.83f : (strcmp(name, "big") == 0 ? 1.2f : 0.0f);
    const bool sized = cssStyle.hasFontSize() || tagScale > 0.0f;
    if (sized || cssStyle.hasFontWeight() || cssStyle.hasFontStyle() || cssStyle.hasTextDecoration() ||
        cssStyle.hasSmallCaps() || cssStyle.defined.whiteSpace || cssStyle.hasDirection() ||
        cssStyle.hasVerticalAlign() || inheritedTableTextAlign) {
      // Flush buffer before style change so preceding text gets current style
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      StyleStackEntry entry;
      entry.depth = self->depth;  // Track depth for matching pop
      if (cssStyle.hasFontWeight()) {
        entry.hasBold = true;
        entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
      }
      if (cssStyle.hasFontStyle()) {
        entry.hasItalic = true;
        entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
      }
      applyTextDecorationToEntry(entry, cssStyle);
      applyInlinePresentationToEntry(entry, cssStyle);
      applyDirectionToEntry(entry, cssStyle);
      entry.setsParagraphDirection = strcmp(name, "html") == 0 || strcmp(name, "body") == 0;
      if (inheritedTableTextAlign) {
        entry.hasTextAlign = true;
        entry.textAlign = cssStyle.textAlign;
      }
      applyVerticalAlignToEntry(entry, cssStyle);
      if (sized) {
        entry.hasFontScale = true;
        entry.fontScaleRem = cssStyle.hasFontSize() && cssStyle.fontSize.unit == CssUnit::Rem;
        entry.fontScale = cssStyle.hasFontSize() ? cssStyle.fontSize.value : tagScale;
      }
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();
    }
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void ChapterSemantics::characterData(void* userData, const char* s, const int len) {
  auto* self = static_cast<ChapterSemantics*>(userData);
  if (self->stopRequested) return;
  const bool countVisibleOffsets = self->insideBody && self->nonVisibleTextDepth == 0 && !self->syntheticCharacterData;
  const uint32_t callbackVisibleOffset = self->visibleTextOffset;
  if (countVisibleOffsets) {
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(s);
    const unsigned char* end = ptr + len;
    while (ptr < end) {
      utf8NextCodepoint(&ptr);
      self->visibleTextOffset++;
    }
  }

  // Nested content needs an enclosing bounded cell collector.
  if (self->tableDepth > 1 && !self->insideTableCell) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Collect ruby text instead of normal word processing.
  if (self->collectingRubyText) {
    self->rubyTextBuffer.append(s, len);
    return;
  }

  if (self->tableDepth == 1 && !self->insideTableCell) {
    bool onlyWhitespace = true;
    for (int i = 0; i < len; ++i) {
      if (!isWhitespace(s[i])) {
        onlyWhitespace = false;
        break;
      }
    }
    if (onlyWhitespace) {
      return;
    }
  }

  // Recreate flow storage for valid text (for example a caption) after a row.
  if (!self->paragraphExists()) {
    const BlockStyle flowStyle =
        self->blockStyleStack.empty() ? BlockStyle() : self->blockStyleStack.back().withoutBottom();
    self->createParagraph(flowStyle);
    if (!self->paragraphExists()) {
      LOG_ERR("EHP", "OOM: text block for character data");
      return;
    }
    self->resetExtractedWords();
  }

  // Collect footnote link display text (for the number label)
  // Skip whitespace and brackets to normalize noterefs like "[1]" → "1"
  if (self->insideFootnoteLink) {
    int start = 0;
    int end = len - 1;

    // Example input and output texts:
    // "     [  12  ]   " => "12"
    // "   turn to 256  " => "turn to 256"

    // Ignore leading whitespaces and left square brackets
    while (start < len && (isWhitespace(s[start]) || (s[start] == '['))) {
      ++start;
    }

    // Ignore trailing whitespaces and right square brackets
    while (end >= start && (isWhitespace(s[end]) || (s[end] == ']'))) {
      --end;
    }

    // Extract footnote link text
    for (int i = start; (self->currentFootnoteLinkTextLen < sizeof(self->currentFootnote.number) - 1) && (i <= end);
         ++i) {
      self->currentFootnote.number[self->currentFootnoteLinkTextLen++] = s[i];
    }
    self->currentFootnote.number[self->currentFootnoteLinkTextLen] = '\0';
  }

  if (self->inlineSize.valid && !self->inlineSize.open &&
      std::any_of(s, s + len, [](const char c) { return !isWhitespace(c); })) {
    self->inlineSize.valid = false;
  }

  uint32_t nextCodepointOffset = callbackVisibleOffset;
  for (int i = 0; i < len; i++) {
    const uint32_t codepointOffset = nextCodepointOffset;
    if (countVisibleOffsets && (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) {
      nextCodepointOffset++;
    }

    // Initial-letter capture: a drop-cap span takes all of its text; ::first-letter takes
    // leading punctuation plus the block's first letter.
    const bool capturingSpan = self->dropCap.spanDepth >= 0;
    if (capturingSpan || (self->dropCap.firstLetterPending && !isWhitespace(s[i]) && self->partWordBufferIndex == 0 &&
                          !self->insideFootnoteLink && self->paragraphEmpty())) {
      if (isWhitespace(s[i])) {
        // Several words: not an initial letter after all.
        if (self->dropCap.length > 0) self->cancelDropCapToWord();
      } else {
        const auto lead = static_cast<uint8_t>(s[i]);
        const int codepointLength = std::min(len - i, lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4)));
        if (self->captureDropCapCodepoint(s + i, codepointLength)) {
          i += codepointLength - 1;
          continue;
        }
      }
    }

    if (self->effectivePreserveWhitespace && isWhitespace(s[i])) {
      self->flushPartWordBuffer();
      self->suppressFirstLineIndent();
      if (s[i] == '\n') {
        const BlockStyle style = self->paragraphStyle().withoutTop().withoutBottom();
        self->breakTextLine(style);
        if (!self->paragraphExists()) return;
      } else if (s[i] != '\r') {
        // XML normalizes line endings; tabs use a bounded four-space expansion.
        const int count = s[i] == '\t' ? 4 : 1;
        for (int space = 0; space < count; ++space) {
          self->partWordBuffer[0] = ' ';
          self->partWordBufferIndex = 1;
          self->partWordVisibleOffset = codepointOffset;
          self->flushPartWordBuffer();
        }
      }
      continue;
    }

    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      // Skip the whitespace char
      continue;
    }

    // Detect U+00A0 (non-breaking space, UTF-8: 0xC2 0xA0) or
    //        U+202F (narrow no-break space, UTF-8: 0xE2 0x80 0xAF).
    //
    // Both are rendered as a visible space but must never allow a line break around them.
    // We split the no-break space into its own word token and link the surrounding words
    // with continuation flags so the layout engine treats them as an indivisible group.
    //
    // Example: "200&#xA0;Quadratkilometer" or "200&#x202F;Quadratkilometer"
    //   Input bytes:  "200\xC2\xA0Quadratkilometer"  (or 0xE2 0x80 0xAF for U+202F)
    //   Tokens produced:
    //     [0] "200"               continues=false
    //     [1] " "                 continues=true   (attaches to "200", no gap)
    //     [2] "Quadratkilometer"  continues=true   (attaches to " ", no gap)
    //
    //   The continuation flags prevent the line-breaker from inserting a line break
    //   between "200" and "Quadratkilometer". However, "Quadratkilometer" is now a
    //   standalone word for hyphenation purposes, so Liang patterns can produce
    //   "200 Quadrat-" / "kilometer" instead of the unusable "200" / "Quadratkilometer".
    if (static_cast<uint8_t>(s[i]) == 0xC2 && i + 1 < len && static_cast<uint8_t>(s[i + 1]) == 0xA0) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->partWordVisibleOffset = codepointOffset;
      self->nextWordContinues = true;  // Attach space to previous word (no break).
      self->flushPartWordBuffer();

      self->nextWordContinues = true;  // Next real word attaches to this space (no break).

      i++;  // Skip the second byte (0xA0)
      continue;
    }

    // U+202F (narrow no-break space) — identical logic to U+00A0 above.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0xAF) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->partWordVisibleOffset = codepointOffset;
      self->nextWordContinues = true;
      self->flushPartWordBuffer();

      self->nextWordContinues = true;

      i += 2;  // Skip the remaining two bytes (0x80 0xAF)
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const char FEFF_BYTE_1 = static_cast<char>(0xEF);
    const char FEFF_BYTE_2 = static_cast<char>(0xBB);
    const char FEFF_BYTE_3 = static_cast<char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one.
    // For CJK text (no spaces), this is the primary word-breaking mechanism.
    // We must avoid splitting multi-byte UTF-8 sequences across word boundaries,
    // otherwise the trailing bytes become orphaned continuation bytes that the
    // decoder can't interpret.
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      int safeLen = utf8SafeTruncateBuffer(self->partWordBuffer, self->partWordBufferIndex);

      if (safeLen < self->partWordBufferIndex && safeLen > 0) {
        // Incomplete UTF-8 sequence at the end — save it before flushing
        int overflow = self->partWordBufferIndex - safeLen;
        uint32_t overflowVisibleOffset = self->partWordVisibleOffset;
        const unsigned char* offsetPtr = reinterpret_cast<const unsigned char*>(self->partWordBuffer);
        const unsigned char* const safeEnd = offsetPtr + safeLen;
        while (offsetPtr < safeEnd) {
          utf8NextCodepoint(&offsetPtr);
          overflowVisibleOffset++;
        }
        char saved[4];
        for (int j = 0; j < overflow; j++) {
          saved[j] = self->partWordBuffer[safeLen + j];
        }
        self->partWordBufferIndex = safeLen;
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
        for (int j = 0; j < overflow; j++) {
          self->partWordBuffer[j] = saved[j];
        }
        self->partWordBufferIndex = overflow;
        self->partWordVisibleOffset = overflowVisibleOffset;
      } else {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
    }

    if (self->partWordBufferIndex == 0) {
      self->partWordVisibleOffset = codepointOffset;
    }
    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // Block creation failed (OOM): nothing to soft-flush.
  if (!self->paragraphExists()) {
    return;
  }

  // Keep token growth bounded: CSS-heavy spans can fragment text into many tiny
  // words, so flush earlier when embedded CSS is active. We still keep the
  // "exclude last line" behavior to preserve paragraph flow across chunks.
  const size_t blockWordCount = self->paragraphSize();
  const size_t softFlushThreshold =
      self->embeddedStyle ? TEXT_BLOCK_SOFT_FLUSH_WORDS_WITH_CSS : TEXT_BLOCK_SOFT_FLUSH_WORDS;
  if (blockWordCount > softFlushThreshold && !self->inRuby) {
    LOG_DBG("EHP", "Text block soft flush (%u words)", static_cast<unsigned>(blockWordCount));
    self->makePages(/*includeLastLine=*/false);
  }
}

void ChapterSemantics::defaultHandlerExpand(void* userData, const char* s, const int len) {
  // Check if this looks like an entity reference (&...;)
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (utf8Value != nullptr) {
      // Known entity: expand to its UTF-8 value
      characterData(userData, utf8Value, strlen(utf8Value));
      return;
    }
    // Unknown entity: preserve original &...; sequence
    characterData(userData, s, len);
    return;
  }
  // Not an entity we recognize - skip it
}

void ChapterSemantics::endElement(void* userData, const char* name) {
  auto* self = static_cast<ChapterSemantics*>(userData);
  if (self->stopRequested) return;
  if (self->nonVisibleTextDepth > 0) {
    self->nonVisibleTextDepth--;
  }

  // Ruby text: </rt> distributes ruby to base words, </ruby> resets ruby state
  if (strcmp(name, "rt") == 0) {
    self->collectingRubyText = false;
    if (self->inRuby && self->paragraphExists()) {
      const int currentWordCount = static_cast<int>(self->paragraphSize());
      const int baseWordCount = currentWordCount - self->rubyStartWordIndex;
      std::string cleanRuby = trimAndNormalize(self->rubyTextBuffer);
      if (!cleanRuby.empty()) {
        if (baseWordCount > 0) {
          self->setRubyGroupAt(self->rubyStartWordIndex, baseWordCount, cleanRuby);
          self->rubyStartWordIndex = currentWordCount;
        } else if (self->rubyStartWordIndex > 0) {
          int leaderIdx = self->rubyStartWordIndex - 1;
          while (leaderIdx >= 0 && (self->getWordStyleAt(leaderIdx) & EpdFontFamily::RUBY_CONTINUE) != 0) {
            leaderIdx--;
          }
          if (leaderIdx >= 0) {
            std::string prevRuby = self->getRubyTextAt(leaderIdx);
            self->setRubyForWordAt(leaderIdx, prevRuby + cleanRuby);
          }
        }
      }
    }
    self->rubyTextBuffer.clear();
    // Inline close: the next base (e.g. 字 in <ruby>漢<rt>かん</rt>字<rt>じ</rt></ruby>) joins the
    // preceding one with no space. Whitespace in the source resets this in characterData().
    if (self->paragraphExists() && !self->paragraphEmpty()) {
      self->nextWordContinues = true;
    }
    self->depth -= 1;
    return;
  }
  if (strcmp(name, "ruby") == 0 && self->inRuby) {
    self->inRuby = false;
    self->rubyStartWordIndex = -1;
    self->rubyTextBuffer.clear();
    // Inline close: text following </ruby> joins the annotated base with no space.
    if (self->paragraphExists() && !self->paragraphEmpty()) {
      self->nextWordContinues = true;
    }
    self->depth -= 1;
    return;
  }
  // Check if any style state will change after we decrement depth
  // If so, we MUST flush the partWordBuffer with the CURRENT style first
  // Note: depth hasn't been decremented yet, so we check against (depth - 1)
  const bool willPopStyleStack =
      !self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth - 1;
  const bool willClearBold = self->boldUntilDepth == self->depth - 1;
  const bool willClearItalic = self->italicUntilDepth == self->depth - 1;

  const bool styleWillChange = willPopStyleStack || willClearBold || willClearItalic;
  const bool headerOrBlockTag = isHeaderOrBlock(name);
  const bool tableStructuralTag = isTableStructuralTag(name);
  const bool insideSkippedSubtree = self->depth - 1 >= self->skipUntilDepth;

  if (!insideSkippedSubtree && self->tableDepth > 1 && strcmp(name, "table") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->nextWordContinues = false;
    self->tableDepth -= 1;
    self->depth -= 1;
    LOG_DBG("EHP", "nested table flattened into enclosing cell");
    return;
  }

  // Flush buffer with current style BEFORE any style changes
  if (self->partWordBufferIndex > 0) {
    // Flush if style will change OR if we're closing a block/structural element
    const bool isInlineTag = !headerOrBlockTag && !tableStructuralTag &&
                             !matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) && self->depth != 1;
    const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, std::size(BOLD_TAGS)) ||
                             matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS)) ||
                             matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS)) ||
                             matches(name, LINETHROUGH_TAGS, std::size(LINETHROUGH_TAGS)) || tableStructuralTag ||
                             matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) || self->depth == 1;

    if (shouldFlush) {
      self->flushPartWordBuffer();
      // If closing an inline element, the next word fragment continues the same visual word
      if (isInlineTag) {
        self->nextWordContinues = true;
      }
    }
  }

  // A captured letter without body text is ordinary block content.
  if (headerOrBlockTag && strcmp(name, "br") != 0 && !insideSkippedSubtree && self->paragraphExists() &&
      self->paragraphEmpty() && self->dropCap.length > 0) {
    self->cancelDropCapToWord();
    self->flushPartWordBuffer();
  }

  self->depth -= 1;

  // The captured drop-cap letter waits for its block's layout.
  if (self->dropCap.spanDepth == self->depth) {
    self->dropCap.spanDepth = -1;
  }
  if (self->inlineSize.valid && self->inlineSize.depth == self->depth) {
    self->inlineSize.open = false;
  }

  // Closing a footnote link — create entry from collected text and href
  if (self->insideFootnoteLink && self->depth == self->footnoteLinkDepth) {
    if (self->currentFootnote.number[0] != '\0' && self->currentFootnote.href[0] != '\0') {
      FootnoteEntry entry;
      strncpy(entry.number, self->currentFootnote.number, sizeof(entry.number) - 1);
      entry.number[sizeof(entry.number) - 1] = '\0';
      strncpy(entry.href, self->currentFootnote.href, sizeof(entry.href) - 1);
      entry.href[sizeof(entry.href) - 1] = '\0';
      int wordIndex = self->extractedWords() + (self->paragraphExists() ? static_cast<int>(self->paragraphSize()) : 0);
      self->addPendingFootnote(wordIndex, entry);
    }
    self->insideFootnoteLink = false;
    self->currentFootnoteLinkId = 0;
  }

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (!insideSkippedSubtree && self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    self->closeTableCell();
    self->nextWordContinues = false;
  }

  if (!insideSkippedSubtree && self->tableDepth == 1 && (strcmp(name, "tr") == 0)) {
    self->finishTableRow();
    self->nextWordContinues = false;
  }

  if (!insideSkippedSubtree && self->tableDepth == 1 && strcmp(name, "table") == 0) {
    self->finishTableRow();
    if (self->paragraphExists() && !self->paragraphEmpty()) {
      self->makePages();
    }
    self->resetParagraph();
    self->tableDepth = 0;
    self->insideTableCell = false;
    self->setTableStacked(false);
    self->tableRowsSpannedRemaining = 0;
    self->resetTableBytes();
    self->clearTableCells();
    self->nextWordContinues = false;

    const BlockStyle flowStyle =
        self->blockStyleStack.empty() ? BlockStyle() : self->blockStyleStack.back().withoutBottom();
    self->createParagraph(flowStyle);
    if (!self->paragraphExists()) {
      LOG_ERR("EHP", "OOM: text block after table");
    }
    self->resetExtractedWords();
  }

  // Leaving bold tag
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  // Leaving italic tag
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }

  // Pop from inline style stack if we pushed an entry at this depth
  // This handles all inline elements: b, i, u, span, etc.
  if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();
  }

  // Clear block style when leaving header or block elements
  if (headerOrBlockTag && !insideSkippedSubtree) {
    self->currentCssStyle.reset();
    self->updateEffectiveInlineStyle();

    // br is self-closing and not a container — it doesn't push/pop the stack.
    if (strcmp(name, "br") != 0 && self->blockStyleStack.size() > 1) {
      // Apply closing element's bottom margin to the current text block so
      // container spacing appears after the element's content (on the last child),
      // not on the first child via the empty-block merge in startNewTextBlock.
      if (self->paragraphExists()) {
        const auto style = self->paragraphStyle();
        self->setParagraphStyle(style.addBottom(self->blockStyleStack.back()));
      }
      const bool breakAfter = self->blockStyleStack.back().pageBreakAfter;
      self->blockStyleStack.pop_back();
      // Start a new text block with the parent style to prevent subsequent bare text
      // from inheriting the closed block style (e.g. alignment or margins).
      // Vertical margins and paddings are stripped
      self->startNewTextBlock(self->blockStyleStack.back().withoutTop().withoutBottom());
      self->updateEffectiveInlineStyle();
      if (self->boxAtDepth(self->depth)) self->closeBoxScope();
      // Set after the closing element's own text was laid out above.
      if (breakAfter) self->pendingPageBreak = true;
    }

    // </li> closes: if the bullet never got inline text (empty <li> or <li> with only
    // block children that were flushed), clear the flag so the next sibling doesn't
    // merge into this block.
    if (strcmp(name, "li") == 0) {
      self->listItemBulletOnly = false;
    }
  }

  // </ul> or </ol> closes: pop its list context so a following sibling list at the
  // same nesting level starts its own counter/style instead of inheriting this one's.
  // Guarded by depth (not just tag name + non-empty stack): a display:none <ul>/<ol>
  // returns early in startElement without ever pushing a context (see the
  // hasDisplay()/CssDisplay::None branch above), so its closing tag must not pop
  // the *parent* list's context out from under still-unprocessed siblings.
  if ((strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0) && !self->listStack.empty() &&
      self->listStack.back().depth == self->depth) {
    self->listStack.pop_back();
  }
  if (strcmp(name, "body") == 0) {
    self->insideBody = false;
  }
  if (strcmp(name, "html") == 0) {
    self->htmlEnded_ = true;
  }
}

void ChapterSemantics::flushPartWordBuffer() {
  // Block creation failed (OOM): drop the buffered text; parseStep() is about
  // to fail the build via layoutOom.
  if (!paragraphExists()) {
    partWordBufferIndex = 0;
    nextWordContinues = false;
    return;
  }

  if (effectivePreserveWhitespace) {
    suppressFirstLineIndent();
    auto& style = paragraphStyle();
    if (style.alignment == CssTextAlign::Justify) style.alignment = CssTextAlign::Left;
  }

  // Determine font style from depth-based tracking and CSS effective style
  const bool isBold = boldUntilDepth < depth || effectiveBold;
  const bool isItalic = italicUntilDepth < depth || effectiveItalic;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | fontStyleForTextDecoration(effectiveTextDecoration));
  if (effectiveSmallCaps) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SMALL_CAPS);
  }
  if (effectiveSup) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUP);
  } else if (effectiveSub) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUB);
  }

  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  const size_t wordBytes = static_cast<size_t>(partWordBufferIndex);
  if (insideTableCell && !tableStacked() && tableBytes() + wordBytes > MAX_GRID_TABLE_ROW_BYTES) {
    fallbackTableRowToStacked();
  }

  uint8_t linkId = 0;
  if (insideFootnoteLink) {
    if (!linkTargetMatches(currentFootnoteLinkId, currentFootnote.href)) {
      currentFootnoteLinkId = addLinkTarget(currentFootnote.href);
    }
    linkId = currentFootnoteLinkId;
  }
  // Inline font-size spans size their words apart from the block.
  float inlineScale = 0.0f;
  for (const auto& entry : inlineStyleStack) {
    if (!entry.hasFontScale) continue;
    const float base = inlineScale > 0.0f ? inlineScale : paragraphStyle().fontScale;
    inlineScale = entry.fontScaleRem ? entry.fontScale : base * entry.fontScale;
  }
  const uint8_t sizeSlot = inlineScale > 0.0f && (inlineSize.valid || inlineScale != paragraphStyle().fontScale)
                               ? sizeSlotFor(std::clamp(inlineScale, 0.5f, 3.0f))
                               : 0;
  addWord(partWordBuffer, fontStyle, false, nextWordContinues, partWordVisibleOffset, linkId, sizeSlot,
          effectivePreserveWhitespace);
  if (insideTableCell && !tableStacked()) {
    addTableBytes(wordBytes);
    size_t rowWords = paragraphSize();
    rowWords += bufferedTableWords();
    if (rowWords > MAX_GRID_TABLE_ROW_WORDS) {
      fallbackTableRowToStacked();
    }
  }
  partWordBufferIndex = 0;
  nextWordContinues = false;
  listItemBulletOnly = false;
}

bool ChapterSemantics::measureTableColumns(ChapterTableSource& file, TableColumnMeasure& out) const {
  out = TableColumnMeasure{};
  constexpr size_t MAX_SCAN_BYTES = 32 * 1024;
  const bool sdFont = bodyIsSdFont();
  const int spaceWidth = bodySpaceWidth();

  char buffer[96];
  char word[40];
  size_t wordLength = 0;
  int wordWidth = 0;  // flushed chunks of a word longer than the buffer
  char tagText[48];
  size_t tagLength = 0;
  bool inTag = false;
  bool inEntity = false;
  char quote = 0;
  int tableNesting = 0;
  int column = -1;
  bool inCell = false;
  bool skipCell = false;
  int lineWidth = 0;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;

  const auto flushChunk = [&] {
    if (wordLength == 0) return;
    word[wordLength] = '\0';
    if (sdFont) prepareBodyText(word, static_cast<uint8_t>(1u << style));
    wordWidth += bodyTextAdvance(word, style);
    wordLength = 0;
  };
  const auto finishWord = [&] {
    flushChunk();
    if (wordWidth == 0 || column < 0) return;
    out.minWidth[column] = static_cast<uint16_t>(std::max<int>(out.minWidth[column], wordWidth));
    lineWidth += (lineWidth > 0 ? spaceWidth : 0) + wordWidth;
    wordWidth = 0;
  };
  const auto finishLine = [&] {
    finishWord();
    if (column >= 0) out.prefWidth[column] = static_cast<uint16_t>(std::max<int>(out.prefWidth[column], lineWidth));
    lineWidth = 0;
  };
  // Returns 1 when the table ends, -1 when it cannot be planned, 0 otherwise.
  const auto handleTag = [&]() -> int {
    tagText[tagLength] = '\0';
    const bool closing = tagText[0] == '/';
    const char* name = tagText + (closing ? 1 : 0);
    size_t nameLength = 0;
    while (name[nameLength] && !isWhitespace(name[nameLength]) && name[nameLength] != '/') ++nameLength;
    const std::string_view tag(name, nameLength);
    if (tag == "table") {
      tableNesting += closing ? -1 : 1;
      if (tableNesting == 0) return 1;
      return 0;
    }
    if (tableNesting != 1) {
      finishWord();
      return 0;
    }
    if (tag == "tr" && !closing) {
      if (inCell) finishLine();
      inCell = false;
      column = -1;
    } else if (tag == "td" || tag == "th") {
      if (inCell) finishLine();
      inCell = !closing;
      if (!closing) {
        // Spanning cells lay out as stacked rows, so they don't shape the grid.
        skipCell = strstr(tagText, "colspan") != nullptr || strstr(tagText, "rowspan") != nullptr;
        if (++column >= static_cast<int>(MAX_GRID_TABLE_COLUMNS)) return -1;
        out.columns = static_cast<uint8_t>(std::max<int>(out.columns, column + 1));
        style = tag == "th" ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      }
    } else if (tag == "br") {
      finishLine();
    } else if (tag == "p" || tag == "div" || tag == "li" || (tag.size() == 2 && tag[0] == 'h')) {
      finishWord();  // block markup inside a cell collapses to a word boundary
    }
    return 0;
  };

  size_t scanned = 0;
  while (scanned < MAX_SCAN_BYTES) {
    const size_t count = file.read(buffer, sizeof(buffer));
    if (count == 0) return false;
    scanned += count;
    for (size_t i = 0; i < count; ++i) {
      const char c = buffer[i];
      if (inTag) {
        if (quote) {
          if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
          quote = c;
        } else if (c == '>') {
          inTag = false;
          const int result = handleTag();
          if (result != 0) return result > 0 && out.columns >= 2;
          continue;
        }
        if (tagLength + 1 < sizeof(tagText)) {
          tagText[tagLength++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        continue;
      }
      if (c == '<') {
        inTag = true;
        tagLength = 0;
        continue;
      }
      if (!inCell || skipCell || tableNesting != 1) continue;
      if (inEntity) {
        inEntity = c != ';';
        continue;
      }
      if (isWhitespace(c)) {
        finishWord();
        continue;
      }
      if (wordLength + 1 >= sizeof(word)) flushChunk();
      // An entity measures roughly as one character.
      inEntity = c == '&';
      word[wordLength++] = inEntity ? 'n' : c;
    }
  }
  return false;
}

void ChapterSemantics::planTableColumns(const TableColumnMeasure& measure) {
  tableColumnCount = 0;
  const int columns = measure.columns;
  if (columns < 2 || columns > static_cast<int>(MAX_GRID_TABLE_COLUMNS)) return;
  constexpr int padding = TABLE_CELL_HORIZONTAL_PADDING * 2;
  const int available = pageWidth();
  int minWidth[MAX_GRID_TABLE_COLUMNS];
  int prefWidth[MAX_GRID_TABLE_COLUMNS];
  int minTotal = 0;
  int prefTotal = 0;
  for (int c = 0; c < columns; ++c) {
    minWidth[c] = measure.minWidth[c] + padding;
    prefWidth[c] = std::max(minWidth[c], measure.prefWidth[c] + padding);
    minTotal += minWidth[c];
    prefTotal += prefWidth[c];
  }
  int assigned = 0;
  for (int c = 0; c < columns; ++c) {
    int width;
    if (prefTotal <= available) {
      width = prefWidth[c] + (available - prefTotal) * prefWidth[c] / prefTotal;
    } else if (minTotal <= available) {
      width = minWidth[c] + (available - minTotal) * (prefWidth[c] - minWidth[c]) / (prefTotal - minTotal);
    } else {
      width = available * minWidth[c] / minTotal;
    }
    if (c + 1 == columns) width = available - assigned;
    tableColumnWidths[c] = static_cast<uint16_t>(std::max(width, padding + 1));
    assigned += width;
  }
  tableColumnCount = static_cast<uint8_t>(columns);
}
