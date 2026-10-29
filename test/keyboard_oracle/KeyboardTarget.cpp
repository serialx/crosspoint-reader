#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_keyboard.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
#define STR_OK_BUTTON "OK"
#define STR_KEY_MODE_ABC "abc"
#define STR_KEY_MODE_SYMBOLS "?123"
#define tr(x) x
namespace fui = freeink::ui;
class KeyboardEntryActivity {
 public:
  struct Bytes {
    std::unique_ptr<uint8_t[]> data;
    uint32_t capacity = 0;
    void grow(uint32_t n) {
      if (n > capacity) {
        data = std::make_unique<uint8_t[]>(n);
        capacity = n;
      }
    }
  };
  struct Arena {
    XpUiKeyboardState snapshot{};
    XpUiKeyboardTextSpan spans[64]{};
    uint16_t spanCapacity = 64;
    XpUiKeyboardPlacedKey rowKeys[16];
    Bytes raw, display, paintText;
  };
  GfxRenderer& renderer;
  XpUiKeyboardViewLayout layout{};
  std::unique_ptr<Arena> arena = std::make_unique<Arena>();
  explicit KeyboardEntryActivity(GfxRenderer& r) : renderer(r) {}
  static XpUiKeyboardLayouts layouts() {
    return {nullptr,
            [](void*, uint8_t id, uint8_t sh, uint8_t sy, uint8_t la, uint8_t r) -> XpUiKeyboardRow {
              const auto& l = fui::builtinKeyboardLayout(fui::KeyboardLayoutId(id), sh, sy, true, la);
              if (r >= l.rowCount) return {};
              const auto& x = l.rows[r];
              return {x.count, x.insetUnits, uint8_t(x.independentKeyWidth), 0};
            },
            [](void*, uint8_t id, uint8_t sh, uint8_t sy, uint8_t la, uint8_t r, uint8_t c) -> XpUiKeyboardKey {
              const auto& l = fui::builtinKeyboardLayout(fui::KeyboardLayoutId(id), sh, sy, true, la);
              if (r >= l.rowCount || c >= l.rows[r].count) return {};
              const auto& x = l.rows[r].keys[c];
              return {x.label, x.output, x.alt, x.value, uint8_t(x.kind), x.widthUnits};
            }};
  }
  void paintKey(const XpUiCommand&);
  void paintRow(const XpUiCommand&);
  void paintText(const XpUiCommand&, XpUiRect);
  bool validText(uint32_t) const;
};
#include "keyboard-paint.inc"
enum class InputType { Text, Password, Url };
struct FieldRect {
  int x, y, width, height;
};
class OriginalField {
 public:
  GfxRenderer& renderer;
  XpUiKeyboardViewLayout l;
  std::string text;
  size_t cursorPos = 0;
  InputType inputType = InputType::Text;
  bool passwordVisible = false, cursorMode = false, togglePos = false;
  static size_t utf8Prev(const std::string&, size_t);
  static size_t utf8Next(const std::string&, size_t);
  std::string displayTextForCurrentState() const;
  int measureRange(std::string&, int, int) const;
  bool rangeIsRtl(std::string&, int, int) const;
  int lineBreakEnd(std::string&, int, int) const;
  void render();
};
#include "keyboard-field.inc"
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont regular(&ubuntu_10_regular), bold(&ubuntu_10_bold);
  for (int id : {UI_12_FONT_ID, SMALL_FONT_ID}) renderer.insertFont(id, EpdFontFamily(&regular, &bold));
  FontDecompressor decompress;
  assert(decompress.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompress);
  renderer.setFontCacheManager(&cache);
  KeyboardEntryActivity owner(renderer);
  auto tables = owner.layouts();
  XpUiPaint paint{&owner, nullptr, nullptr,
                  [](void* p, uint32_t id, uint32_t generation) -> uint8_t {
                    return generation == 1 && (id < 0x200 || static_cast<KeyboardEntryActivity*>(p)->validText(id));
                  },
                  [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                    auto& owner = *static_cast<KeyboardEntryActivity*>(p);
                    if (c->resource >= 0x200)
                      owner.paintText(*c, clip);
                    else if (c->resource >= 0x100)
                      owner.paintRow(*c);
                  }};
  XpUiKeyboardMetrics metrics{
      &renderer,
      [](void* p, int32_t font, const uint8_t* s, uint32_t n, uint8_t mode) -> int32_t {
        if (mode == 2) return xp_bidi_paragraph_level(s, n, 0, n);
        auto provider = static_cast<GfxRenderer*>(p)->textProvider();
        return mode == 1 ? xp_native_advance(&provider, font, s, n, 0, 0, -1, 1)
                         : xp_native_width(&provider, font, s, n, 0, -1);
      },
      [](void* p, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(p)->getLineHeight(font); }};
  std::array<XpUiCommand, 96> commands{};
  std::array<XpUiInteraction, 56> hits{};
  std::array<uint8_t, 4096> text{};
  XpUiKeyboardBuffer buffer{commands.data(), hits.data(), text.data(), 96, 56, 4096, owner.arena->spans, 64};
  for (int orientation = 0; orientation < 4; orientation++)
    for (uint8_t layout : {0, 4, 8, 9})
      for (uint8_t mode : {0, 1, 2}) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
        renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
        auto& state = owner.arena->snapshot;
        xp_ui_keyboard_init(&state, 0, 0, mode, 1023, 0);
        state.layout = layout;
        state.active_key = INT16_MIN;
        XpUiKeyboardViewLayout l{};
        l.screen = {0, 0, int16_t(renderer.getScreenWidth()), int16_t(renderer.getScreenHeight())};
        l.content = l.screen;
        l.font = UI_12_FONT_ID;
        l.small_font = SMALL_FONT_ID;
        l.spacing = 8;
        l.header_height = 40;
        l.key_height = 56;
        l.hints_height = 40;
        l.key_spacing = 6;
        l.text_percent = 80;
        l.field_normal = 2;
        l.field_cursor = 3;
        l.field_end = -1;
        owner.layout = l;
        uint8_t raw[] = {0};
        XpUiScene scene{};
        assert(xp_ui_keyboard_view(&state, raw, raw, &tables, &l, &metrics, &buffer, &scene));
        assert(renderer.drawUiScene(scene, &paint));
        const auto expected = display.pixels;
        display.pixels.fill(0x69);
        std::array<uint8_t, 48000> assembled{};
        std::array<uint8_t, 1602> band{};
        for (int row = 0; row < 480; row += 16) {
          band.fill(0xa5);
          XpGlyphTarget target{band.data() + 1,
                               1600,
                               800,
                               480,
                               100,
                               row,
                               16,
                               {0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                               uint8_t(orientation),
                               0,
                               0,
                               1,
                               0};
          assert(renderer.drawUiScene(scene, target, &paint));
          assert(band.front() == 0xa5 && band.back() == 0xa5);
          assert(std::all_of(display.pixels.begin(), display.pixels.end(), [](uint8_t b) { return b == 0x69; }));
          std::copy_n(band.data() + 1, 1600, assembled.data() + row * 100);
        }
        assert(assembled == expected);
      }
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int centered = 0; centered < 2; ++centered)
      for (int mode = 0; mode < 3; ++mode)
        for (int size : {0, 64, 1024, 3000, 5000, -1, -2, -3})
          for (int cursor : {0, 1}) {
            renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
            renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
            XpUiKeyboardViewLayout l{};
            l.screen = {0, 0, int16_t(renderer.getScreenWidth()), int16_t(renderer.getScreenHeight())};
            l.content = l.screen;
            l.font = UI_12_FONT_ID;
            l.small_font = SMALL_FONT_ID;
            l.spacing = 8;
            l.header_height = 40;
            l.key_height = 56;
            l.hints_height = 40;
            l.key_spacing = 6;
            l.text_percent = 80;
            l.field_normal = 2;
            l.field_cursor = 3;
            l.field_end = -1;
            l.centered = centered;
            owner.layout = l;
            OriginalField old{renderer, l};
            const int fieldFull =
                ((l.content.w * l.text_percent / 100) /
                 renderer.getTextAdvanceX(UI_12_FONT_ID, "w", EpdFontFamily::REGULAR)) *
                    ((l.screen.h - l.header_height - 5 * l.spacing) / renderer.getLineHeight(UI_12_FONT_ID)) +
                1;
            old.text = std::string(size < 0 ? fieldFull : size, 'w');
            if (size == 5000) {
              old.text.clear();
              for (int i = 0; i < 2500; ++i) old.text += "\xcc\x81";
            }
            if (size == -2) old.text = "שלום אבג 12 abc";
            if (size == -3) old.text = "مرحبا بالعالم 12 abc";
            old.cursorMode = cursor;
            old.cursorPos = cursor ? old.text.size() / 2 : old.text.size();
            while (old.cursorPos > 0 && old.cursorPos < old.text.size() &&
                   (uint8_t(old.text[old.cursorPos]) & 0xc0) == 0x80)
              --old.cursorPos;
            old.inputType = static_cast<InputType>(mode);
            old.render();
            const auto expected = display.pixels;
            auto& arena = *owner.arena;
            xp_ui_keyboard_init(&arena.snapshot, old.text.size(), 0, mode, 1, 0);
            arena.snapshot.cursor = old.cursorPos;
            arena.snapshot.cursor_mode = cursor;
            arena.raw.grow(old.text.size() + 1);
            arena.display.grow(old.text.size() + 1);
            memcpy(arena.raw.data.get(), old.text.c_str(), old.text.size() + 1);
            assert(xp_ui_keyboard_mask(&arena.snapshot, arena.raw.data.get(), arena.display.data.get(),
                                       arena.display.capacity));
            for (auto& span : arena.spans) span.raw = 255;
            XpUiScene scene{};
            assert(xp_ui_keyboard_view(&arena.snapshot, arena.raw.data.get(), arena.display.data.get(), &tables, &l,
                                       &metrics, &buffer, &scene));
            uint32_t longest = 0;
            for (const auto& span : arena.spans)
              if (span.raw <= 1) longest = std::max(longest, span.length);
            arena.paintText.grow(longest + 1);
            std::vector<XpUiCommand> field;
            for (unsigned i = 0; i < scene.command_count; ++i) {
              const auto c = scene.commands[i];
              if (c.resource >= 0x100 && c.resource < 0x200) break;
              if (c.kind == XP_UI_TEXT && c.font == SMALL_FONT_ID) continue;
              if (c.kind == XP_UI_BITMAP) continue;
              field.push_back(c);
            }
            scene.commands = field.data();
            scene.command_count = field.size();
            assert(renderer.drawUiScene(scene, &paint));
            if (display.pixels != expected) {
              printf("field mismatch orientation=%d centered=%d mode=%d size=%d cursor=%d longest=%u\n", orientation,
                     centered, mode, size, cursor, longest);
              fflush(stdout);
              return 1;
            }
            if (size == 3000 || size == 5000 || size < 0) {
              display.pixels.fill(0x69);
              std::array<uint8_t, 48000> assembled{};
              std::array<uint8_t, 1602> band{};
              for (int row = 0; row < 480; row += 16) {
                band.fill(0xa5);
                XpGlyphTarget target{band.data() + 1,
                                     1600,
                                     800,
                                     480,
                                     100,
                                     row,
                                     16,
                                     {0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                                     uint8_t(orientation),
                                     0,
                                     0,
                                     1,
                                     0};
                assert(renderer.drawUiScene(scene, target, &paint));
                assert(band.front() == 0xa5 && band.back() == 0xa5);
                assert(std::all_of(display.pixels.begin(), display.pixels.end(), [](uint8_t b) { return b == 0x69; }));
                std::copy_n(band.data() + 1, 1600, assembled.data() + row * 100);
              }
              assert(assembled == expected);
            }
            ++cases;
          }
  printf("%u original C++ versus Rust field pixel cases (including 5KB combining marks)\n", cases);
  puts("48 keyboard scenes: full frame equals 16-row bands, guards and unrelated framebuffer unchanged");
}
