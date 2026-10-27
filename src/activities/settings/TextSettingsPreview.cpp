#include "TextSettingsPreview.h"

#include <EpdFontFamily.h>
#include <Epub/Page.h>
#include <Epub/Paragraph.h>
#include <Epub/blocks/BlockStyle.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace textsettings {

PreviewLayout::PreviewLayout() = default;
PreviewLayout::~PreviewLayout() = default;

namespace {

// Map the paragraph-alignment setting to the engine's CssTextAlign (BOOK_STYLE = justified)
CssTextAlign toCssAlign(uint8_t align) {
  if (align == CrossPointSettings::BOOK_STYLE) return CssTextAlign::Justify;
  return static_cast<CssTextAlign>(align);
}

// Store only whole visible lines from the two sample paragraphs.
bool relayout(PreviewLayout& layout, const GfxRenderer& renderer, int fontId, int textWidth, int textHeight, int lineH,
              int lineAdvance, int paragraphGap) {
  layout.page.reset();

  BlockStyle style;
  style.alignment = toCssAlign(SETTINGS.paragraphAlignment);
  style.textAlignDefined = true;  // honor the user's choice; RTL auto-detected from text

  Paragraph parsed(SETTINGS.hyphenationEnabled != 0, SETTINGS.focusReadingEnabled != 0, style,
                   SETTINGS.paragraphIndentSpaces);

  // Feed one space-separated word at a time; addWord handles NFC/CJK/RTL/focus splitting
  const char* text = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  std::string word;
  for (const char* p = text;; p++) {
    if (*p == ' ' || *p == '\0') {
      if (!word.empty()) {
        parsed.addWord(word, EpdFontFamily::REGULAR);
        word.clear();
      }
      if (*p == '\0') break;
    } else {
      word.push_back(*p);
    }
  }

  const int visibleLines = textHeight < lineH ? 0 : (textHeight - lineH) / lineAdvance + 1;
  size_t wordBytes = 0;
  for (size_t i = 0; i < parsed.size(); ++i) wordBytes += parsed.wordAt(i).size() + 1;
  // Two copies of the words, one indentation token per paragraph, and one split
  // word per visible line. Four bytes cover each extra token's UTF-8 text and NUL.
  const size_t extraWords = 2 + size_t(visibleLines);
  const size_t capacity = LinePageBuilder::capacityForLines(visibleLines, 2 * parsed.size() + extraWords,
                                                            2 * wordBytes + 4 * extraWords, true);
  if (capacity == SIZE_MAX) return false;
  LinePageBuilder builder(capacity);
  struct Lines {
    LinePageBuilder& builder;
    Paragraph& paragraph;
    int row, lineH, advance, gap, height;
  } lines{builder, parsed, 0, lineH, lineAdvance, paragraphGap, textHeight};
  parsed.layoutLines(
      renderer, fontId, static_cast<uint16_t>(textWidth), &lines,
      [](void* raw, const XpParaLine* line) -> uint8_t {
        auto& c = *static_cast<Lines*>(raw);
        const int firstY = c.row * c.advance;
        const int secondY = (c.row + c.paragraph.linesAfterCurrentLine() + 1) * c.advance + c.gap + firstY;
        bool ok = true;
        if (firstY + c.lineH <= c.height) ok = c.builder.addLine(*line, 0, firstY);
        if (ok && secondY + c.lineH <= c.height) ok = c.builder.addLine(*line, 0, secondY);
        ++c.row;
        return ok;
      },
      true, SETTINGS.getCharacterSpacing(), SETTINGS.wordSpacing);
  if (parsed.hadDroppedWords()) return false;
  layout.page = builder.finish();
  return layout.page != nullptr;
}

}  // namespace

void renderPreview(GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, Rect bounds,
                   const char* caption) {
  const int top = bounds.y;
  const int height = bounds.height;
  const int left = bounds.x + previewPadding;
  const int width = bounds.width - (previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelReserved = labelH + labelGap + previewPadding;

  const int labelY = top + height - previewPadding - labelH;
  renderer.drawText(UI_10_FONT_ID, left, labelY, caption);

  const int fontId = SETTINGS.getReaderFontId();
  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int textLeft = left + SETTINGS.screenMargin;
  const int textWidth = width - 2 * SETTINGS.screenMargin;
  if (textWidth <= 0) return;

  const float compression = SETTINGS.getReaderLineCompression();
  const int lineAdvance = std::max(1, renderer.getLineHeight(fontId, compression));
  const int paragraphGap = SETTINGS.extraParagraphSpacing ? lineAdvance / 2 : 0;

  // Re-lay-out (and re-prewarm glyphs) only when a layout-affecting setting or the
  // geometry changed; else reuse the cache. The prewarm inputs are (fontId, constant
  // sample text, styleMask<-focusReading), all of which are key fields, so a matching
  // key means an identical prewarm call. This relies on nothing else evicting the SD
  // glyph cache while this activity is up — true today: the only evictor is
  // FontCacheManager::PrewarmScope, used solely by the reader/dictionary activities.
  const int textHeight = height - labelReserved - previewPadding;
  const PreviewKey key{.fontId = fontId,
                       .fontPointSize = SETTINGS.fontPointSize,
                       .screenMargin = SETTINGS.screenMargin,
                       .textWidth = textWidth,
                       .textHeight = textHeight,
                       .lineCompression = compression,
                       .alignment = SETTINGS.paragraphAlignment,
                       .extraParagraphSpacing = SETTINGS.extraParagraphSpacing != 0,
                       .paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces,
                       .characterSpacing = SETTINGS.getCharacterSpacing(),
                       .wordSpacingPercent = SETTINGS.wordSpacing,
                       .focusReading = SETTINGS.focusReadingEnabled != 0,
                       .hyphenation = SETTINGS.hyphenationEnabled != 0};
  if (key != layout.key || !layout.page) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->prewarmCache(fontId, I18N.get(StrId::STR_FONT_PREVIEW_TEXT), SETTINGS.focusReadingEnabled ? 0x03 : 0x01);
    }
    if (!relayout(layout, renderer, fontId, textWidth, textHeight, lineH, lineAdvance, paragraphGap)) {
      LOG_ERR("TXT", "Cannot build text preview page");
      return;
    }
    layout.key = key;
  }

  if (layout.page) layout.page->render(renderer, fontId, textLeft, top + previewPadding);
}

}  // namespace textsettings
