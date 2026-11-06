#include "KeyboardEntryActivity.h"

#include <BidiUtils.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "CrossPointSettings.h"
#include "KeyboardLayoutSet.h"
#include "activities/OwnedLegacyActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

KeyboardEntryActivity::KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                             std::string initialText, const size_t maxLength, InputType inputType)
    : Activity("KeyboardEntry", renderer, mappedInput),
      title(std::move(title)),
      initialText(std::move(initialText)),
      maxLength(maxLength),
      inputType(inputType) {}

namespace fui = freeink::ui;
XpUiKeyboardLayouts KeyboardEntryActivity::layouts() {
  return {
      nullptr,
      [](void*, uint8_t id, uint8_t shift, uint8_t symbols, uint8_t language, uint8_t row) -> XpUiKeyboardRow {
        if (id >= keyboard_layouts::COUNT) return {};
        const auto& layout = fui::builtinKeyboardLayout(keyboard_layouts::ALL[id].id, shift, symbols, true, language);
        if (row >= layout.rowCount) return {};
        const auto& value = layout.rows[row];
        return {value.count, value.insetUnits, static_cast<uint8_t>(value.independentKeyWidth), 0};
      },
      [](void*, uint8_t id, uint8_t shift, uint8_t symbols, uint8_t language, uint8_t row,
         uint8_t col) -> XpUiKeyboardKey {
        if (id >= keyboard_layouts::COUNT) return {};
        const auto& layout = fui::builtinKeyboardLayout(keyboard_layouts::ALL[id].id, shift, symbols, true, language);
        if (row >= layout.rowCount || col >= layout.rows[row].count) return {};
        const auto& key = layout.rows[row].keys[col];
        return {key.label, key.output, key.alt, key.value, static_cast<uint8_t>(key.kind), key.widthUnits};
      }};
}

XpUiKeyboardMetrics KeyboardEntryActivity::metrics() const {
  return {
      &renderer,
      [](void* context, int32_t font, const uint8_t* text, uint32_t length, uint8_t mode) -> int32_t {
        if (mode == 2) return xp_bidi_paragraph_level(text, length, 0, length);
        const auto provider = static_cast<GfxRenderer*>(context)->textProvider();
        return mode == 1 ? xp_native_advance(&provider, font, text, length, 0, 0, -1, 1)
                         : xp_native_width(&provider, font, text, length, 0, -1);
      },
      [](void* context, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(context)->getLineHeight(font); }};
}

XpUiKeyboardViewLayout KeyboardEntryActivity::viewLayout() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto content = UITheme::getContentArea(renderer);
  XpUiKeyboardViewLayout out{};
  out.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                static_cast<int16_t>(renderer.getScreenHeight())};
  out.content = {static_cast<int16_t>(content.x), static_cast<int16_t>(content.y), static_cast<int16_t>(content.width),
                 static_cast<int16_t>(content.height)};
  out.font = UI_12_FONT_ID;
  out.small_font = SMALL_FONT_ID;
  out.top_padding = m.topPadding;
  out.header_height = m.headerHeight;
  out.spacing = m.verticalSpacing;
  out.vertical_offset = m.keyboardVerticalOffset;
  out.key_spacing = m.keyboardKeySpacing;
  out.key_height = m.keyboardKeyHeight;
  out.hints_height = m.buttonHintsHeight;
  out.side_width = m.sideButtonHintsWidth;
  out.text_percent = m.keyboardTextFieldWidthPercent;
  out.field_normal = m.textFieldNormalThickness;
  out.field_cursor = m.textFieldCursorThickness;
  out.field_end = m.textFieldLineEndOffset;
  if (SETTINGS.uiTheme == CrossPointSettings::ROUNDEDRAFF) {
    out.field_normal = 2;
    out.field_cursor = 3;
    out.field_end = -1;
  }
  out.centered = m.keyboardCenteredText;
  out.touch = mappedInput.hasTouch();
  out.x3 = gpio.deviceIsX3();
  out.language = static_cast<uint8_t>(I18N.getLanguage());
  const StrId ids[] = {StrId::STR_OK_BUTTON,
                       StrId::STR_KEY_MODE_ABC,
                       StrId::STR_KEY_MODE_SYMBOLS,
                       StrId::STR_KB_TIPS,
                       StrId::STR_KB_HINT_TOGGLE_HIDE_PASSWORD,
                       StrId::STR_KB_HINT_TOGGLE_SHOW_PASSWORD,
                       StrId::STR_KB_HINT_RETURN_CURSOR,
                       StrId::STR_KB_HINT_MOVE_CURSOR,
                       StrId::STR_KB_HINT_HIDE_PASSWORD,
                       StrId::STR_KB_HINT_SHOW_PASSWORD,
                       StrId::STR_KB_HINT_EDIT_ENTRY,
                       StrId::STR_KB_HINT_RETURN_KEYBOARD,
                       StrId::STR_KB_HINT_EXIT_URL_MODE,
                       StrId::STR_KB_HINT_CLEAR_TEXT,
                       StrId::STR_KB_HINT_SECONDARY_CHAR,
                       StrId::STR_KB_HINT_LOWER_SECONDARY,
                       StrId::STR_KB_HINT_UPPER_SECONDARY,
                       StrId::STR_KB_HINT_URL_SNIPPETS};
  for (unsigned i = 0; i < std::size(ids); ++i) out.strings[i] = static_cast<uint16_t>(ids[i]);
  return out;
}

