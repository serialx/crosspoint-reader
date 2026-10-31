#include "KeyboardEntryActivity.h"

#include <BidiUtils.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "KeyboardLayoutSet.h"
#include "components/UITheme.h"
#include "fontIds.h"

KeyboardEntryActivity::KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                             std::string initialText, const size_t maxLength, InputType inputType)
    : RustActivityAdapter("KeyboardEntry", renderer, mappedInput),
      title(std::move(title)),
      initialText(std::move(initialText)),
      maxLength(maxLength),
      inputType(inputType) {}

namespace fui = freeink::ui;
static_assert(sizeof(XpUiKeyboardState) == 64 && alignof(XpUiKeyboardState) == 4);
static_assert(offsetof(XpUiKeyboardState, row) == 32 && offsetof(XpUiKeyboardState, layout) == 40);
static_assert(offsetof(XpUiKeyboardState, active_index) == 62);
static_assert(sizeof(XpUiKeyboardRow) == 4 && alignof(XpUiKeyboardRow) == 1);
static_assert(offsetof(XpUiKeyboardRow, independent) == 2);
static_assert(sizeof(XpUiKeyboardKey) == (sizeof(void*) == 4 ? 16 : 32));
static_assert(alignof(XpUiKeyboardKey) == alignof(void*) && offsetof(XpUiKeyboardKey, value) == sizeof(void*) * 3);
static_assert(sizeof(XpUiKeyboardLayouts) == sizeof(void*) * 3 && alignof(XpUiKeyboardLayouts) == alignof(void*));
static_assert(offsetof(XpUiKeyboardLayouts, key) == sizeof(void*) * 2);
static_assert(sizeof(XpUiKeyboardEffect) == 16 && alignof(XpUiKeyboardEffect) == 4);
static_assert(offsetof(XpUiKeyboardEffect, required_capacity) == 8 && offsetof(XpUiKeyboardEffect, haptics) == 12);
static_assert(sizeof(XpUiKeyboardViewLayout) == 88 && alignof(XpUiKeyboardViewLayout) == 4);
static_assert(offsetof(XpUiKeyboardViewLayout, strings) == 52);
static_assert(sizeof(XpUiKeyboardMetrics) == sizeof(void*) * 3 && alignof(XpUiKeyboardMetrics) == alignof(void*));
static_assert(offsetof(XpUiKeyboardMetrics, line_height) == sizeof(void*) * 2);
static_assert(sizeof(XpUiKeyboardBuffer) == (sizeof(void*) == 4 ? 28 : 48));
static_assert(alignof(XpUiKeyboardBuffer) == alignof(void*) &&
              offsetof(XpUiKeyboardBuffer, command_capacity) == sizeof(void*) * 3);
static_assert(sizeof(XpUiKeyboardPaintKey) == 30 && alignof(XpUiKeyboardPaintKey) == 2);
static_assert(offsetof(XpUiKeyboardPaintKey, value) == 24 && offsetof(XpUiKeyboardPaintKey, label_present) == 28);

static_assert(sizeof(XpUiKeyboardTextSpan) == 12 && alignof(XpUiKeyboardTextSpan) == 4);
static_assert(offsetof(XpUiKeyboardTextSpan, raw) == 8);
static_assert(offsetof(XpUiKeyboardBuffer, spans) == (sizeof(void*) == 4 ? 20 : 32));

static_assert(sizeof(XpUiKeyboardPlacedKey) == 20 && alignof(XpUiKeyboardPlacedKey) == 2);
static_assert(offsetof(XpUiKeyboardPlacedKey, value) == 16 && offsetof(XpUiKeyboardPlacedKey, selected) == 18);

