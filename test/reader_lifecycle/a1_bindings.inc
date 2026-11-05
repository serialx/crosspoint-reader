// Physical facts for a single App-owned EPUB. No owner policy hooks or seeds.
#include "projection_session_fixture.h"
#include "base_owner_fixture.h"
#include "link_text_session_fixture.h"
#include <span>

#include "BleRust.h"
static void initializeA1Ble() {
  static bleturner::Config config{};
  static bleturner::Profile profile{};
  static bleturner::rust::Services services{};
  services.heap = &AppFixtureUnexpected<decltype(services.heap), 300>::call;
  services.release = &AppFixtureUnexpected<decltype(services.release), 301>::call;
  services.deliver = &AppFixtureUnexpected<decltype(services.deliver), 302>::call;
  services.yieldRadio = &AppFixtureUnexpected<decltype(services.yieldRadio), 303>::call;
  services.transfer = &AppFixtureUnexpected<decltype(services.transfer), 304>::call;
  services.restart = &AppFixtureUnexpected<decltype(services.restart), 305>::call;
  services.hold = &AppFixtureUnexpected<decltype(services.hold), 306>::call;
  services.diagnostic = &AppFixtureUnexpected<decltype(services.diagnostic), 307>::call;
  services.heapMap = &AppFixtureUnexpected<decltype(services.heapMap), 308>::call;
  services.compiledIn = &AppFixtureUnexpected<decltype(services.compiledIn), 309>::call;
  services.begin = &AppFixtureUnexpected<decltype(services.begin), 310>::call;
  services.end = &AppFixtureUnexpected<decltype(services.end), 311>::call;
  services.running = &AppFixtureUnexpected<decltype(services.running), 312>::call;
  services.stopping = &AppFixtureUnexpected<decltype(services.stopping), 313>::call;
  services.connected = &AppFixtureUnexpected<decltype(services.connected), 314>::call;
  services.connecting = &AppFixtureUnexpected<decltype(services.connecting), 315>::call;
  services.activity = &AppFixtureUnexpected<decltype(services.activity), 316>::call;
  services.scanning = &AppFixtureUnexpected<decltype(services.scanning), 317>::call;
  services.poll = &AppFixtureUnexpected<decltype(services.poll), 318>::call;
  services.popRaw = &AppFixtureUnexpected<decltype(services.popRaw), 319>::call;
  services.popKey = &AppFixtureUnexpected<decltype(services.popKey), 320>::call;
  services.arm = &AppFixtureUnexpected<decltype(services.arm), 321>::call;
  services.linked = &AppFixtureUnexpected<decltype(services.linked), 322>::call;
  services.scan = &AppFixtureUnexpected<decltype(services.scan), 323>::call;
  services.connect = &AppFixtureUnexpected<decltype(services.connect), 324>::call;
  services.disconnect = &AppFixtureUnexpected<decltype(services.disconnect), 325>::call;
  services.forget = &AppFixtureUnexpected<decltype(services.forget), 326>::call;
  services.spawn = &AppFixtureUnexpected<decltype(services.spawn), 327>::call;
  services.now = &AppFixtureUnexpected<decltype(services.now), 328>::call;
  services.sleep = &AppFixtureUnexpected<decltype(services.sleep), 329>::call;
  services.memo = &AppFixtureUnexpected<decltype(services.memo), 330>::call;
  services.defaultTable = &AppFixtureUnexpected<decltype(services.defaultTable), 331>::call;
  services.connected = +[] { return false; };
  services.profile = &profile;
  bleturner::rust::xp_ble_init(&services, &config);
}


// ACTUAL_PAGE_METADATA
// ACTUAL_NATIVE_FRAME
// ACTUAL_NATIVE_RESOURCES
// ACTUAL_NATIVE_ACCESS

