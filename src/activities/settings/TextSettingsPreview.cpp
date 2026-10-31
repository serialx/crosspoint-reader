#include "TextSettingsPreview.h"

#include <Epub/Page.h>
#include <Epub/Paragraph.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "fontIds.h"
namespace textsettings {
PreviewLayout::PreviewLayout() = default;
PreviewLayout::~PreviewLayout() = default;
namespace {
bool relayout(PreviewLayout& layout, const GfxRenderer& renderer, const XpUiTextPreviewKey& key,
              const XpUiTextPreviewGeometry& geometry, int lineHeight) {
  layout.page.reset();
  BlockStyle style;
  style.alignment = static_cast<CssTextAlign>(xp_ui_text_preview_align(key.alignment));
  style.textAlignDefined = true;
  Paragraph paragraph(key.hyphenation, key.focus, style, key.indent);
  xp_ui_text_preview_feed(paragraph.handle(), I18N.get(StrId::STR_FONT_PREVIEW_TEXT));
  const auto capacity = xp_ui_text_preview_capacity(paragraph.handle(), key.height, lineHeight, geometry.advance);
  if (capacity == SIZE_MAX) return false;
  LinePageBuilder builder(capacity);
  struct Lines {
    LinePageBuilder& builder;
    Paragraph& paragraph;
    int row, height, lineHeight, advance, gap;
  } lines{builder, paragraph, 0, key.height, lineHeight, geometry.advance, geometry.gap};
  paragraph.layoutLines(
      renderer, key.font_id, key.width, &lines,
      [](void* raw, const XpParaLine* line) -> uint8_t {
        auto& c = *static_cast<Lines*>(raw);
        int16_t y[2]{};
        const uint8_t visible = xp_ui_text_preview_line(c.row++, c.paragraph.linesAfterCurrentLine(), c.height,
                                                        c.lineHeight, c.advance, c.gap, y);
        if ((visible & 1) && !c.builder.addLine(*line, 0, y[0])) return false;
        return !(visible & 2) || c.builder.addLine(*line, 0, y[1]);
      },
      true, key.tracking, key.word_spacing);
  if (paragraph.hadDroppedWords()) return false;
  layout.page = builder.finish();
  return layout.page != nullptr;
}
}  // namespace
void renderPreview(GfxRenderer& renderer, PreviewLayout& layout, int padding, int labelGap, Rect bounds,
                   const char* caption) {
  XpUiTextPreviewKey key{};
  key.font_id = SETTINGS.getReaderFontId();
  key.point_size = SETTINGS.fontPointSize;
  key.margin = SETTINGS.screenMargin;
  key.compression = SETTINGS.getReaderLineCompression();
  key.alignment = SETTINGS.paragraphAlignment;
  key.paragraph_gap = SETTINGS.extraParagraphSpacing != 0;
  key.indent = SETTINGS.paragraphIndentSpaces;
  key.tracking = SETTINGS.getCharacterSpacing();
  key.word_spacing = SETTINGS.wordSpacing;
  key.focus = SETTINGS.focusReadingEnabled != 0;
  key.hyphenation = SETTINGS.hyphenationEnabled != 0;
  const int lineHeight = key.font_id ? renderer.getTextHeight(key.font_id) : 0;
  XpUiTextPreviewGeometry geometry{};
  xp_ui_text_preview_geometry({static_cast<int16_t>(bounds.x), static_cast<int16_t>(bounds.y),
                               static_cast<int16_t>(bounds.width), static_cast<int16_t>(bounds.height)},
                              padding, labelGap, renderer.getTextHeight(UI_10_FONT_ID), lineHeight,
                              key.font_id ? renderer.getLineHeight(key.font_id, key.compression) : 0, &key, &geometry);
  if (geometry.caption_visible) renderer.drawText(UI_10_FONT_ID, geometry.caption.x, geometry.caption.y, caption);
  if (!geometry.text_visible) return;
  key.width = geometry.text.w;
  key.height = geometry.text.h;
  if (!xp_ui_text_preview_same(&key, &layout.key) || !layout.page) {
    if (auto* cache = renderer.getFontCacheManager())
      cache->prewarmCache(key.font_id, I18N.get(StrId::STR_FONT_PREVIEW_TEXT), key.focus ? 0x03 : 0x01);
    if (!relayout(layout, renderer, key, geometry, lineHeight)) {
      LOG_ERR("TXT", "Cannot build text preview page");
      return;
    }
    layout.key = key;
  }
  if (layout.page) layout.page->render(renderer, key.font_id, geometry.text.x, geometry.text.y);
}
}  // namespace textsettings