bool KeyboardEntryActivity::Bytes::grow(const uint32_t required, const uint32_t used) {
  if (required <= capacity) return true;
  const uint32_t next = std::max(required, capacity <= UINT32_MAX / 2 ? capacity * 2 : required);
  auto replacement = makeUniqueNoThrow<uint8_t[]>(next);
  if (!replacement) return false;
  if (data && used) memcpy(replacement.get(), data.get(), used);
  data = std::move(replacement);
  capacity = next;
  return true;
}

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

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  arena = makeUniqueNoThrow<Arena>();
  if (!arena || initialText.size() >= UINT32_MAX || maxLength > UINT32_MAX || !text.grow(initialText.size() + 1, 0)) {
    LOG_ERR("RUI", "Cannot allocate keyboard scene");
    applyRustEffect(XpUiEffect{0, XP_UI_CANCEL, 0, {0, 0}});
    return;
  }
  memcpy(text.data.get(), initialText.c_str(), initialText.size() + 1);
  xp_ui_keyboard_init(&state, initialText.size(), maxLength, static_cast<uint8_t>(inputType),
                      keyboard_layouts::enabled(), static_cast<uint8_t>(I18N.getLanguage()));
  inputInterest = xp_ui_keyboard_interest(&state);
  std::string().swap(initialText);
  layout = viewLayout();
  requestUpdate();
}

void KeyboardEntryActivity::onExit() {
  publication.ready = false;
  arena.reset();
  text = {};
  Activity::onExit();
}

void KeyboardEntryActivity::loop() {
  if (!arena || !text.data) return;
  reconcileInput();
  if (consumeRejectedInput()) return;
  tappedCursor = UINT32_MAX;
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const XpUiKeyboardState snapshot = state;
    const auto tables = layouts();
    bool keyTap = false;
    {
      ModelLock lock(*this);
      XpUiInteraction hit{};
      keyTap = publication.ready && xp_ui_keyboard_hit(&publishedState, &tables, &publishedLayout, x, y, &hit);
    }
    if (keyTap) {
      RustActivityAdapter::loop();
      return;
    }
    Bytes touchDisplay;
    const bool masked = snapshot.input_type == 1 && !snapshot.password_visible;
    if (!masked || touchDisplay.grow(snapshot.length + 1, 0)) {
      if (masked) xp_ui_keyboard_mask(&snapshot, text.data.get(), touchDisplay.data.get(), snapshot.length + 1);
      const auto fonts = metrics();
      xp_ui_keyboard_cursor_at(&snapshot, masked ? touchDisplay.data.get() : text.data.get(), &tables, &layout, &fonts,
                               x, y, &tappedCursor);
    } else {
      LOG_ERR("RUI", "Cannot allocate keyboard touch text: %u bytes", snapshot.length + 1);
    }
  }
  RustActivityAdapter::loop();
}

void KeyboardEntryActivity::rustPublished() {
  if (!publicationPending) previousPublished = publishedState;
  publishedState = arena->snapshot;
  publishedLayout = layout;
  publicationPending = true;
}

void KeyboardEntryActivity::reconcileInput() {
  TextLock textLock(*this);
  const auto tables = layouts();
  ModelLock lock(*this);
  if (publicationPending) {
    xp_ui_keyboard_publish(&state, &previousPublished, &publishedState, &tables);
    publicationPending = false;
  }
  if (resetInput) {
    pending = {};
    lastInput = {};
    tappedCursor = UINT32_MAX;
    state.confirm_held = state.confirm_long = state.up_held = state.up_long = 0;
    state.down_held = state.down_long = state.right_held = state.right_long = 0;
    state.touch_held = state.touch_long = 0;
    state.active_key = INT16_MIN;
    state.active_index = -1;
    state.last_repeat = state.touch_start = 0;
    resetInput = false;
  }
  inputInterest = xp_ui_keyboard_interest(&state);
}

XpUiEffect KeyboardEntryActivity::updateRust(const XpUiInput& input) {
  lastInput = input;
  const auto tables = layouts();
  if (publication.ready && input.touch_flags) {
    XpUiInteraction hit{};
    if (xp_ui_keyboard_hit(&publishedState, &tables, &publishedLayout, input.touch_x, input.touch_y, &hit)) {
      lastInput.action = hit.action;
      lastInput.value = hit.value;
    }
  }
  return {};
}