static NativeResources* a1Root;
static unsigned a1Paints, a1Statuses, a1Fills, a1Copies, a1Finishes;
static unsigned a1Sources, a1Replays, a1Prepares, a1Gray, a1Updates;
static unsigned a1MetadataReads;
static bool a1WrongFont;
struct A1Event { std::string name; int a, b, c; };
static std::vector<A1Event> a1Events;
static void a1Physical(const char* name, int a, int b, int c) {
  assert(f().modelDepth == 0 && criticalDepth == 0);
  a1Events.push_back({name, a, b, c});
  std::printf("a1 %s %d %d %d\n", name, a, b, c);
  if (std::strcmp(name, "page-pass") == 0) {
    assert(sceneRenderDepth == 1 && livePages == 1);
    if (a != 41) a1WrongFont = true;
    assert(b == 11 && c == 3); ++a1Paints;
  } else if (std::strcmp(name, "source") == 0 && a) ++a1Sources;
  else if (std::strcmp(name, "source-replay") == 0) ++a1Replays;
  else if (std::strcmp(name, "prepare") == 0) ++a1Prepares;
  else if (std::strcmp(name, "mode") == 0 && a != 0) ++a1Gray;
}

static XpAppText nativeText(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), uint32_t(std::strlen(value))};
}
static const auto a1NativeServices = /* ACTUAL_SERVICES */;
static const auto a1NativeBind = /* ACTUAL_BIND */;
static const auto a1NativeFinish = /* ACTUAL_FINISH */;
static const auto a1NativeInfo = /* ACTUAL_PAGE_INFO */;
static const auto a1NativeAssign = /* ACTUAL_METADATA_ASSIGN */;
static const auto a1NativeClear = /* ACTUAL_METADATA_CLEAR */;
static const auto a1NativeNotes = /* ACTUAL_METADATA_FOOTNOTES */;
static const auto a1NativeLinks = /* ACTUAL_METADATA_LINKS */;
static const auto a1NativeNote = /* ACTUAL_METADATA_FOOTNOTE */;
static const auto a1NativeLink = /* ACTUAL_METADATA_LINK */;

static size_t a1Find(const char* event, size_t start = 0) {
  for (size_t i = start; i < a1Events.size(); ++i)
    if (a1Events[i].name == event) return i;
  assert(false && "required A1 physical event missing"); return 0;
}

