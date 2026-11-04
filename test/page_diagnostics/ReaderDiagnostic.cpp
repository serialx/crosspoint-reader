#include <Logging.h>

#include "app_entry_fixture.inc"

extern "C" void xp_epub_test_book_render(void*);
namespace {
Fixture diagnosticFixture;
bool initialized;
struct Diagnostic {
  void* owner = nullptr;
  unsigned char section = 0, page = 0;
  bool live = false, metadataFailure = false, measured = false;
  unsigned opened = 0, closed = 0, loads = 0, copies = 0, pageCloses = 0;
  unsigned abandons = 0, clears = 0, paints = 0, logs = 0;
} diagnostic;
XpUiReaderRenderServices diagnosticRender{};
void (*entryLog)(uint8_t, XpAppText, XpAppText);
void physical() { assert(f().renderDepth == 1 && f().modelDepth == 0 && diagnostic.measured); }
void section(void* raw) {
  physical();
  assert(raw == &diagnostic.section && diagnostic.live);
}
void bindDiagnostic() {
  entryLog = leaves.log;
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText value) {
    if (!diagnostic.measured) {
      entryLog(severity, origin, value);
      return;
    }
    physical();
    assert(severity == 1 && origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    ++diagnostic.logs;
    LOG_ERR("ERS", "%.*s", int(value.len), reinterpret_cast<const char*>(value.data));
  };
  leaves.log_enabled = [](uint8_t severity) -> uint8_t {
    assert(severity <= 2 && !f().modelDepth);
    return diagnostic.measured && severity == 1;
  };
  runtime.now_ms = []() -> uint32_t {
    assert(!f().modelDepth);
    return 0;
  };
  legacyBook.book_spine_count = [](void* raw) -> int32_t {
    physical();
    f().handle(raw, Kind::Book);
    return 2;
  };
  legacyBook.book_spine = [](void* raw, int32_t index, XpEpubSpineSink sink, void* context) -> uint8_t {
    physical();
    f().handle(raw, Kind::Book);
    assert(index == 1 && sink);
    static const uint8_t href[] = "chapter.xhtml";
    const XpEpubSpine value{{href, sizeof(href) - 1}, 1};
    return sink(context, &value);
  };
  legacyBook.section.section_open = [](void* raw, int32_t spine) -> void* {
    physical();
    f().handle(raw, Kind::Book);
    assert(spine == 1 && !diagnostic.live);
    diagnostic.live = true;
    ++diagnostic.opened;
    return &diagnostic.section;
  };
  legacyBook.section.section_close = [](void* raw) {
    section(raw);
    diagnostic.live = false;
    ++diagnostic.closed;
  };
  legacyBook.section.section_page_count = [](void* raw) -> uint16_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_estimated_pages = [](void* raw) -> int32_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_building = [](void* raw) -> uint8_t {
    section(raw);
    return 0;
  };
  legacyBook.section.section_complete = [](void* raw) -> uint8_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_partial = [](void* raw) -> uint8_t {
    section(raw);
    return 0;
  };
  legacyBook.section.section_load_cache = [](void* raw, const XpEpubRenderSpec* spec) -> uint8_t {
    section(raw);
    assert(spec && spec->width == 480 && spec->height == 800);
    return 1;
  };
  legacyBook.section.section_load_page = [](void* raw, int32_t index) -> void* {
    section(raw);
    assert(index == 0 && ++diagnostic.loads == 1);
    return diagnostic.metadataFailure ? &diagnostic.page : nullptr;
  };
  legacyBook.section.page.metadata_clear = [](void* raw) {
    physical();
    f().handle(raw, Kind::Root);
  };
  legacyBook.section.page.metadata_assign = [](void* raw, void* page) -> uint8_t {
    physical();
    f().handle(raw, Kind::Root);
    assert(page == &diagnostic.page && diagnostic.metadataFailure);
    ++diagnostic.copies;
    return 0;
  };
  legacyBook.section.page.page_close = [](void* raw) {
    physical();
    assert(raw == &diagnostic.page);
    ++diagnostic.pageCloses;
  };
  legacyBook.section.section_abandon_build = [](void* raw) {
    section(raw);
    ++diagnostic.abandons;
  };
  legacyBook.section.section_clear_cache = [](void* raw) -> uint8_t {
    section(raw);
    ++diagnostic.clears;
    return 1;
  };
  leaves.bookmarks_count = [](void* raw) -> uint32_t {
    physical();
    f().handle(raw, Kind::Bookmarks);
    return 0;
  };
  leaves.radio_settle_start = [] { physical(); };
  leaves.render_margins = [](int32_t* out) {
    physical();
    assert(out);
    for (unsigned i = 0; i < 4; ++i) out[i] = 0;
  };
  leaves.render_geometry = [](XpEpubGeometry* out) {
    physical();
    assert(out);
    *out = {};
    out->width = 480;
    out->height = 800;
  };
  leaves.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    physical();
    assert(out && width == 480 && height == 800);
    *out = {};
    out->width = width;
    out->height = height;
  };
  diagnosticRender.read = &AppFixtureUnexpected<decltype(diagnosticRender.read), 1000>::call;
  diagnosticRender.execute = [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
    physical();
    assert(op && op->kind == XP_UI_READER_RENDER_CLEAR && op->value == 255);
    ++diagnostic.paints;
    return 0;
  };
  leaves.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    physical();
    f().handle(raw, Kind::Root);
    return &diagnosticRender;
  };
}
}  // namespace

