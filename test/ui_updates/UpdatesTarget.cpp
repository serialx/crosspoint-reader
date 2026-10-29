#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_updates.h>

#include <cassert>
#include <cstdio>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui = freeink::ui;
struct Owner {
  GfxRenderer& renderer;
  ui::GfxRendererTarget& target;
  bool message;
  const char* text(uint32_t handle) const {
    switch (handle) {
      case 0x4000001a:
        return message ? "Clear reading cache" : "New update";
      case 0x4000001b:
        return "Remove the reading cache while keeping your books and settings. This long synthetic warning wraps "
               "across several lines on small displays.";
      case 0x4000001c:
        return "Cancel";
      case 0x4000001d:
        return message ? "Clear" : "Update";
      default:
        return "";
    }
  }
};
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont small(&ubuntu_10_regular), smallBold(&ubuntu_10_bold);
  static EpdFont body(&ubuntu_12_regular), bodyBold(&ubuntu_12_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &smallBold));
  renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&body, &bodyBold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int radius : {0, 6, 20})
      for (bool message : {false, true})
        for (int selected : {0, 1}) {
          renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
          renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
          ui::GfxRendererTarget target(renderer, true);
          target.setFont(0, UI_10_FONT_ID);
          target.setFont(1, UI_12_FONT_ID);
          Owner owner{renderer, target, message};
          auto device = target.deviceContext();
          ui::InteractionBuffer<4> hits;
          ui::Frame<4> frame(target, device, {}, hits);
          ui::DialogOption options[2];
          for (int n = 0; n < 2; ++n) {
            options[n].label = owner.text(0x4000001c + n);
            options[n].action = 1;
            options[n].value = n;
            options[n].state = selected == n ? ui::StateFocused : ui::StateNormal;
          }
          ui::OptionDialogProps p;
          p.title = owner.text(0x4000001a);
          p.message = message ? owner.text(0x4000001b) : nullptr;
          p.titleText.font = 1;
          p.titleText.bold = true;
          p.titleText.align = ui::TextAlign::Center;
          p.titleText.maxLines = 2;
          p.messageText.font = 1;
          p.messageText.align = ui::TextAlign::Center;
          p.messageText.maxLines = 6;
          p.buttonText.font = 1;
          p.padding = {16, 16, 16, 16};
          p.gap = 8;
          p.buttonHeight = renderer.getLineHeight(UI_12_FONT_ID) + 16;
          p.verticalOptions = true;
          p.options = options;
          p.optionCount = 2;
          p.styles = ui::defaultPopupStyles();
          p.styles.normal.border = ui::Paint::solid(ui::Color::Black);
          p.styles.normal.borderWidth = 2;
          p.styles.normal.radius = radius;
          if (radius) {
            p.buttonStyles = ui::defaultButtonStyles();
            p.buttonStyles.focused = p.buttonStyles.selected;
            ui::setStyleRadius(p.buttonStyles, radius);
          }
          int16_t width = device.width * 3 / 4;
          int16_t height = ui::optionDialogHeight(target, p, width);
          auto area = ui::centeredRect(device.screen(), {width, height});
          renderer.clearScreen();
          ui::optionDialog(frame, area, p);
          const auto expected = display.pixels;
          XpUiUpdateState state{};
          xp_ui_updates_init(&state, message ? XP_UI_UPDATE_CACHE : XP_UI_UPDATE_OTA, 0);
          state.phase = message ? 0 : 2;
          state.popup_active = 1;
          state.popup_selected = selected;
          XpUiUpdateLayout layout{};
          layout.list.list.screen = {0, 0, device.width, device.height};
          layout.list.list.area = layout.list.list.screen;
          layout.list.list.font = UI_12_FONT_ID;
          layout.list.help_font = UI_10_FONT_ID;
          layout.list.side_padding = 16;
          layout.list.list.radius = radius;
          layout.progress_height = 24;
          layout.popup_padding = 16;
          layout.popup_gap = 8;
          layout.popup_side_margin = 20;
          layout.selection_padding = 8;
          layout.popup_radius = radius;
          layout.popup_border = 2;
          XpUiUpdateResources resources{};
          resources.context = &owner;
          resources.rows.context = &owner;
          resources.text = [](void* p, uint32_t h) { return static_cast<Owner*>(p)->text(h); };
          resources.rows.width = [](void* p, const char* t, int32_t f, uint8_t b) -> int32_t {
            return static_cast<Owner*>(p)->renderer.getTextWidth(f, t, static_cast<EpdFontFamily::Style>(b));
          };
          resources.rows.line = [](void* p, int32_t f) -> int32_t {
            return static_cast<Owner*>(p)->renderer.getLineHeight(f);
          };
          resources.text_height = [](void* p, uint32_t h, int32_t f, int16_t w, uint8_t b, uint8_t lines) -> int16_t {
            auto& o = *static_cast<Owner*>(p);
            o.target.setFont(0, f);
            ui::TextStyle style;
            style.font = 0;
            style.bold = b;
            style.maxLines = lines;
            return ui::measureWrappedText(o.target, o.text(h), style, w).height;
          };
          XpUiCommand commands[128];
          XpUiBuffer buffer{commands, nullptr, 128, 0};
          XpUiScene scene{};
          XpUiUpdateFeedback feedback{};
          assert(xp_ui_updates_view(&state, &layout, &resources, &buffer, &feedback, &scene) == 1);
          XpUiPaint paint{&owner, nullptr, nullptr, [](void*, uint32_t, uint32_t g) -> uint8_t { return g == 1; },
                          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                            if (c->resource < 0x4000001a || c->resource > 0x4000001d) return;
                            auto& o = *static_cast<Owner*>(p);
                            o.renderer.drawUiText(*c, clip, o.text(c->resource));
                          }};
          assert(renderer.drawUiScene(scene, &paint));
          if (display.pixels != expected) {
            fprintf(stderr, "popup pixels differ: orientation=%d radius=%d message=%d selected=%d\n", orientation,
                    radius, message, selected);
            return 1;
          }
          assert(feedback.popup.x == area.x && feedback.popup.y == area.y && feedback.popup.w == area.width &&
                 feedback.popup.h == area.height);
          for (int n = 0; n < 2; ++n) {
            const auto& r = hits.data()[n].rect;
            const auto& s = feedback.options[n];
            assert(r.x == s.x && r.y == s.y && r.width == s.w && r.height == s.h);
          }
          ++cases;
        }
  printf("%u update popup cases match SDK pixels and touch rectangles\n", cases);
}
