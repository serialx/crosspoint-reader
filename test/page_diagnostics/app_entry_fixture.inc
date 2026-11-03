// Frozen App ownership oracle with explicit synthetic book inputs.
#include "../reader_lifecycle/legacy_book_fixture.h"
#include <array>
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "physical_defaults.h"

extern "C" {
uint8_t xp_app_test_install_epub_legacy_book(XpAppText, uint8_t, const XpEpubLeaves*, const XpEpubLegacyBookServices*);
void* xp_app_test_current_epub();
uint8_t xp_app_test_drain();
void xp_test_fail_allocation_after(size_t);
size_t xp_test_live_allocations();
size_t xp_test_allocation_attempts();
}

namespace {
enum class Kind : unsigned { Root, Bookmarks, OverlayPopup, FailurePopup, Book, File, Legacy, Count };
constexpr unsigned kinds = unsigned(Kind::Count);
const char* names[] = {"root", "bookmarks", "overlay-popup", "failure-popup", "book", "file", "legacy"};
struct Handle {
  Kind kind;
  unsigned generation, id;
  uint8_t activityKind = XP_APP_SLEEP;
  std::string path;
};
struct Event {
  std::string name;
  unsigned generation, id;
  int render, model;
};
struct Fixture {
  int renderDepth = 0, modelDepth = 0, maxRender = 0, maxModel = 0;
  unsigned nextId = 0, generation = 0, componentAttempt = 0;
  int failComponent = -1;
  std::map<void*, std::unique_ptr<Handle>> handles;
  std::array<unsigned, kinds> acquired{}, released{};
  std::vector<Event> events;
  Handle* extractor = nullptr;
  Handle* clippings = nullptr;
  unsigned entered = 0, exited = 0, clearedExtractor = 0, unloadedClippings = 0;
  unsigned bookLoads = 0, bookmarkLoads = 0, progressReads = 0;
  unsigned entersGeneration = 0;
  unsigned char lockToken = 0;
  std::string path = "/" + std::string(4096, 'p') + ".epub";

