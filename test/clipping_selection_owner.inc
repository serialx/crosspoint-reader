#include "owner_tables.h"
#include "clipping_owner_fixture.h"
namespace retirement_selection {
struct Context {
  const EpubReaderActivity& reader;
  const Page& page;
  std::unique_ptr<EpubReaderActivity::Section> section;
  unsigned publication = 0;
};
static Context* active;
static XpUiReaderRenderServices renderServices{};
static const Page& page(void* handle) { assert(active && handle == &active->page); return active->page; }
static const Block& block(void* handle, uint16_t element) {
  const auto& value = page(handle);
  assert(element < value.elementCount());
  return value.element(element).textBlock();
}
static void publication(void* handle, unsigned step) {
  assert(active && handle == active->section.get() && active->publication++ == step);
}
static auto legacySection = unexpectedLegacySectionServices();
static XpEpubLeaves physicalLeaves() {
  auto table = retirement_fixture::leavesDefaults();
  legacySection.section_page_count = [](void* p) -> uint16_t { publication(p, 0); return active->section->pageCount; };
  legacySection.section_estimated_pages = [](void* p) -> int32_t { publication(p, 1); return active->section->pageCount; };
  legacySection.section_building = [](void* p) -> uint8_t { publication(p, 2); return 0; };
  legacySection.section_complete = [](void* p) -> uint8_t { publication(p, 3); return 1; };
  legacySection.section_partial = [](void* p) -> uint8_t { publication(p, 4); return 0; };
  legacySection.section_close = [](void* p) { assert(active && p == active->section.get()); active->section.reset(); };
  table.render_margins = [](int32_t* out) {
    assert(active && out);
    active->reader.renderer.getOrientedViewableTRBL(&out[0], &out[1], &out[2], &out[3]);
  };
  table.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    assert(resolve <= 1 && out); *out = {};
    out->font = resolve ? SETTINGS.getReaderFontId() : 0;
    out->screen_margin = SETTINGS.screenMargin;
  };
  table.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    assert(active && out && width == active->reader.buildViewportWidth && height == active->reader.buildViewportHeight);
    const auto spec = SETTINGS.readerRenderSpec(width, height);
    *out = {spec.fontId, spec.lineCompression, width, height, spec.extraParagraphSpacing,
            spec.paragraphIndentSpaces, spec.characterSpacing, spec.wordSpacingPercent,
            spec.paragraphAlignment, spec.hyphenationEnabled, spec.embeddedStyle,
            spec.imageRendering, spec.focusReadingEnabled};
  };
  legacySection.page.page_info = [](void* p, XpEpubPageInfo* out) {
    assert(out); *out = {}; out->elements = page(p).elementCount();
  };
  legacySection.page.page_element = [](void* p, uint16_t index, XpEpubElement* out) {
    const auto& value = page(p); assert(out && index < value.elementCount());
    const auto e = value.element(index); *out = {}; out->x = e.xPos; out->y = e.yPos; out->tag = e.getTag();
    if (out->tag == TAG_PageLine) { out->valid = e.textBlock().valid(); out->words = e.textBlock().wordCount(); }
  };
  legacySection.page.page_word = [](void* p, uint16_t element, uint16_t word, XpEpubWord* out) {
    const auto& b = block(p, element); assert(out && word < b.wordCount());
    const auto range = b.wordSourceRange(word);
    *out = {{reinterpret_cast<const uint8_t*>(b.wordText(word)), b.wordTextLen(word)}, b.wordXpos(word),
            range.start, range.end, static_cast<uint8_t>(b.wordStyle(word)), b.getBlockStyle().characterSpacing};
  };
  legacySection.page.page_word_font = [](void* p, uint16_t e, uint16_t w, int32_t base) -> int32_t {
    return TextBlock::renderFontId(active->reader.renderer, block(p, e).wordFontId(w, base), base);
  };
  legacySection.page.page_line_font = [](void* p, uint16_t e, int32_t base) -> int32_t {
    return TextBlock::renderFontId(active->reader.renderer, block(p, e).blockFontId(base), base);
  };
  legacySection.page.page_word_y_offset = [](void* p, uint16_t e, uint16_t w, int32_t base) -> int32_t {
    return block(p, e).wordYOffset(active->reader.renderer, base, w);
  };
  legacySection.page.page_line_ruby_shift = [](void* p, uint16_t e, int32_t font) -> int32_t {
    return block(p, e).getRubyShift(active->reader.renderer.getFontAscenderSize(font));
  };
  table.font_text_advance = [](XpAppText value, int32_t font, uint8_t style, int8_t tracking) -> int32_t {
    assert(value.data && value.data[value.len] == 0);
    return active->reader.renderer.getTextAdvanceX(font, reinterpret_cast<const char*>(value.data),
                                                  static_cast<EpdFontFamily::Style>(style), tracking);
  };
  table.clippings_count = []() -> uint32_t { return CLIPPINGS.clippingCount(); };
  table.clippings_row = [](uint32_t index, XpEpubClip* out) {
    assert(out && index < CLIPPINGS.clippingCount()); const auto& row = *CLIPPINGS.clippingAt(index);
    *out = {row.spineIndex, row.startPage, row.endPage, row.pageCount, row.startWordIndex, row.endWordIndex,
            row.layoutSignature, row.startOffset, row.endOffset};
  };
  table.render_services = [](void* p) -> const XpUiReaderRenderServices* { assert(p == active); return &renderServices; };
  table.paint_pixel = [](int32_t x, int32_t y) { active->reader.renderer.drawPixel(x, y, false); };
  table.paint_dither = [](int32_t x, int32_t y, int32_t width, int32_t height) {
    active->reader.renderer.fillRectDither(x, y, width, height, Color::LightGray);
  };
  table.log_enabled = [](uint8_t severity) -> uint8_t { assert(severity == 0); return 1; };
  table.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    assert(severity == 0 && origin.len == 4 && std::string_view(reinterpret_cast<const char*>(origin.data), origin.len) == "CLIP");
    LOG_DBG("CLIP", "%.*s", int(message.len), reinterpret_cast<const char*>(message.data));
  };
  return table;
}
struct Owner {
  Context context;
  XpEpubLeaves leaves{};
  XpAppRuntime runtime{};
  XpAppResources resources{};
  void* handle;
  Owner(const EpubReaderActivity& reader, const Page& page) : context{reader, page, {}, 0}, handle(nullptr) {
    using namespace retirement_fixture;
    assert(!active);
    if (reader.section) context.section = std::make_unique<EpubReaderActivity::Section>(*reader.section);
    active = &context;
    leaves = physicalLeaves();
    runtime = modelRuntime();
    resources = resourcesDefaults();
    runtime.now_ms = []() -> uint32_t { return millis(); };
    resources.line_height = [](int32_t font) -> int32_t { return active->reader.renderer.getLineHeight(font); };
    renderServices = {};
    renderServices.context = &context;
    renderServices.read = [](void* p, uint8_t query) -> uint32_t {
      assert(p == active);
      if (query == XP_UI_READER_RENDER_IS_A4) return BoardConfig::isEegoA4();
      assert(query == XP_UI_READER_RENDER_IS_BW);
      return active->reader.renderer.getRenderMode() == Renderer::BW;
    };
    renderServices.execute = &Unexpected<decltype(renderServices.execute)>::call;
    XpEpubTestClippingSeed seed{};
    seed.native = &context; seed.section = context.section.get(); seed.spine = reader.currentSpineIndex;
    seed.page = reader.section ? reader.section->currentPage : 0;
    seed.width = reader.buildViewportWidth; seed.height = reader.buildViewportHeight;
    seed.signature = &Unexpected<decltype(seed.signature)>::call;
    seed.nonempty = &Unexpected<decltype(seed.nonempty)>::call;
    handle = xp_epub_test_clipping_create_legacy_section(&seed, &leaves, &runtime, &resources, &legacySection);
    assert(handle && context.publication == (reader.section ? 5u : 0u));
  }
  ~Owner() {
    xp_epub_test_clipping_destroy(handle);
    assert(!context.section && !retirement_fixture::modelDepth);
    active = nullptr;
  }
};
}  // namespace retirement_selection
int EpubReaderActivity::clippingAtPoint(const Page& page, int x, int y) const {
  retirement_selection::Owner owner(*this, page);
  return xp_epub_test_clipping_hit(owner.handle, const_cast<Page*>(&page), x, y);
}
void EpubReaderActivity::drawClippingHighlights(const Page& page, int font, int top, int left) const {
  retirement_selection::Owner owner(*this, page);
  xp_epub_test_clipping_draw(owner.handle, const_cast<Page*>(&page), font, top, left);
}
