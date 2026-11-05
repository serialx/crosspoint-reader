#pragma once
#include <BlePageTurner.h>

#include "../ble_link_note_title/owner_tables.h"
#include "legacy_book_fixture.h"
#include "position_session_fixture.h"
#include "view_values_session_fixture.h"
extern "C" void xp_epub_test_loop(void*);

class LinkNoteTitle {
 public:
  LinkNoteTitle() {
    using namespace retirement_fixture;
    assert(!active && !modelDepth);
    active = this;
    leaves = legacyLeavesDefaults();
    runtime = modelRuntime();
    resources = resourcesDefaults();
    runtime.render_lock = []() -> void* {
      physical();
      assert(!active->renderDepth);
      active->renderDepth = 1;
      return active;
    };
    runtime.render_try_lock = runtime.render_lock;
    runtime.render_unlock = [](void* token) {
      physical();
      assert(token == active && active->renderDepth == 1);
      active->renderDepth = 0;
    };
    runtime.now_ms = []() -> uint32_t {
      physical();
      return 1000;
    };
    leaves.physical.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
      physical();
      assert(active->renderDepth == 1);
      *out = {};
      out->note = static_cast<uint8_t>(active->current);
      if (peer) {
        assert(active->drawing && active->titleShown && active->current == bleturner::LinkNote::Connected);
        *peer = {};
        peer->name[0] = 'P';
        ++active->peerReads;
      } else
        ++active->radioReads;
    };
    leaves.physical.render_geometry = [](XpEpubGeometry* out) {
      physical();
      *out = {};
      out->width = 528;
      out->height = 792;
      out->title_mode = active->titleShown ? 0 : 2;
    };
    leaves.physical.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
      physical();
      assert(!resolve);
      *out = {};
    };
    leaves.physical.render_busy = []() -> uint8_t {
      physical();
      return 0;
    };
    leaves.book.book_spine_count = [](void* book) -> int32_t {
      physical();
      assert(book == &active->book);
      return 1;
    };
    leaves.book.book_progress = [](void* book, int32_t spine, float) -> float {
      physical();
      assert(book == &active->book && spine == 0);
      return 0.0f;
    };
    leaves.book.book_title = [](void* book, XpAppTextSink sink, void* context) -> uint8_t {
      physical();
      assert(book == &active->book);
      return sink(context, literal("Book"));
    };
    leaves.book.book_language = [](void* book, XpAppTextSink sink, void* context) -> uint8_t {
      physical();
      assert(book == &active->book);
      return sink(context, literal("en"));
    };
    leaves.physical.paint_status = [](const XpEpubStatus* status) {
      physical();
      assert(active->renderDepth == 1 && status);
      ++active->paints;
    };
    resources.translate = [](uint16_t) -> XpAppText {
      physical();
      return literal("Radio note");
    };
    leaves.physical.input_facts = [](XpEpubInputFacts* out) {
      physical();
      *out = {};
    };
    leaves.physical.has_sync_credentials = []() -> uint8_t {
      physical();
      return 0;
    };
    leaves.physical.input_services = []() -> const XpUiReaderInputServices* {
      physical();
      assert(!active->renderDepth);
      static const XpUiReaderInputServices input{nullptr,
                                                 [](void*, uint32_t, uint8_t query, uint32_t) -> uint8_t {
                                                   physical();
                                                   assert(query <= XP_UI_READER_BUTTON_LONG);
                                                   return 0;
                                                 },
                                                 [](void*, uint8_t query) -> uint32_t {
                                                   physical();
                                                   switch (query) {
                                                     case XP_UI_READER_INPUT_HAS_TOUCH:
                                                     case XP_UI_READER_INPUT_HELD_MS:
                                                     case XP_UI_READER_INPUT_HOME_ACTION:
                                                     case XP_UI_READER_INPUT_BACK_GESTURE:
                                                     case XP_UI_READER_INPUT_NAV_SWAPPED:
                                                       return 0;
                                                     default:
                                                       // In particular, no native or shared swipe value is synthesized.
                                                       assert(false && "unexpected idle link-note input query");
                                                       std::abort();
                                                   }
                                                 },
                                                 &Unexpected<uint8_t (*)(void*, int32_t*, int32_t*)>::call};
      return &input;
    };
    const XpEpubTestPositionSeed seed{this, &book, nullptr, 0, 0, nullptr, &Unexpected<void (*)(void*, int32_t)>::call};
    owner = xp_epub_test_position_create_legacy_book(&seed, &leaves.physical, &runtime, &resources, &leaves.book);
    assert(owner);
  }
  ~LinkNoteTitle() {
    xp_epub_test_position_destroy(owner);
    assert(active == this && !renderDepth && !retirement_fixture::modelDepth);
    active = nullptr;
  }
  LinkNoteTitle(const LinkNoteTitle&) = delete;
  LinkNoteTitle& operator=(const LinkNoteTitle&) = delete;

  bleturner::LinkNote draw(bool shown, bleturner::LinkNote now) {
    current = now;
    titleShown = shown;
    const auto oldPaints = paints, oldRadio = radioReads;
    const auto oldPeers = peerReads;
    drawing = true;
    void* token = runtime.render_lock();
    xp_epub_test_view_status(owner);
    runtime.render_unlock(token);
    drawing = false;
    assert(peerReads == oldPeers + unsigned(shown && now == bleturner::LinkNote::Connected));
    const auto state = observe();
    assert(state.note_shown == shown && paints == oldPaints + 1 && radioReads == oldRadio + 1);
    return static_cast<bleturner::LinkNote>(state.note);
  }
  bool repaint(bleturner::LinkNote now) {
    const auto before = observe();
    current = now;
    const auto oldRadio = radioReads;
    xp_epub_test_loop(owner);
    const auto after = observe();
    assert(radioReads == oldRadio + 1 && !renderDepth);
    assert(after.note_shown <= before.note_shown);
    if (after.note_shown) assert(after.note == before.note);
    // Observe consumption by the ordinary loop; do not compare note values in C++.
    return before.note_shown != after.note_shown;
  }

 private:
  inline static LinkNoteTitle* active;
  LegacyEpubLeaves leaves{};
  XpAppRuntime runtime{};
  XpAppResources resources{};
  void* owner = nullptr;
  uint8_t book = 0;
  unsigned renderDepth = 0, radioReads = 0, paints = 0, peerReads = 0;
  bool drawing = false;
  bool titleShown = false;
  bleturner::LinkNote current = bleturner::LinkNote::None;
  static void physical() { assert(active && !retirement_fixture::modelDepth); }
  static XpAppText literal(const char* value) {
    size_t size = 0;
    while (value[size]) ++size;
    return {reinterpret_cast<const uint8_t*>(value), size};
  }
  XpEpubTestViewObservation observe() const {
    XpEpubTestViewObservation value{};
    xp_epub_test_view_observe(owner, &value);
    assert(value.note_shown <= 1);
    return value;
  }
};
static_assert(static_cast<uint8_t>(bleturner::LinkNote::None) == 0);
static_assert(static_cast<uint8_t>(bleturner::LinkNote::Connecting) == 1);
static_assert(static_cast<uint8_t>(bleturner::LinkNote::Failed) == 2);
