#pragma once
#include "clipping_owner_fixture.h"
#include "owner_tables.h"

namespace retirement_planes {
struct Context {
  GfxRenderer& renderer;
  int x, y, width, height;
  bool section = true;
  uint32_t signature = 0;
};
inline Context* active;
inline auto legacySection = unexpectedLegacySectionServices();
inline XpUiReaderRenderServices renderServices{};
inline void fill(GfxRenderer& renderer, int x, int y, int width, int height) {
  using namespace retirement_fixture;
  Context context{renderer, x, y, width, height};
  assert(!active);
  active = &context;
  auto leaves = leavesDefaults();
  auto runtime = modelRuntime();
  auto resources = resourcesDefaults();
  unsigned publicationStep = 0;
  struct Publication {
    Context* context;
    unsigned* step;
  } publication{&context, &publicationStep};
  legacySection.section_page_count = [](void* p) -> uint16_t {
    auto& state = *static_cast<Publication*>(p);
    assert(state.context == active && (*state.step)++ == 0);
    return 1;
  };
  legacySection.section_estimated_pages = [](void* p) -> int32_t {
    auto& state = *static_cast<Publication*>(p);
    assert(state.context == active && (*state.step)++ == 1);
    return 1;
  };
  legacySection.section_building = [](void* p) -> uint8_t {
    auto& state = *static_cast<Publication*>(p);
    assert(state.context == active && (*state.step)++ == 2);
    return 0;
  };
  legacySection.section_complete = [](void* p) -> uint8_t {
    auto& state = *static_cast<Publication*>(p);
    assert(state.context == active && (*state.step)++ == 3);
    return 1;
  };
  legacySection.section_partial = [](void* p) -> uint8_t {
    auto& state = *static_cast<Publication*>(p);
    assert(state.context == active && (*state.step)++ == 4);
    return 0;
  };
  legacySection.section_close = [](void* p) {
    auto& state = *static_cast<Publication*>(p);
    assert(state.context == active && active->section);
    active->section = false;
  };
  legacySection.page.page_info = [](void* p, XpEpubPageInfo* out) {
    assert(p == active && out);
    *out = {};
    out->elements = 1;
  };
  legacySection.page.page_element = [](void* p, uint16_t i, XpEpubElement* out) {
    assert(p == active && i == 0 && out);
    *out = {};
    out->tag = 1;
    out->valid = 1;
    out->words = 1;
  };
  legacySection.page.page_word = [](void* p, uint16_t i, uint16_t j, XpEpubWord* out) {
    assert(p == active && i == 0 && j == 0 && out);
    static const uint8_t word[] = "x";
    *out = {};
    out->text = {word, 1};
    out->source_end = 1;
  };
  legacySection.page.page_word_font = [](void* p, uint16_t i, uint16_t j, int32_t font) -> int32_t {
    assert(p == active && i == 0 && j == 0);
    return font;
  };
  legacySection.page.page_line_font = [](void* p, uint16_t i, int32_t font) -> int32_t {
    assert(p == active && i == 0);
    return font;
  };
  legacySection.page.page_word_y_offset = [](void* p, uint16_t i, uint16_t j, int32_t) -> int32_t {
    assert(p == active && i == 0 && j == 0);
    return 0;
  };
  legacySection.page.page_line_ruby_shift = [](void* p, uint16_t i, int32_t) -> int32_t {
    assert(p == active && i == 0);
    return 0;
  };
  leaves.font_text_advance = [](XpAppText text, int32_t, uint8_t style, int8_t tracking) -> int32_t {
    assert(text.len == 1 && text.data[0] == 'x' && !style && !tracking);
    return active->width;
  };
  resources.line_height = [](int32_t) -> int32_t { return active->height; };
  leaves.clippings_count = []() -> uint32_t { return 1; };
  leaves.clippings_row = [](uint32_t i, XpEpubClip* out) {
    assert(i == 0 && out);
    *out = {0, 0, 0, 1, 0, 0, active->signature, 0, 1};
  };
  renderServices = {};
  renderServices.context = &context;
  renderServices.read = [](void* p, uint8_t query) -> uint32_t {
    assert(p == active);
    if (query == XP_UI_READER_RENDER_IS_A4) return BoardConfig::isEegoA4();
    assert(query == XP_UI_READER_RENDER_IS_BW);
    return active->renderer.getRenderMode() == GfxRenderer::BW;
  };
  renderServices.execute = &Unexpected<decltype(renderServices.execute)>::call;
  leaves.render_services = [](void* p) -> const XpUiReaderRenderServices* {
    assert(p == active);
    return &renderServices;
  };
  leaves.paint_pixel = [](int32_t px, int32_t py) { active->renderer.drawPixel(px, py, false); };
  leaves.paint_dither = [](int32_t px, int32_t py, int32_t w, int32_t h) {
    active->renderer.fillRectDither(px, py, w, h, Color::LightGray);
  };
  runtime.now_ms = []() -> uint32_t { return 0; };
  leaves.log_enabled = [](uint8_t severity) -> uint8_t {
    assert(severity == 0);
    return 0;
  };
  leaves.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    assert(width == 480 && height == 800 && out);
    *out = {};
    out->width = width;
    out->height = height;
    out->compression = 1.0f;
  };
  XpEpubTestClippingSeed seed{};
  seed.native = &context;
  seed.section = &publication;
  seed.width = 480;
  seed.height = 800;
  seed.signature = &Unexpected<decltype(seed.signature)>::call;
  seed.nonempty = &Unexpected<decltype(seed.nonempty)>::call;
  void* owner = xp_epub_test_clipping_create_legacy_section(&seed, &leaves, &runtime, &resources, &legacySection);
  assert(owner && publicationStep == 5);
  context.signature = xp_epub_test_clipping_signature(owner);
  xp_epub_test_clipping_draw(owner, &context, 0, y, x);
  xp_epub_test_clipping_destroy(owner);
  assert(!context.section && !modelDepth);
  active = nullptr;
}
}  // namespace retirement_planes