// All real App/owner/path setup precedes the caller's unchanged measurement reset.
void readerDiagnosticPrepare() {
  assert(!diagnostic.owner);
  xp_test_fail_allocation_after(SIZE_MAX);
  if (!initialized) {
    fixture = &diagnosticFixture;
    bind();
    bindDiagnostic();
    initialize();
    initialized = true;
  }
  diagnostic = {};
  f().startAttempt();
  assert(install() == 1);
  if (xp_app_test_pending_kind()) assert(xp_app_test_drain() == 1);
  diagnostic.owner = xp_app_test_current_epub();
  assert(diagnostic.owner && f().extractor && f().clippings == f().extractor);
  assert(!f().renderDepth && !f().modelDepth);
}

void readerFailureDiagnostic(bool metadataFailure) {
  assert(diagnostic.owner && !diagnostic.measured && !diagnostic.loads);
  diagnostic.metadataFailure = metadataFailure;
  diagnostic.measured = true;
  void* lock = runtime.render_lock();
  xp_epub_test_book_render(diagnostic.owner);
  runtime.render_unlock(lock);
  diagnostic.measured = false;
  assert(diagnostic.opened == 1 && diagnostic.closed == 1 && !diagnostic.live);
  assert(diagnostic.loads == 1 && diagnostic.copies == unsigned(metadataFailure));
  assert(diagnostic.pageCloses == unsigned(metadataFailure));
  assert(diagnostic.abandons == 1 && diagnostic.clears == 1);
  assert(diagnostic.paints == 1 && diagnostic.logs == 1);
}

// Called after track=false and the original zero-Rust-allocation-attempt assertion.
void readerDiagnosticFinish() {
  assert(diagnostic.owner && !diagnostic.measured && !diagnostic.live);
  XpAppEvent event{};
  event.kind = XP_APP_SLEEP_REQUESTED;
  event.value.sleep_from_timeout = 0;
  assert(xp_app_event(&event) == XP_APP_ACCEPTED);
  assert(!xp_app_test_current_epub() && xp_app_test_current_legacy());
  assert(!f().extractor && !f().clippings && !f().renderDepth && !f().modelDepth);
  assert(f().handles.size() == 1);  // The real App retains its native Sleep activity.
  diagnostic.owner = nullptr;
}