  void trace(const std::string& name, unsigned generation = 0, unsigned id = 0) {
    (void)name; (void)generation; (void)id;
  }
  void physical(const std::string& name, unsigned generation = 0, unsigned id = 0) {
    assert(modelDepth == 0);
    trace(name, generation, id);
  }
  Handle& handle(const void* raw, Kind kind) {
    auto it = handles.find(const_cast<void*>(raw));
    assert(it != handles.end() && it->second->kind == kind);
    return *it->second;
  }
  Handle* acquire(Kind kind, unsigned ownerGeneration) {
    auto value = std::make_unique<Handle>();
    value->kind = kind;
    value->generation = ownerGeneration;
    value->id = ++nextId;
    auto* pointer = value.get();
    handles.emplace(pointer, std::move(value));
    ++acquired[unsigned(kind)];
    physical(std::string("open:") + names[unsigned(kind)], ownerGeneration, pointer->id);
    return pointer;
  }
  void release(void* raw, Kind kind) {
    auto& value = handle(raw, kind);
    physical(std::string("close:") + names[unsigned(kind)], value.generation, value.id);
    ++released[unsigned(kind)];
    handles.erase(raw);
  }
  void* component(Kind kind) {
    assert(renderDepth == 0 && modelDepth == 0);
    const unsigned ordinal = componentAttempt++;
    assert(ordinal == unsigned(kind) && ordinal < 4);
    if (int(ordinal) == failComponent) {
      physical(std::string("fail:") + names[ordinal], generation);
      return nullptr;
    }
    return acquire(kind, generation);
  }
  void startAttempt(int failure = -1) {
    assert(renderDepth == 0 && modelDepth == 0);
    ++generation;
    componentAttempt = 0;
    failComponent = failure;
  }
  void count(Kind kind, unsigned opened, unsigned closed) const {
    assert(acquired[unsigned(kind)] == opened && released[unsigned(kind)] == closed);
  }
  void componentOrder(unsigned ownerGeneration, const std::vector<std::string>& expected) const {
    std::vector<std::string> actual;
    for (const auto& event : events) {
      if (event.generation != ownerGeneration) continue;
      for (unsigned i = 0; i < 4; ++i) {
        if (event.name == std::string("open:") + names[i] || event.name == std::string("close:") + names[i] ||
            event.name == std::string("fail:") + names[i])
          actual.push_back(event.name);
      }
    }
    assert(actual == expected);
  }
  size_t find(const std::string& name, unsigned ownerGeneration, size_t start = 0) const {
    for (size_t i = start; i < events.size(); ++i)
      if (events[i].name == name && events[i].generation == ownerGeneration) return i;
    std::fprintf(stderr, "Missing physical event %s for generation %u\n", name.c_str(), ownerGeneration);
    std::abort();
  }
  void ordered(unsigned ownerGeneration, std::initializer_list<const char*> sequence) const {
    size_t cursor = 0;
    for (const char* name : sequence) cursor = find(name, ownerGeneration, cursor) + 1;
  }
  void dump() const {
    for (const auto& event : events)
      std::printf("event %s generation=%u handle=%u render=%d model=%d\n", event.name.c_str(), event.generation,
                  event.id, event.render, event.model);
    for (unsigned i = 0; i < kinds; ++i)
      std::printf("handles %s acquired=%u released=%u\n", names[i], acquired[i], released[i]);
    std::printf("depth render=%d model=%d maxima=%d/%d entry=%u exit=%u extractor-clear=%u clippings-unload=%u\n",
                renderDepth, modelDepth, maxRender, maxModel, entered, exited, clearedExtractor, unloadedClippings);
  }
};
Fixture* fixture;
Fixture& f() {
  assert(fixture);
  return *fixture;
}
std::string text(XpAppText value) {
  assert(value.data || value.len == 0);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
XpAppText wire(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
uint8_t deliver(XpAppTextSink sink, void* context, const char* value) {
  assert(sink && f().modelDepth == 0);
  const auto result = sink(context, {reinterpret_cast<const uint8_t*>(value), std::strlen(value)});
  assert(result == 1 && f().modelDepth == 0);
  return result;
}
static XpEpubLeaves leaves;
static XpEpubLegacyBookServices legacyBook;
static XpAppLegacy legacy;
static XpAppResources resources;
static XpAppRuntime runtime;
static XpAppPlatform platform;

void bind() {
  leaves = unexpectedEpubLeaves();
  legacyBook = unexpectedLegacyBookServices();
  leaves.book_set_extractor = [](void* book) {
    if (book) legacyBook.book_install_extractor(book);
    else legacyBook.book_clear_extractor();
  };
  legacy = unexpectedAppLegacy();
  resources = unexpectedAppResources();
  runtime = unexpectedAppRuntime();
  runtime.model_lock = [] {
    auto& x = f();
    assert(!x.modelDepth);
    x.trace("model-lock");
    x.modelDepth = 1;
    x.maxModel = 1;
  };
  runtime.model_unlock = [] {
    auto& x = f();
    assert(x.modelDepth == 1);
    x.trace("model-unlock");
    x.modelDepth = 0;
  };
  runtime.render_lock = []() -> void* {
    auto& x = f();
    assert(!x.renderDepth && !x.modelDepth);
    x.physical("render-lock");
    x.renderDepth = 1;
    x.maxRender = 1;
    return &x.lockToken;
  };
  runtime.render_unlock = [](void* raw) {
    auto& x = f();
    assert(raw == &x.lockToken && x.renderDepth == 1 && !x.modelDepth);
    x.physical("render-unlock");
    x.renderDepth = 0;
  };
  runtime.header_back_clear = [] {
    assert(f().renderDepth == 1);
    f().physical("header-clear");
  };
  runtime.notify_render = [] { f().physical("notify-render"); };
  runtime.ble_before_screen_change = [](uint8_t reader) -> uint8_t {
    assert(reader <= 1 && !f().renderDepth);
    f().physical("before-screen-change");
    return 1;
  };
  legacy.create = [](const XpAppLaunch* launch) -> void* {
    assert(launch && launch->kind == XP_APP_SLEEP && !f().renderDepth);
    auto* value = f().acquire(Kind::Legacy, 0);
    value->activityKind = XP_APP_SLEEP;
    return value;
  };
  legacy.facts = [](const void* raw, uint8_t, uint8_t) -> XpAppLegacyFacts {
    auto& value = f().handle(raw, Kind::Legacy);
    f().physical("legacy-facts", 0, value.id);
    XpAppLegacyFacts facts{};
    facts.kind = value.activityKind;
    return facts;
  };
  legacy.enter = [](void* raw) {
    auto& h = f().handle(raw, Kind::Legacy);
    assert(!f().renderDepth);
    f().physical("legacy-enter", 0, h.id);
  };
  legacy.exit = [](void* raw) {
    auto& h = f().handle(raw, Kind::Legacy);
    assert(f().renderDepth == 1);
    f().physical("legacy-exit", 0, h.id);
  };
  legacy.destroy = [](void* raw) { f().release(raw, Kind::Legacy); };
  legacy.with_result = [](void* raw, XpAppResultSink consume, void* context) {
    const unsigned id = f().handle(raw, Kind::Legacy).id;
    assert(consume && f().renderDepth == 1 && !f().modelDepth);
    f().physical("result-begin", 0, id);
    consume(context, nullptr);
    // The continuation destroyed raw; only the copied scalar ID remains usable.
    assert(f().handles.find(raw) == f().handles.end());
    assert(!f().renderDepth && !f().modelDepth);
    assert(!xp_app_test_current_legacy() && !xp_app_test_current_epub() && !xp_app_test_is_home());
    f().physical("result-end", 0, id);
  };

  leaves.resources_open = [] { return f().component(Kind::Root); };
  leaves.bookmarks_open = [] { return f().component(Kind::Bookmarks); };
  leaves.popup_open = [] {
    assert(f().componentAttempt == 2 || f().componentAttempt == 3);
    return f().component(Kind(f().componentAttempt));
  };
  leaves.popup_close = [](void* raw) {
    auto it = f().handles.find(raw);
    assert(it != f().handles.end());
    const Kind kind = it->second->kind;
    assert(kind == Kind::OverlayPopup || kind == Kind::FailurePopup);
    f().release(raw, kind);
  };
  leaves.bookmarks_close = [](void* raw) { f().release(raw, Kind::Bookmarks); };
  leaves.resources_close = [](void* raw) {
    auto& root = f().handle(raw, Kind::Root);
    for (const auto& entry : f().handles) assert(entry.first == raw || entry.second->generation != root.generation);
    f().release(raw, Kind::Root);
  };
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    if (text(origin) == "MEM") {
      assert(severity == 2 && (text(message) == "reader enter: free=100000 max_block=100000" ||
                               text(message) == "reader exit: free=100000 max_block=100000"));
      f().physical("memory-log");
      return;
    }
    if (text(origin) == "ERS") {
      assert(severity == 0 && text(message) == "Loaded cache: 1, 2");
      f().physical("progress-log");
      return;
    }
    assert(severity == 0 && text(origin) == "ACT");
    if (text(message) == "Entering activity: EpubReader") {
      assert(!f().renderDepth);
      ++f().entered;
      f().entersGeneration = f().generation;
      f().physical("entry", f().entersGeneration);
    } else {
      assert(text(message) == "Exiting activity: EpubReader" && f().renderDepth == 1 && f().extractor);
      ++f().exited;
      f().physical("exit", f().extractor->generation);
    }
  };
  leaves.log_enabled = [](uint8_t severity) -> uint8_t {
    assert(severity <= 2);
    f().physical("log-enabled");
    return 0;
  };
  leaves.heap_snapshot = [](XpEpubHeap* out) {
    f().physical("heap");
    assert(out);
    *out = {100000, 100000};
  };
  resources.path_exists = [](XpAppText path) -> uint8_t {
    const auto value = text(path);
    f().physical("path-exists");
    if (value == f().path || value == "/cache/book.bin") return 1;
    assert(value == "/cache/links.bin");
    return 0;
  };
  leaves.state_open_path = [](XpAppTextSink sink, void* context) -> uint8_t {
    f().physical("state-open-path");
    return deliver(sink, context, "");
  };
  leaves.fonts_ensure_loaded = [] {
    assert(!f().renderDepth);
    f().physical("fonts-load", f().entersGeneration);
  };
  leaves.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    assert(resolve == 0 && out);
    f().physical("settings");
    *out = {};
    out->embedded_style = 1;
  };
  leaves.render_orientation = [](uint8_t orientation) {
    assert(orientation == 0);
    f().physical("orientation");
  };
  legacyBook.book_open = [](XpAppText path) -> void* {
    assert(!f().renderDepth && text(path) == f().path);
    auto* book = f().acquire(Kind::Book, f().entersGeneration);
    book->path = text(path);
    return book;
  };
  legacyBook.book_cache_path = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    auto& book = f().handle(raw, Kind::Book);
    f().physical("cache-path", book.generation, book.id);
    return deliver(sink, context, "/cache");
  };
  legacyBook.book_load = [](void* raw, uint8_t build, uint8_t skip) -> uint8_t {
    auto& book = f().handle(raw, Kind::Book);
    assert(build == 1 && skip == 0 && !f().renderDepth);
    ++f().bookLoads;
    f().physical("book-load", book.generation, book.id);
    return 1;
  };
  leaves.image_clear_failures = [] {
    assert(!f().renderDepth);
    f().physical("image-clear", f().entersGeneration);
  };
  legacyBook.book_install_extractor = [](void* raw) {
    auto& book = f().handle(raw, Kind::Book);
    assert(!f().extractor && !f().renderDepth);
    f().extractor = &book;
    f().physical("extractor-install", book.generation, book.id);
  };
  legacyBook.book_setup_cache = [](void* raw) {
    auto& book = f().handle(raw, Kind::Book);
    f().physical("cache-setup", book.generation, book.id);
  };
  leaves.file_open_read = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    assert(text(path) == "/cache/progress.bin" && f().extractor && !f().renderDepth);
    return f().acquire(Kind::File, f().extractor->generation);
  };
  leaves.file_read = [](void* raw, uint8_t* out, uint32_t capacity) -> int32_t {
    auto& file = f().handle(raw, Kind::File);
    assert(out && capacity == 10 && !f().renderDepth);
    const uint8_t saved[] = {1, 0, 2, 0};
    std::memcpy(out, saved, sizeof(saved));
    ++f().progressReads;
    f().physical("progress-read", file.generation, file.id);
    return sizeof(saved);
  };
  leaves.file_close = [](void* raw) -> uint8_t { f().release(raw, Kind::File);  return 1; };
  leaves.bookmarks_clear = [](void* raw) {
    auto& h = f().handle(raw, Kind::Bookmarks);
    assert(!f().renderDepth);
    f().physical("bookmarks-clear", h.generation, h.id);
  };
  legacyBook.bookmarks_load = [](void* raw, void* bookRaw) -> uint8_t {
    auto& h = f().handle(raw, Kind::Bookmarks);
    auto& book = f().handle(bookRaw, Kind::Book);
    assert(h.generation == book.generation && !f().renderDepth);
    ++f().bookmarkLoads;
    f().physical("bookmarks-load", h.generation, h.id);
    return 1;
  };
  legacyBook.clippings_load = [](void* raw) -> uint8_t {
    auto& book = f().handle(raw, Kind::Book);
    assert(!f().clippings && !f().renderDepth);
    f().clippings = &book;
    f().physical("clippings-load", book.generation, book.id);
    return 1;
  };
  leaves.fonts_release_caches = [] {
    assert(f().renderDepth == 1 && f().extractor);
    f().physical("fonts-release", f().extractor->generation);
  };
  leaves.has_exit_subscriber = []() -> uint8_t {
    assert(f().renderDepth == 1);
    f().physical("exit-subscriber");
    return 0;
  };
  leaves.state_reset_reader_count = [] {
    assert(f().renderDepth == 1 && f().extractor);
    f().physical("reader-count-reset", f().extractor->generation);
  };
  leaves.state_save = [] {
    assert(f().renderDepth == 1 && f().extractor);
    f().physical("state-save", f().extractor->generation);
  };
  legacyBook.book_clear_extractor = [] {
    assert(f().renderDepth == 1 && f().extractor);
    ++f().clearedExtractor;
    f().physical("extractor-clear", f().extractor->generation);
    f().extractor = nullptr;
  };
  leaves.clippings_unload = [] {
    assert(f().renderDepth == 1 && f().clippings);
    ++f().unloadedClippings;
    f().physical("clippings-unload", f().clippings->generation);
    f().clippings = nullptr;
  };
  legacyBook.book_close = [](void* raw) {
    assert(f().renderDepth == 1 && !f().extractor && !f().clippings);
    f().release(raw, Kind::Book);
  };
  platform = {&legacy, &resources, &runtime, &leaves};
}

void initialize() {
  XpAppCompatibility compatibility{};
  assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
  assert(compatibility.activity_facts && compatibility.home_action && compatibility.screenshot_info);
}
uint8_t install() { return xp_app_test_install_epub_legacy_book(wire(f().path), 0, &leaves, &legacyBook); }
}
