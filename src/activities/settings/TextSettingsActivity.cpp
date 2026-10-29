#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
namespace fui = freeink::ui;
namespace {
static_assert(sizeof(XpUiTextState) == 180 && alignof(XpUiTextState) == 4);
static_assert(offsetof(XpUiTextState, tabs) == 0);
static_assert(offsetof(XpUiTextState, last_repeat) == 160);
static_assert(offsetof(XpUiTextState, font_count) == 164);
static_assert(offsetof(XpUiTextState, size_count) == 166);
static_assert(offsetof(XpUiTextState, family) == 168);
static_assert(offsetof(XpUiTextState, size) == 172);
static_assert(offsetof(XpUiTextState, tab) == 176);
static_assert(offsetof(XpUiTextState, started_on_tabs) == 177);
static_assert(offsetof(XpUiTextState, language) == 178);
static_assert(offsetof(XpUiTextState, reserved) == 179);
static_assert(sizeof(XpUiTextLayout) == 92 && alignof(XpUiTextLayout) == 4);
static_assert(offsetof(XpUiTextLayout, theme) == 0);
static_assert(offsetof(XpUiTextLayout, bezel) == 72);
static_assert(offsetof(XpUiTextLayout, preview_percent) == 80);
static_assert(offsetof(XpUiTextLayout, preview_padding) == 82);
static_assert(offsetof(XpUiTextLayout, side_padding) == 84);
static_assert(offsetof(XpUiTextLayout, tab_spacing) == 86);
static_assert(offsetof(XpUiTextLayout, underline) == 88);
static_assert(offsetof(XpUiTextLayout, full_slot) == 90);
static_assert(offsetof(XpUiTextLayout, reserved) == 91);
static_assert(sizeof(XpUiTextValues) == 11 && alignof(XpUiTextValues) == 1);
static_assert(offsetof(XpUiTextValues, fields) == 0);
static_assert(sizeof(XpUiTextEdit) == 8 && alignof(XpUiTextEdit) == 4);
static_assert(offsetof(XpUiTextEdit, value) == 0);
static_assert(offsetof(XpUiTextEdit, kind) == 4);
static_assert(offsetof(XpUiTextEdit, field) == 5);
static_assert(offsetof(XpUiTextEdit, count) == 6);
static_assert(offsetof(XpUiTextEdit, selected) == 7);
static_assert(sizeof(XpUiTextPreviewKey) == 32 && alignof(XpUiTextPreviewKey) == 4);
static_assert(offsetof(XpUiTextPreviewKey, font_id) == 0);
static_assert(offsetof(XpUiTextPreviewKey, point_size) == 4);
static_assert(offsetof(XpUiTextPreviewKey, margin) == 8);
static_assert(offsetof(XpUiTextPreviewKey, width) == 12);
static_assert(offsetof(XpUiTextPreviewKey, height) == 16);
static_assert(offsetof(XpUiTextPreviewKey, compression) == 20);
static_assert(offsetof(XpUiTextPreviewKey, alignment) == 24);
static_assert(offsetof(XpUiTextPreviewKey, paragraph_gap) == 25);
static_assert(offsetof(XpUiTextPreviewKey, indent) == 26);
static_assert(offsetof(XpUiTextPreviewKey, tracking) == 27);
static_assert(offsetof(XpUiTextPreviewKey, word_spacing) == 28);
static_assert(offsetof(XpUiTextPreviewKey, focus) == 29);
static_assert(offsetof(XpUiTextPreviewKey, hyphenation) == 30);
static_assert(offsetof(XpUiTextPreviewKey, reserved) == 31);
static_assert(sizeof(XpUiTextPreviewGeometry) == 28 && alignof(XpUiTextPreviewGeometry) == 4);
static_assert(offsetof(XpUiTextPreviewGeometry, caption) == 0);
static_assert(offsetof(XpUiTextPreviewGeometry, text) == 8);
static_assert(offsetof(XpUiTextPreviewGeometry, advance) == 16);
static_assert(offsetof(XpUiTextPreviewGeometry, gap) == 20);
static_assert(offsetof(XpUiTextPreviewGeometry, caption_visible) == 24);
static_assert(offsetof(XpUiTextPreviewGeometry, text_visible) == 25);
static_assert(offsetof(XpUiTextPreviewGeometry, reserved) == 26);
static_assert(static_cast<int>(XP_UI_TEXT_FAMILY) > static_cast<int>(XP_UI_SCROLL) &&
              XP_UI_TEXT_SIZE == XP_UI_TEXT_FAMILY + 1 && XP_UI_TEXT_SAVE == XP_UI_TEXT_SIZE + 1 &&
              XP_UI_TEXT_OPTIONS == XP_UI_TEXT_SAVE + 1);

constexpr uint8_t CrossPointSettings::* FIELDS[] = {
    &CrossPointSettings::lineSpacing,           &CrossPointSettings::wordSpacing,
    &CrossPointSettings::characterSpacing,      &CrossPointSettings::extraParagraphSpacing,
    &CrossPointSettings::paragraphIndentSpaces, &CrossPointSettings::paragraphAlignment,
    &CrossPointSettings::screenMargin,          &CrossPointSettings::focusReadingEnabled,
    &CrossPointSettings::hyphenationEnabled,    &CrossPointSettings::embeddedStyle,
    &CrossPointSettings::textAntiAliasing};
uint16_t rowCount(const XpUiTextState& state) {
  switch (static_cast<TextSettingsActivity::Tab>(state.tab)) {
    case TextSettingsActivity::Tab::Family:
      return state.font_count;
    case TextSettingsActivity::Tab::Size:
      return state.size_count;
    case TextSettingsActivity::Tab::Layout:
      return 7;
    case TextSettingsActivity::Tab::Style:
      return 4;
    case TextSettingsActivity::Tab::Count:
      return 0;
  }
  return 0;
}
XpUiTextValues sampleValues() {
  XpUiTextValues values{};
  for (size_t i = 0; i < std::size(FIELDS); ++i) values.fields[i] = SETTINGS.*FIELDS[i];
  return values;
}
static_assert(static_cast<uint16_t>(StrId::STR_FONT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SIZE) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_LAYOUT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_STYLE) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_LINE_SPACING) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_WORD_SPACING) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_CHARACTER_SPACING) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_EXTRA_SPACING) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_PARAGRAPH_INDENTATION) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_ALIGNMENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SCREEN_MARGIN) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_FOCUS_READING) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_HYPHENATION_NO_PREVIEW) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_EMBEDDED_STYLE_NO_PREVIEW) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_TEXT_AA_NO_PREVIEW) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_TIGHT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_NORMAL) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_WIDE) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_EXTRA_WIDE) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_50_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_75_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_100_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_125_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_150_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_175_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_200_PERCENT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_MINUS_2) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_MINUS_1) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_ZERO) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_PLUS_1) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SPACING_PLUS_2) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_JUSTIFY) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_ALIGN_LEFT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_CENTER) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_ALIGN_RIGHT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_BOOK_S_STYLE) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_STATE_OFF) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_SELECT) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(static_cast<uint16_t>(StrId::STR_TOGGLE) < static_cast<uint16_t>(StrId::_COUNT));
static_assert(std::size(FIELDS) == XP_UI_TEXT_FIELD_COUNT);
static_assert(FIELDS[XP_UI_TEXT_LINE] == &CrossPointSettings::lineSpacing);
static_assert(FIELDS[XP_UI_TEXT_WORD] == &CrossPointSettings::wordSpacing);
static_assert(FIELDS[XP_UI_TEXT_CHARACTER] == &CrossPointSettings::characterSpacing);
static_assert(FIELDS[XP_UI_TEXT_PARAGRAPH_GAP] == &CrossPointSettings::extraParagraphSpacing);
static_assert(FIELDS[XP_UI_TEXT_INDENT] == &CrossPointSettings::paragraphIndentSpaces);
static_assert(FIELDS[XP_UI_TEXT_ALIGNMENT] == &CrossPointSettings::paragraphAlignment);
static_assert(FIELDS[XP_UI_TEXT_MARGIN] == &CrossPointSettings::screenMargin);
static_assert(FIELDS[XP_UI_TEXT_FOCUS] == &CrossPointSettings::focusReadingEnabled);
static_assert(FIELDS[XP_UI_TEXT_HYPHENATION] == &CrossPointSettings::hyphenationEnabled);
static_assert(FIELDS[XP_UI_TEXT_EMBEDDED] == &CrossPointSettings::embeddedStyle);
static_assert(FIELDS[XP_UI_TEXT_ANTIALIASING] == &CrossPointSettings::textAntiAliasing);
static_assert(CrossPointSettings::WORD_SPACING_MIN == 50 && CrossPointSettings::WORD_SPACING_MAX == 200 &&
              CrossPointSettings::WORD_SPACING_STEP == 25);