static void bindA1() {
  bindScenePhysical();
  static auto openRoot = legacyBook.section.page.resources_open;
  static auto closeRoot = legacyBook.section.page.resources_close;
  legacyBook.section.page.resources_open = []() -> void* {
    auto* tag = openRoot(); assert(tag && !a1Root);
    a1Root = new NativeResources(*target);
    auto node = f().handles.extract(tag); assert(!node.empty());
    node.key() = a1Root; f().handles.insert(std::move(node));
    a1Physical("root-open", 0, 0, 0); return a1Root;
  };
  legacyBook.section.page.resources_close = [](void* raw) {
    sceneLeaf(1); assert(raw == a1Root && !a1Root->frame && !sceneSection && !livePages);
    a1Physical("root-drop-begin", 0, 0, 0);
    delete a1Root; a1Root = nullptr;
    a1Physical("root-drop-end", 0, 0, 0); closeRoot(raw);
  };
  runtime.notify_render = [] { sceneLeaf(); ++scene.requested; ++a1Updates; a1Physical("update", 0, 0, 0); };
  leaves.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    sceneLeaf(); assert(resolve <= 1); *out = {};
    out->embedded_style = 1; out->menu_style = 1; out->font = resolve ? 41 : 0;
    out->font_size = 19; out->font_family = nativeText("A1 selected family"); out->text_aa = 1;
  };
  leaves.render_geometry = [](XpEpubGeometry* out) {
    sceneLeaf(); *out = {}; out->width = 528; out->height = 792;
    out->has_framebuffer = 1; out->async_refresh = 1; out->title_mode = 1;
    out->strip_enabled = XP_STRIP_RENDER; out->ui_font_12 = 1;
  };
  leaves.render_margins = [](int32_t* out) {
    sceneLeaf(1); const int32_t margins[] = {3, 5, 7, 11};
    std::copy(std::begin(margins), std::end(margins), out);
  };
  leaves.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    sceneLeaf(1); assert(width == 512 && height == 782); *out = {};
    out->font = 41; out->width = width; out->height = height;
    out->compression = 1; out->word_spacing_percent = 100; out->embedded_style = 1;
  };
  legacyBook.section.page.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    sceneLeaf(1); assert(raw == a1Root); return a1NativeServices(raw);
  };
  legacyBook.section.page.render_bind_page = [](void* raw, void* page, int32_t font, int32_t left, int32_t top,
                              int32_t bottom, XpEpubLegacyHighlight callback, void* context) {
    sceneLeaf(1); assert(raw == a1Root && !a1Root->frame && page && livePages == 1);
    assert(left == 11 && top == 3 && bottom == 7 && callback && context);
    if (font != 41) a1WrongFont = true;
    scene.boundPage = page; a1Physical("bind", font, left, top);
    a1NativeBind(raw, page, font, left, top, bottom, callback, context);
  };
  legacyBook.section.page.render_finish_page = [](void* raw) {
    sceneLeaf(1); assert(raw == a1Root && a1Root->frame && scene.boundPage && livePages == 1);
    a1Physical("finish-begin", 0, 0, 0); a1NativeFinish(raw);
    assert(!a1Root->frame && !boundSource && livePages == 1);
    scene.boundPage = nullptr; ++a1Finishes; a1Physical("finish-end", 0, 0, 0);
  };
  legacyBook.section.section_load_page = [](void* raw, int32_t page) -> void* {
    sceneLeaf(1); assert(raw == sceneSection && !livePages);
    auto* result = new Page; result->number = page;
    a1Physical("page-open", page, 0, 0); return result;
  };
  static auto closeSection = legacyBook.section.section_close;
  legacyBook.section.section_close = [](void* raw) {
    sceneLeaf(1); assert(!livePages && !boundSource && !a1Root->frame);
    a1Physical("section-close", 0, 0, 0); f().physical("section-close", f().generation);
    closeSection(raw);
  };
  legacyBook.section.page.page_close = [](void* raw) {
    sceneLeaf(1); assert(raw && !a1Root->frame && !scene.boundPage && !boundSource);
    a1Physical("page-close", static_cast<Page*>(raw)->number, 0, 0);
    delete static_cast<Page*>(raw); assert(!livePages);
  };
  legacyBook.section.page.page_info = [](void* page, XpEpubPageInfo* out) { sceneLeaf(1); a1NativeInfo(page, out); };
  legacyBook.section.page.metadata_assign = [](void* raw, void* page) -> uint8_t {
    sceneLeaf(1); assert(raw == a1Root && livePages == 1);
    const auto result = a1NativeAssign(raw, page); assert(result);
    assert(a1Root->metadata.footnotes().size() == 1 && a1Root->metadata.links().size() == 1);
    ++a1Copies; a1Physical("metadata-copy", static_cast<Page*>(page)->number, 1, 1); return result;
  };
  legacyBook.section.page.metadata_clear = [](void* raw) { sceneLeaf(1); a1NativeClear(raw); a1Physical("metadata-clear", 0, 0, 0); };
  legacyBook.section.page.metadata_footnotes = [](void* raw) -> uint16_t { sceneLeaf(1); return a1NativeNotes(raw); };
  legacyBook.section.page.metadata_links = [](void* raw) -> uint16_t { sceneLeaf(1); ++a1MetadataReads; return a1NativeLinks(raw); };
  legacyBook.section.page.metadata_footnote = [](void* raw, uint16_t row, XpEpubFootnoteSink sink, void* context) -> uint8_t {
    sceneLeaf(1); return a1NativeNote(raw, row, sink, context);
  };
  legacyBook.section.page.metadata_link = [](void* raw, uint16_t row, XpEpubLinkSink sink, void* context) -> uint8_t {
    sceneLeaf(1); ++a1MetadataReads; return a1NativeLink(raw, row, sink, context);
  };
  leaves.clippings_count = []() -> uint32_t { sceneLeaf(); return 1; };
  leaves.clippings_row = [](uint32_t row, XpEpubClip* out) {
    sceneLeaf(1); assert(row == 0); *out = {};
    out->spine = 1; out->page_count = 10; out->start_page = 2; out->end_page = 3;
    out->start_word = out->end_word = 0; out->start_offset = 100; out->end_offset = 104;
  };
  legacyBook.section.page.page_element = [](void* raw, uint16_t element, XpEpubElement* out) {
    sceneLeaf(1); assert(raw == scene.boundPage && livePages == 1 && element == 0);
    *out = {}; out->tag = 1; out->valid = 1; out->words = 1; out->x = 13; out->y = 17;
  };
  legacyBook.section.page.page_word = [](void* raw, uint16_t element, uint16_t word, XpEpubWord* out) {
    sceneLeaf(1); assert(raw == scene.boundPage && element == 0 && word == 0);
    *out = {}; out->text = nativeText("word"); out->x = 2; out->source_start = 100; out->source_end = 104;
  };
  legacyBook.section.page.page_word_font = [](void* raw, uint16_t line, uint16_t word, int32_t base) -> int32_t {
    sceneLeaf(1); assert(raw == scene.boundPage && line == 0 && word == 0);
    if (base != 41) a1WrongFont = true; a1Physical("word-font", base, 0, 0); return base;
  };
  legacyBook.section.page.page_line_font = [](void* raw, uint16_t line, int32_t base) -> int32_t {
    sceneLeaf(1); assert(raw == scene.boundPage && line == 0); if (base != 41) a1WrongFont = true; return base;
  };
  legacyBook.section.page.page_word_y_offset = [](void* raw, uint16_t line, uint16_t word, int32_t base) -> int32_t {
    sceneLeaf(1); assert(raw == scene.boundPage && line == 0 && word == 0); if (base != 41) a1WrongFont = true; return 4;
  };
  legacyBook.section.page.page_line_ruby_shift = [](void* raw, uint16_t line, int32_t base) -> int32_t {
    sceneLeaf(1); assert(raw == scene.boundPage && line == 0); if (base != 41) a1WrongFont = true; return 6;
  };
  leaves.font_text_advance = [](XpAppText value, int32_t font, uint8_t style, int8_t spacing) -> int32_t {
    sceneLeaf(1); assert(text(value) == "word" && style == 0 && spacing == 0);
    if (font != 41) a1WrongFont = true; return 20;
  };
  resources.line_height = [](int32_t font) -> int32_t { sceneLeaf(1); if (font != 41) a1WrongFont = true; return 18; };
  leaves.paint_dither = [](int32_t x, int32_t y, int32_t width, int32_t height) {
    sceneLeaf(1); assert(scene.boundPage && x == 26 && y == 30 && width == 20 && height == 18);
    ++a1Fills; a1Physical("highlight", x, y, width);
  };
  leaves.paint_pixel = [](int32_t, int32_t) { assert(false && "non-A4 physical profile uses dither"); };
  leaves.paint_status = [](const XpEpubStatus* value) {
    sceneLeaf(1); assert(scene.boundPage && livePages == 1);
    assert(value && value->page >= 3 && value->page <= 4 && value->pages == 10);
    assert(text(value->title) == "Chapter" && !value->building && !value->bookmarked);
    ++a1Statuses; a1Physical("status", value->page, value->pages, value->y_offset);
  };
  leaves.render_restore_bw = [](uint8_t resync) { sceneLeaf(1); assert(!resync); target->restoreBwBuffer(); };
  leaves.render_discard_bw = [] { sceneLeaf(1); a1Physical("discard", 0, 0, 0); };
  platform = {&legacy, &resources, &runtime, &leaves};
}
