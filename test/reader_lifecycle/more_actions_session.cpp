#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

#include "overlay_actions_session_fixture.h"

extern "C" {
uint8_t xp_epub_test_overlay_build_more(void*, void*, uint8_t[19], uint8_t*);
void xp_epub_test_overlay_reject(void*);
void xp_epub_test_overlay_publish(void*);
}

// NATIVE_LABEL_ENUM
static unsigned flags;
struct {
  bool present() const { return (flags & 8) != 0; }
} Frontlight;
static bool slideSwitchOwnsLight() { return (flags & 16) != 0; }
namespace bleturner {
struct Status {
  bool compiledIn;
};
static Status status() { return {(flags & 32) != 0}; }
}  // namespace bleturner
struct {
  char dictionaryName[2]{};
} SETTINGS;
struct EpubReaderMenuActivity {
  // NATIVE_MENU_ENUM
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };
  // NATIVE_MENU_CAPACITY
  static void buildMenuItems(std::vector<MenuItem>&, bool, bool, bool);
};
// NATIVE_MENU_METHOD

namespace candidate {
static void* owner;
static int native, bookmarks, lockToken;
static unsigned renderDepth, modelDepth, acquisitions, releases, rejectAt;
static std::vector<unsigned> probes;
enum Probe { Footnotes, Bookmarks, Clippings, FrontlightPresent, InputFacts, Radio, Settings };
static void leaf(unsigned depth) { assert(modelDepth == 0 && renderDepth == depth); }
static const XpAppResources resources = overlayUnexpectedResources();
static const XpAppRuntime runtime = [] {
  auto table = overlayUnexpectedRuntime();
  table.model_lock = [] {
    assert(modelDepth == 0);
    ++modelDepth;
  };
  table.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
  };
  table.render_lock = []() -> void* {
    leaf(0);
    ++renderDepth;
    ++acquisitions;
    if (rejectAt == acquisitions) {
      assert(owner);
      xp_epub_test_overlay_reject(owner);
      leaf(1);
    }
    return &lockToken;
  };
  table.render_unlock = [](void* value) {
    leaf(1);
    assert(value == &lockToken);
    ++releases;
    --renderDepth;
  };
  table.frontlight_present = []() -> uint8_t {
    leaf(0);
    probes.push_back(FrontlightPresent);
    return (flags & 8) != 0;
  };
  return table;
}();
static const XpEpubLeaves leaves = [] {
  auto table = overlayUnexpectedLeaves();
  table.metadata_footnotes = [](void* value) -> uint16_t {
    leaf(1);
    assert(value == &native && acquisitions == 1);
    probes.push_back(Footnotes);
    return (flags & 1) != 0;
  };
  table.bookmarks_count = [](void* value) -> uint32_t {
    leaf(0);
    assert(value == &bookmarks);
    probes.push_back(Bookmarks);
    return (flags & 2) ? 3 : 0;
  };
  table.clippings_count = []() -> uint32_t {
    leaf(0);
    probes.push_back(Clippings);
    return (flags & 4) ? 2 : 0;
  };
  table.input_facts = [](XpEpubInputFacts* out) {
    leaf(0);
    assert((flags & 8) && out);
    probes.push_back(InputFacts);
    *out = {};
    out->slide_owns_light = (flags & 16) != 0;
  };
  table.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    leaf(0);
    assert(out && !peer);
    probes.push_back(Radio);
    *out = {};
    out->compiled_in = (flags & 32) != 0;
  };
  table.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf(0);
    assert(!resolveFont && out);
    probes.push_back(Settings);
    *out = {};
    out->dictionary_available = (flags & 64) != 0;
  };
  return table;
}();
static void create() {
  assert(!owner && !renderDepth && !modelDepth);
  XpEpubTestOverlaySeed seed{};
  seed.native = &native;
  seed.context = &native;
#define UNEXPECTED(field) seed.field = &OverlayUnexpected<decltype(seed.field)>::call
  UNEXPECTED(home_reset);
  UNEXPECTED(current_toc);
  UNEXPECTED(build_more);
  UNEXPECTED(paint);
  UNEXPECTED(push);
  UNEXPECTED(a4);
  UNEXPECTED(xteink);
  UNEXPECTED(settle);
  UNEXPECTED(discard);
  UNEXPECTED(update);
#undef UNEXPECTED
  owner = xp_epub_test_overlay_create(&seed, &leaves, &runtime, &resources);
  assert(owner);
}
struct Output {
  std::array<uint8_t, 19> actions{};
  uint8_t count = 0;
  bool built = false;
};
static Output build(unsigned rejection = 0) {
  leaf(0);
  assert(owner);
  acquisitions = releases = 0;
  rejectAt = rejection;
  probes.clear();
  Output out;
  out.actions.fill(0xff);
  out.count = 0xff;
  out.built = xp_epub_test_overlay_build_more(owner, &bookmarks, out.actions.data(), &out.count) != 0;
  leaf(0);
  assert(acquisitions == releases && out.count <= out.actions.size());
  return out;
}
static void assertProbes() {
  std::vector<unsigned> expected{Footnotes, Bookmarks, Clippings, FrontlightPresent};
  if (flags & 8) expected.push_back(InputFacts);
  expected.push_back(Radio);
  expected.push_back(Settings);
  assert(probes == expected);
}
static void destroy() {
  leaf(0);
  xp_epub_test_overlay_destroy(owner);
  owner = nullptr;
}
}  // namespace candidate

static std::vector<uint8_t> reference() {
  SETTINGS.dictionaryName[0] = (flags & 64) ? 'x' : '\0';
  std::vector<EpubReaderMenuActivity::MenuItem> items;
  EpubReaderMenuActivity::buildMenuItems(items, flags & 1, flags & 2, flags & 4);
  std::vector<uint8_t> result;
  for (const auto& item : items) {
    using Action = EpubReaderMenuActivity::MenuAction;
    if (item.action != Action::SELECT_CHAPTER && item.action != Action::TEXT_SETTINGS)
      result.push_back(static_cast<uint8_t>(item.action));
  }
  return result;
}
static void assertActions(const candidate::Output& actual) {
  const auto expected = reference();
  assert(actual.built);
  assert(actual.count == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) assert(actual.actions[i] == expected[i]);
}
int main() {
  for (flags = 0; flags < 128; ++flags) {
    candidate::create();
    const auto actual = candidate::build();
    assertActions(actual);
    assert(candidate::acquisitions == 2 && candidate::releases == 2);
    candidate::assertProbes();
    candidate::destroy();
  }
  flags = 127;
  candidate::create();
  const auto previous = candidate::build();
  assertActions(previous);
  flags = 0;
  for (unsigned rejection : {1u, 2u}) {
    xp_epub_test_overlay_publish(candidate::owner);
    const auto rejected = candidate::build(rejection);
    assert(!rejected.built);
    assert(candidate::acquisitions == rejection && candidate::releases == rejection);
    assert(rejected.count == previous.count && rejected.actions == previous.actions);
    if (rejection == 1)
      assert(candidate::probes.empty());
    else
      candidate::assertProbes();
    XpEpubTestOverlayObservation observed{};
    xp_epub_test_overlay_observe(candidate::owner, &observed);
    assert(observed.failed);
  }
  xp_epub_test_overlay_publish(candidate::owner);
  const auto recovered = candidate::build();
  assertActions(recovered);
  candidate::assertProbes();
  assert(recovered.count != previous.count);
  candidate::destroy();
  std::puts("more actions: 128 native-order cases; two publication-gate interleavings; lazy input facts; recovery");
}