void KeyboardEntryActivity::paintKey(const XpUiCommand& command, const XpUiKeyboardPaintKey& value, uint8_t layout,
                                     bool symbols, bool modeAbc) {
  const fui::KeyboardKey key{value.label_present ? value.label : nullptr,
                             nullptr,
                             static_cast<fui::KeyKind>(value.kind),
                             fui::StateNormal,
                             value.value,
                             value.units,
                             true,
                             value.alternate_present ? value.alternate : nullptr};
  const fui::KeyboardRow row{&key, 1, 0};
  const fui::KeyboardLayout keys{&row, 1};
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  fui::InteractionBuffer<0> noHits;
  fui::Frame<0> frame(target, target.deviceContext(), {}, noHits);
  fui::KeyboardProps props;
  props.layout = &keys;
  props.selectedIndex = command.paint ? 0 : -1;
  props.okLabel = tr(STR_OK_BUTTON);
  props.modeLabel = modeAbc ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.labelText.font = keyboard_layouts::ALL[layout].id == fui::KeyboardLayoutId::ArabicAr && !symbols
                             ? fui::GfxRendererTarget::FONT_SMALL
                             : fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.background = fui::Paint::none();
  props.uniformKeyWidth = false;
  const auto r = command.rect;
  fui::keyboard(frame,
                fui::Rect{static_cast<int16_t>(r.x - 4), static_cast<int16_t>(r.y - 4), static_cast<int16_t>(r.w + 8),
                          static_cast<int16_t>(r.h + 8)},
                props);
}

void KeyboardEntryActivity::paintChrome(const XpUiCommand& command, XpUiRect clip, XpAppText text) {
  switch (command.resource) {
    case 1: {
      const auto r = command.rect;
      GUI.drawHeader(renderer, Rect{r.x, r.y, r.w, r.h}, reinterpret_cast<const char*>(text.data));
      break;
    }
    case 2: {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case 3:
      GUI.drawSideButtonHints(renderer, ">", "<");
      break;
  }
}

namespace {
KeyboardEntryActivity& keyboard(void* activity) {
  // pushActivity admits this pointer only for the reserved KeyboardEntryActivity name.
  return *static_cast<KeyboardEntryActivity*>(nativeActivity(activity));
}
}  // namespace
void xp_keyboard_launch(void* activity, XpAppKeyboardLaunch* out) { *out = keyboard(activity).launch(); }
void xp_keyboard_release_launch(void* activity) { keyboard(activity).releaseLaunch(); }
XpUiKeyboardLayouts xp_keyboard_layouts() { return KeyboardEntryActivity::layouts(); }
XpUiKeyboardMetrics xp_keyboard_metrics(void* activity) { return keyboard(activity).metrics(); }
void xp_keyboard_snapshot(void* activity, XpUiKeyboardViewLayout* layout, uint16_t* configured, uint8_t* language) {
  if (layout) *layout = keyboard(activity).viewLayout();
  if (configured) *configured = SETTINGS.keyboardLayouts;
  if (language) *language = static_cast<uint8_t>(I18N.getLanguage());
}
void xp_keyboard_text_lock(void* activity, uint8_t acquire) { keyboard(activity).textLock(acquire != 0); }
void xp_keyboard_draw_key(void* activity, const XpUiCommand* command, const XpUiKeyboardPaintKey* key, uint8_t layout,
                          uint8_t symbols, uint8_t modeAbc) {
  keyboard(activity).paintKey(*command, *key, layout, symbols != 0, modeAbc != 0);
}
void xp_keyboard_chrome(void* activity, const XpUiCommand* command, XpUiRect clip, XpAppText title) {
  keyboard(activity).paintChrome(*command, clip, title);
}
void xp_keyboard_deliver(const XpAppLegacy* legacy, void* parent, XpAppText text, uint8_t cancelled) {
  ActivityResult result;
  result.isCancelled = cancelled != 0;
  if (!cancelled) {
    result.data =
        KeyboardResult{text.len ? std::string(reinterpret_cast<const char*>(text.data), text.len) : std::string()};
  }
  legacy->deliver_result(parent, &result);
}