static_assert(CrossPointSettings::SCREEN_MARGIN_MIN == 5 && CrossPointSettings::SCREEN_MARGIN_MAX == 40 &&
              CrossPointSettings::SCREEN_MARGIN_STEP == 5);
}  // namespace
TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& input,
                                           const SdCardFontRegistry* registry, Tab tab)
    : RustActivityAdapter("TextSettings", renderer, input), registry_(registry) {
  state.tab = static_cast<uint8_t>(tab);
}
void TextSettingsActivity::onEnter() {
  Activity::onEnter();
  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));
  fonts_.push_back({tr(STR_NOTO_SANS), true, static_cast<uint8_t>(CrossPointSettings::NOTOSANS)});
  int family = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (size_t i = 0; i < families.size(); ++i) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
      if (families[i].name == SETTINGS.sdFontFamilyName) family = CrossPointSettings::BUILTIN_FONT_COUNT + i;
    }
  }
  const int size = rebuildSizeList();
  xp_ui_text_init(&state, state.tab, family, size, fonts_.size(), sizes_.size(),
                  static_cast<uint8_t>(I18N.getLanguage()));
  if (!commands.reserve(32)) {
    finish();
    return;
  }
  requestUpdate();
}
void TextSettingsActivity::onExit() {
  publication.ready = false;
  commands.reset();
  Activity::onExit();
}
int TextSettingsActivity::rebuildSizeList() {
  const auto points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);
  const auto selected = snapToNearestPointSize(points, SETTINGS.fontPointSize);
  int current = 0;
  sizes_.clear();
  sizes_.reserve(points.size());
  for (const auto pt : points) {
    char label[12];
    snprintf(label, sizeof(label), "%u pt", pt);
    if (pt == selected) current = sizes_.size();
    sizes_.push_back({label, pt});
  }
  return current;
}
void TextSettingsActivity::loop() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  RustActivityAdapter::loop();
}
void TextSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;
  RustActivityAdapter::render(std::move(lock));
}
void TextSettingsActivity::applyFamily(int index) {
  RenderLock lock;
  const auto& font = fonts_[index];
  if (font.isBuiltin) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else {
    strncpy(SETTINGS.sdFontFamilyName, font.name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  }
  sdFontSystem.ensureLoaded(renderer);
  const auto size = rebuildSizeList();
  ModelLock model(*this);
  state.family = index;
  state.size = size;
  state.size_count = sizes_.size();
  state.tabs[1].selected = size + 1;
}
void TextSettingsActivity::applySize(int index) {
  RenderLock lock;
  SETTINGS.fontPointSize = sizes_[index].pointSize;
  sdFontSystem.ensureLoaded(renderer);
  ModelLock model(*this);
  state.size = index;
}
void TextSettingsActivity::activateRow(int row) {
  const auto current = sampleValues();
  XpUiTextEdit edit{};
  {
    ModelLock lock(*this);
    edit = xp_ui_text_edit(&state, row, &current);
  }
  switch (edit.kind) {
    case XP_UI_TEXT_FAMILY:
      applyFamily(edit.value);
      break;
    case XP_UI_TEXT_SIZE:
      applySize(edit.value);
      break;
    case XP_UI_TEXT_SAVE:
      SETTINGS.*FIELDS[edit.field] = edit.value;
      break;
    case XP_UI_TEXT_OPTIONS: {
      const uint8_t language = static_cast<uint8_t>(I18N.getLanguage());
      char scratch[8][16]{};
      const char* labels[8]{};
      for (uint8_t i = 0; i < edit.count; ++i)
        labels[i] = xp_ui_text_label(language, 4, edit.field, i, scratch[i], sizeof(scratch[i]));
      const char* title = xp_ui_text_label(language, 1, edit.field, 0, nullptr, 0);
      optionPopup_.show(title, labels, edit.count, edit.selected, [field = edit.field](int selected) {
        SETTINGS.*FIELDS[field] = xp_ui_text_choice(field, selected);
        SETTINGS.saveToFile();
      });
      requestUpdate();
      return;
    }
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}
XpUiEffect TextSettingsActivity::updateRust(const XpUiInput& source) {
  XpUiInput input = source;
  if (publication.ready && publishedHits.generation == static_cast<uint32_t>(state.tab) + 1 && input.touch_flags) {
    XpUiInteraction hit{};
    if (xp_ui_reader_hit(&publishedHits, input.touch_x, input.touch_y, &hit)) {
      input.action = hit.action;
      input.value = hit.value;
    }
  }
  const auto effect = xp_ui_text_update(&state, &input);
  touchActivated = input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) &&
                   (effect.kind == XP_UI_ACTIVATE || effect.invalidate || input.action == 2);
  return effect;
}
void TextSettingsActivity::applyRustEffect(const XpUiEffect& effect) {
  if (touchActivated) haptic_feedback::touchAction();
  switch (effect.kind) {
    case XP_UI_CANCEL:
      finish();
      break;
    case XP_UI_ACTIVATE:
      activateRow(effect.value);
      break;
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), static_cast<int>(effect.value), rowCount(state));
      break;
    default:
      break;
  }
}
const char* TextSettingsActivity::text(uint32_t handle) const {
  if (handle >= XP_UI_TEXT_TAB_BASE && handle < XP_UI_TEXT_TAB_BASE + 4)
    return xp_ui_text_label(rendering.language, 0, handle - XP_UI_TEXT_TAB_BASE, 0, nullptr, 0);
  if (handle == 0 || handle >= XP_UI_READER_HEADER) return nullptr;
  const auto index = (handle - 1) / 4;
  const bool value = (handle - 1) % 4 == 2;
  switch (static_cast<Tab>(rendering.tab)) {
    case Tab::Family:
      return index < fonts_.size() ? (value ? tr(STR_SELECTED) : fonts_[index].name.c_str()) : nullptr;
    case Tab::Size:
      return index < sizes_.size() ? (value ? tr(STR_SELECTED) : sizes_[index].name.c_str()) : nullptr;
    case Tab::Layout:
      return index < 7 ? (value ? rowText[index] : xp_ui_text_label(rendering.language, 1, index, 0, nullptr, 0))
                       : nullptr;
    case Tab::Style:
      return index < 4 ? xp_ui_text_label(rendering.language, 2, index, 0, nullptr, 0) : nullptr;
    case Tab::Count:
      return nullptr;
  }
  return nullptr;
}
XpUiReaderRows TextSettingsActivity::rows() {
  return {this,
          [](void*, uint16_t, uint16_t) -> uint8_t { return 1; },
          [](void* p, uint16_t index, XpUiReaderRow* row) -> uint8_t {
            auto& self = *static_cast<TextSettingsActivity*>(p);
            const auto& s = self.rendering;
            const uint32_t h = index * 4 + 1;
            *row = {};
            row->label = h;
            switch (static_cast<Tab>(s.tab)) {
              case Tab::Family:
                if (index == s.family) row->value = h + 2;
                break;
              case Tab::Size:
                if (index == s.size) row->value = h + 2;
                break;
              case Tab::Layout:
                if (index == XP_UI_TEXT_PARAGRAPH_GAP) {
                  row->toggle = 1;
                  row->checked = self.values.fields[index] != 0;
                } else
                  row->value = h + 2;
                break;
              case Tab::Style:
                row->toggle = 1;
                row->checked = self.values.fields[XP_UI_TEXT_FOCUS + index] != 0;
                break;
              case Tab::Count:
                return 0;
            }
            return self.text(h) != nullptr;
          },
          [](void* p, uint32_t h, int32_t font, uint8_t bold) -> int32_t {
            auto& self = *static_cast<TextSettingsActivity*>(p);
            const char* text = self.text(h);
            return text ? self.renderer.getTextWidth(font, text, static_cast<EpdFontFamily::Style>(bold)) : -1;
          },
          [](void* p, uint32_t h, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
            auto& self = *static_cast<TextSettingsActivity*>(p);
            const char* text = self.text(h);
            if (!text) return -1;
            auto target = makeUiTarget(self.renderer);
            target.setFont(0, font);
            fui::TextStyle style;
            style.font = 0;
            style.bold = bold;
            style.maxLines = lines;
            return fui::measureWrappedText(target, text, style, width).height;
          },
          [](void* p, int32_t font) -> int32_t {
            return static_cast<TextSettingsActivity*>(p)->renderer.getLineHeight(font);
          },
          rowCount(rendering),
          64,
          static_cast<uint32_t>(rendering.tab) + 1};
}
bool TextSettingsActivity::buildRustScene(XpUiScene& scene) {
  if (!commands.ready()) return false;
  {
    ModelLock lock(*this);
    rendering = state;
    renderingScroll = rendering.tabs[rendering.tab].pending_scroll;
  }
  values = sampleValues();
  for (uint8_t i = 0; i < 7; ++i) {
    char scratch[16];
    const char* value = xp_ui_text_label(rendering.language, 3, i, values.fields[i], scratch, sizeof(scratch));
    // Translated values remain borrowed; only numeric values use these fixed slots.
    if (value == scratch) {
      strcpy(rowValues[i], scratch);
      rowText[i] = rowValues[i];
    } else
      rowText[i] = value;
  }
  sampleReaderChromeTheme(renderer, layout.theme);
  auto target = makeUiTarget(renderer);
  const auto device = target.deviceContext();
  const auto safe = device.safeRect();
  layout.bezel = {safe.x, safe.y, safe.width, safe.height};
  const auto& m = UITheme::getInstance().getMetrics();
  layout.preview_percent = m.previewHeightPercent;
  layout.preview_padding = m.previewPadding;
  layout.side_padding = m.contentSidePadding;
  layout.tab_spacing = m.tabSpacing;
  layout.underline = m.headerUnderlineSize;
  layout.full_slot = m.tabPillFullSlot;
  const char* family = rendering.family >= 0 && rendering.family < static_cast<int>(fonts_.size())
                           ? fonts_[rendering.family].name.c_str()
                           : "";
  const char* size = rendering.size >= 0 && rendering.size < static_cast<int>(sizes_.size())
                         ? sizes_[rendering.size].name.c_str()
                         : "";
  snprintf(caption, sizeof(caption), "%s \"%s, %s\"", tr(STR_PREVIEW), family, size);
  const auto resources = rows();
  for (;;) {
    const auto buffer = commands.buffer();
    const auto result = xp_ui_text_view(&rendering, &layout, &resources, &buffer, &next, &scene, &nextHits);
    if (result == 1) return true;
    if (result != 2 || !commands.grow()) return false;
  }
}
void TextSettingsActivity::rustPublished() {
  xp_ui_text_publish(&state, &next, renderingScroll);
  publishedHits = nextHits;
}
XpUiPaint TextSettingsActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t h, uint32_t generation) -> uint8_t {
            auto& self = *static_cast<TextSettingsActivity*>(p);
            return generation == static_cast<uint32_t>(self.rendering.tab) + 1 &&
                   (h == XP_UI_READER_HEADER || h == XP_UI_READER_FOOTER || h == XP_UI_TEXT_PREVIEW || self.text(h));
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
            auto& self = *static_cast<TextSettingsActivity*>(p);
            switch (c->resource) {
              case XP_UI_READER_HEADER:
                GUI.drawHeader(self.renderer, UITheme::getInstance().getHeaderRect(self.renderer, true, false),
                               tr(STR_TEXT_SETTINGS));
                return;
              case XP_UI_READER_FOOTER: {
                const auto& s = self.rendering;
                const char* confirm = xp_ui_text_label(s.language, 5, s.tab, s.tabs[s.tab].selected, nullptr, 0);
                const auto labels = self.mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
                GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
                return;
              }
              case XP_UI_TEXT_PREVIEW: {
                const auto old = self.renderer.getClipRect();
                self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
                const auto& r = c->rect;
                textsettings::renderPreview(self.renderer, self.previewLayout_, self.layout.preview_padding,
                                            self.layout.theme.spacing, Rect{r.x, r.y, r.w, r.h}, self.caption);
                self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
                return;
              }
              default:
                self.renderer.drawUiText(*c, clip, self.text(c->resource));
            }
          }};
}