void KeyboardEntryActivity::applyRustEffect(const XpUiEffect& navigation) {
  if (navigation.kind != XP_UI_CANCEL && rustSceneRejected()) {
    reconcileInput();
    return;
  }
  XpUiEffect effect{};
  {
    TextLock textLock(*this);
    if (navigation.kind != XP_UI_CANCEL && rustSceneRejected()) {
      requestUpdate();
      return;
    }
    if (navigation.kind == XP_UI_CANCEL) {
      pending = {};
      effect = navigation;
    } else {
      beforeInput = state;
      const auto tables = layouts();
      pending = xp_ui_keyboard_update(&state, text.data.get(), text.capacity, &tables, &lastInput, tappedCursor);
      if (pending.grow) {
        if (text.grow(pending.required_capacity, state.length + 1)) {
          state = beforeInput;
          pending = xp_ui_keyboard_update(&state, text.data.get(), text.capacity, &tables, &lastInput, tappedCursor);
        } else {
          LOG_ERR("RUI", "Cannot grow keyboard text: %u bytes", pending.required_capacity);
        }
      }
      effect = pending.navigation;
    }
    ModelLock lock(*this);
    inputInterest = xp_ui_keyboard_interest(&state);
  }
  if (effect.invalidate) requestUpdate();
  if (pending.haptics) haptic_feedback::touchAction(pending.haptics == 2);
  pending = {};
  switch (static_cast<XpUiEffectKind>(effect.kind)) {
    case XP_UI_ACTIVATE:
      setResult(KeyboardResult{std::string(reinterpret_cast<const char*>(text.data.get()), state.length)});
      finish();
      break;
    case XP_UI_CANCEL: {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_EFFECT_NONE:
    case XP_UI_SCROLL:
      break;
  }
}

bool KeyboardEntryActivity::buildRustScene(XpUiScene& scene) {
  if (!arena || !text.data) return false;
  const auto tables = layouts();
  for (;;) {
    uint32_t required;
    {
      TextLock lock(*this);
      required = state.length + 1;
    }
    if (!arena->raw.grow(required, 0)) return false;
    TextLock textLock(*this);
    if (state.length >= arena->raw.capacity) continue;
    arena->snapshot = state;
    {
      ModelLock lock(*this);
      if (publicationPending) xp_ui_keyboard_publish(&arena->snapshot, &previousPublished, &publishedState, &tables);
      xp_ui_keyboard_publish(&arena->snapshot, &publishedState, &state, &tables);
    }
    memcpy(arena->raw.data.get(), text.data.get(), state.length + 1);
    break;
  }
  const bool masked = arena->snapshot.input_type == 1 && !arena->snapshot.password_visible;
  if (masked) {
    if (!arena->display.grow(arena->snapshot.length + 1, 0)) return false;
    xp_ui_keyboard_mask(&arena->snapshot, arena->raw.data.get(), arena->display.data.get(), arena->display.capacity);
  } else {
    arena->display = {};
  }
  if (!arena->commands) {
    arena->commands = makeUniqueNoThrow<XpUiCommand[]>(24);
    if (!arena->commands) return false;
    arena->commandCapacity = 24;
  }
  if (!arena->spans) {
    arena->spans = makeUniqueNoThrow<XpUiKeyboardTextSpan[]>(8);
    if (!arena->spans) return false;
    arena->spanCapacity = 8;
  }
  if (!arena->sceneText.grow(512, 0)) return false;
  const auto fonts = metrics();
  XpUiScene next{};
  for (;;) {
    for (unsigned i = 0; i < arena->spanCapacity; ++i) arena->spans[i].raw = 255;
    const XpUiKeyboardBuffer buffer{arena->commands.get(),
                                    nullptr,
                                    arena->sceneText.data.get(),
                                    arena->commandCapacity,
                                    0,
                                    static_cast<uint16_t>(arena->sceneText.capacity),
                                    arena->spans.get(),
                                    arena->spanCapacity};
    if (xp_ui_keyboard_view(&arena->snapshot, arena->raw.data.get(),
                            masked ? arena->display.data.get() : arena->raw.data.get(), &tables, &layout, &fonts,
                            &buffer, &next))
      break;
    if (arena->commandCapacity == 256 && arena->spanCapacity == 64 && arena->sceneText.capacity == 4096) return false;
    const uint16_t capacity = std::min<unsigned>(256, arena->commandCapacity * 2);
    if (capacity != arena->commandCapacity) {
      auto commands = makeUniqueNoThrow<XpUiCommand[]>(capacity);
      if (!commands) return false;
      arena->commands = std::move(commands);
      arena->commandCapacity = capacity;
    }
    if (arena->spanCapacity < 64) {
      const uint16_t count = std::min<unsigned>(64, arena->spanCapacity * 2);
      auto spans = makeUniqueNoThrow<XpUiKeyboardTextSpan[]>(count);
      if (!spans) return false;
      arena->spans = std::move(spans);
      arena->spanCapacity = count;
    } else if (capacity == arena->commandCapacity &&
               !arena->sceneText.grow(std::min<uint32_t>(4096, arena->sceneText.capacity * 2), 0))
      return false;
  }
  uint32_t longest = 0;
  for (unsigned i = 0; i < arena->spanCapacity; ++i) {
    const auto& span = arena->spans[i];
    if (span.raw <= 1) longest = std::max(longest, span.length);
  }
  if (!arena->paintText.grow(longest + 1, 0)) return false;
  scene = next;
  return true;
}

bool KeyboardEntryActivity::validText(const uint32_t resource) const {
  if (!arena || resource < 0x200 || resource - 0x200 >= arena->spanCapacity) return false;
  const auto& span = arena->spans[resource - 0x200];
  return span.raw <= 1 && span.start <= arena->snapshot.length && span.length <= arena->snapshot.length - span.start &&
         span.length < arena->paintText.capacity;
}

void KeyboardEntryActivity::paintText(const XpUiCommand& command, const XpUiRect clip) {
  if (command.generation != 1 || !validText(command.resource)) return;
  const auto& span = arena->spans[command.resource - 0x200];
  const bool masked = arena->snapshot.input_type == 1 && !arena->snapshot.password_visible;
  const auto* bytes = !span.raw && masked ? arena->display.data.get() : arena->raw.data.get();
  memcpy(arena->paintText.data.get(), bytes + span.start, span.length);
  arena->paintText.data[span.length] = 0;
  renderer.drawUiText(command, clip, reinterpret_cast<const char*>(arena->paintText.data.get()));
}

void KeyboardEntryActivity::paintRow(const XpUiCommand& command) {
  if (command.generation != 1 || command.resource < 0x100 || command.resource >= 0x105) return;
  const auto tables = layouts();
  const uint8_t row = command.resource - 0x100;
  const uint8_t count = xp_ui_keyboard_row(&arena->snapshot, &tables, &layout, row, arena->rowKeys, 16);
  if (!count) return;
  for (uint8_t col = 0; col < count; ++col) {
    XpUiCommand key{};
    key.resource = 0x100 + row * 16 + col;
    key.rect = arena->rowKeys[col].visual;
    key.paint = arena->rowKeys[col].selected;
    paintKey(key);
  }
}

void KeyboardEntryActivity::paintKey(const XpUiCommand& command) {
  XpUiKeyboardPaintKey value{};
  const auto tables = layouts();
  const uint32_t index = command.resource - 0x100;
  if (!xp_ui_keyboard_paint_key(&arena->snapshot, &tables, index / 16, index % 16, &value)) return;
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
  props.modeLabel = (arena->snapshot.symbols || (arena->snapshot.input_type == 2 && arena->snapshot.url_panel))
                        ? tr(STR_KEY_MODE_ABC)
                        : tr(STR_KEY_MODE_SYMBOLS);
  props.labelText.font =
      keyboard_layouts::ALL[arena->snapshot.layout].id == fui::KeyboardLayoutId::ArabicAr && !arena->snapshot.symbols
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

XpUiPaint KeyboardEntryActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t resource, uint32_t generation) -> uint8_t {
            auto& self = *static_cast<KeyboardEntryActivity*>(p);
            if (generation != 1 || !self.arena) return 0;
            if (resource >= 0x200) return self.validText(resource);
            if (resource >= 1 && resource <= 3) return 1;
            if (resource < 0x100 || resource >= 0x105) return 0;
            const auto tables = layouts();
            return xp_ui_keyboard_row(&self.arena->snapshot, &tables, &self.layout, resource - 0x100,
                                      self.arena->rowKeys, 16) != 0;
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<KeyboardEntryActivity*>(p);
            if (command->resource >= 0x200) {
              self.paintText(*command, clip);
              return;
            }
            switch (command->resource) {
              case 1: {
                const auto r = command->rect;
                GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, self.title.c_str());
                break;
              }
              case 2: {
                const auto labels =
                    self.mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
                GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
                break;
              }
              case 3:
                GUI.drawSideButtonHints(self.renderer, ">", "<");
                break;
              default:
                self.paintRow(*command);
                break;
            }
          }};
}
