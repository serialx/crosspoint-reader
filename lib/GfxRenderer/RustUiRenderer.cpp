#include <FreeInkUIGfxRenderer.h>

#include <algorithm>

#include "GfxRenderer.h"

void GfxRenderer::drawUiText(const XpUiCommand& command, XpUiRect clip, const char* text) const {
  const auto& renderer = *this;
  const auto previous = renderer.getClipRect();
  const int left = std::max(previous[0], static_cast<int>(clip.x));
  const int top = std::max(previous[1], static_cast<int>(clip.y));
  const int right = std::min(previous[0] + previous[2], clip.x + clip.w);
  const int bottom = std::min(previous[1] + previous[3], clip.y + clip.h);
  renderer.setClipRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
  if (command.max_lines == 0) {
    renderer.drawText(command.font, command.rect.x, command.rect.y, text, command.paint != 1,
                      static_cast<EpdFontFamily::Style>(command.style));
  } else {
    freeink::ui::GfxRendererTarget textTarget(renderer);
    textTarget.setFont(0, command.font);
    freeink::ui::TextStyle style;
    style.align = static_cast<freeink::ui::TextAlign>(command.align);
    style.color = command.paint == 1 ? freeink::ui::Color::White : freeink::ui::Color::Black;
    style.bold = (command.style & EpdFontFamily::BOLD) != 0;
    style.maxLines = command.max_lines;
    textTarget.text({command.rect.x, command.rect.y, command.rect.w, command.rect.h}, text, style);
  }
  renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
}

bool GfxRenderer::drawUiScene(const XpUiScene& scene, const XpUiPaint* resources) const {
  struct Context {
    const GfxRenderer* renderer;
    const XpUiPaint* resources;
  } context{this, resources};
  const XpUiPaint paint{
      &context, [](void* p, const XpGlyphTarget*) { static_cast<Context*>(p)->renderer->clearScreen(); },
      [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip, const char* text) {
        static_cast<Context*>(p)->renderer->drawUiText(*c, clip, text);
      },
      [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
        const auto* r = static_cast<Context*>(p)->resources;
        return r && r->resource && r->resource_valid && r->resource_valid(r->context, handle, generation);
      },
      [](void* p, const XpGlyphTarget* target, const XpUiCommand* c, XpUiRect clip) {
        const auto* r = static_cast<Context*>(p)->resources;
        if (r && r->resource) r->resource(r->context, target, c, clip);
      }};
  const auto target = glyphTarget();
  return xp_ui_render(&scene, &target, &paint) != 0;
}

bool GfxRenderer::drawUiScene(const XpUiScene& scene, const XpGlyphTarget& target, const XpUiPaint* resources) const {
  // Font and legacy resource services use the same borrowed band as geometry.
  if (target.width != panelWidth || target.height != panelHeight || target.stride != panelWidthBytes ||
      target.orientation != static_cast<uint8_t>(orientation) || target.plane != static_cast<uint8_t>(renderMode) ||
      !target.buffer || target.rows <= 0 || target.originY < 0 || target.originY > panelHeight - target.rows ||
      target.bufferLen < static_cast<uint32_t>(panelWidthBytes * target.rows))
    return false;
  const auto previousTarget = glyphTarget();
  if (target.absolute != previousTarget.absolute || target.scanning != previousTarget.scanning) return false;
  const bool previousStrip = _stripActive;
  const auto previousClip = getClipRect();
  const bool rebound = target.buffer != previousTarget.buffer || target.originY != previousTarget.originY ||
                       target.rows != previousTarget.rows;
  if (rebound) beginStripTarget(target.buffer, target.originY, target.rows);
  setClipRect(target.clip.left, target.clip.top, std::max<int32_t>(0, target.clip.right - target.clip.left),
              std::max<int32_t>(0, target.clip.bottom - target.clip.top));
  const bool result = drawUiScene(scene, resources);
  if (rebound) {
    if (previousStrip)
      beginStripTarget(previousTarget.buffer, previousTarget.originY, previousTarget.rows);
    else
      endStripTarget();
  }
  setClipRect(previousClip[0], previousClip[1], previousClip[2], previousClip[3]);
  return result;
}
