#include <xp_ui_keyboard.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

static bool failAllocation = false;
static bool locked = false;
static unsigned allocations = 0, errors = 0;
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow(size_t n) {
  assert(!locked);
  ++allocations;
  return failAllocation ? nullptr : std::make_unique<T>(n);
}
#define LOG_ERR(...) (++errors)
namespace haptic_feedback {
void touchAction(bool) {}
}  // namespace haptic_feedback
struct KeyboardResult {
  std::string text;
};
struct ActivityResult {
  bool isCancelled = false;
};
struct RustActivityAdapter {
  unsigned baseLoops = 0;
  void loop() { ++baseLoops; }
};
class KeyboardEntryActivity : public RustActivityAdapter {
 public:
  struct Bytes {
    std::unique_ptr<uint8_t[]> data;
    uint32_t capacity = 0;
    bool grow(uint32_t, uint32_t);
  };
  struct ModelLock {
    explicit ModelLock(KeyboardEntryActivity&) {
      assert(!locked);
      locked = true;
    }
    ~ModelLock() { locked = false; }
  };
#include "keyboard-arena.inc"
  std::unique_ptr<Arena> arena;
  struct Publication {
    XpUiScene scene{};
    bool ready = false;
  } publication;
  XpUiKeyboardState publishedState{};
  XpUiKeyboardViewLayout publishedLayout{};
  void rustPublished();
  XpUiKeyboardViewLayout layout{};
  bool buildRustScene(XpUiScene&);
  static XpUiKeyboardMetrics metrics() {
    return {nullptr,
            [](void*, int32_t, const uint8_t*, uint32_t n, uint8_t mode) -> int32_t { return mode == 2 ? 0 : n * 8; },
            [](void*, int32_t) -> int32_t { return 20; }};
  }
  struct Input {
    int x = 0, y = 0;
    bool wasScreenTapped(int& tx, int& ty) const {
      tx = x;
      ty = y;
      return true;
    }
  } mappedInput;
  void loop();
  Bytes text;
  XpUiKeyboardState state{}, beforeInput{};
  XpUiKeyboardEffect pending{};
  XpUiInput lastInput{};
  uint32_t tappedCursor = UINT32_MAX;
  bool finished = false;
  unsigned updates = 0;
  std::string result;
  void requestUpdate() { ++updates; }
  void finish() { finished = true; }
  void setResult(KeyboardResult value) { result = std::move(value.text); }
  void setResult(ActivityResult) { result = "cancel"; }
  static XpUiKeyboardLayouts layouts() {
    return {nullptr,
            [](void*, uint8_t id, uint8_t, uint8_t, uint8_t, uint8_t row) -> XpUiKeyboardRow {
              return {static_cast<uint8_t>(row == 0 ? (id == 1 ? 2 : 1) : 0), 0, 0, 0};
            },
            [](void*, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t col) -> XpUiKeyboardKey {
              return {"é", "é", "É", static_cast<int16_t>(1000 + col), 0, 2};
            }};
  }
  XpUiEffect updateRust(const XpUiInput&);
  void applyRustEffect(const XpUiEffect&);
  void step(const XpUiInput& input) {
    XpUiEffect effect;
    {
      ModelLock guard(*this);
      effect = updateRust(input);
    }
    applyRustEffect(effect);
  }
};
#include "keyboard-allocation.inc"
int main() {
  KeyboardEntryActivity activity;
  assert(activity.text.grow(2, 0));
  memcpy(activity.text.data.get(), "a", 2);
  xp_ui_keyboard_init(&activity.state, 1, 0, 0, 1, 0);
  XpUiInput press{}, release{};
  press.pressed = XP_UI_CONFIRM;
  release.released = XP_UI_CONFIRM;
  activity.step(press);
  auto* prior = activity.text.data.get();
  failAllocation = true;
  activity.step(release);
  assert(errors == 1 && !activity.finished);
  assert(activity.text.data.get() == prior && activity.text.capacity == 2);
  assert(activity.state.length == 1 && activity.state.cursor == 1);
  assert(!memcmp(activity.text.data.get(), "a", 2));
  failAllocation = false;
  activity.step(press);
  activity.step(release);
  assert(!activity.finished && errors == 1);
  assert(activity.state.length == 3 && activity.state.cursor == 3);
  assert(!memcmp(activity.text.data.get(), "aé", 4));
  const auto count = allocations;
  XpUiInput idle{};
  for (unsigned i = 0; i < 100; ++i) activity.step(idle);
  assert(allocations == count);
  activity.step(press);
  XpUiInput held{};
  held.held = XP_UI_CONFIRM;
  held.held_ms = 501;
  failAllocation = true;
  activity.step(held);
  assert(errors == 2 && activity.state.confirm_long && activity.state.length == 3);
  const auto failedCount = allocations;
  for (unsigned i = 0; i < 20; ++i) activity.step(held);
  activity.step(release);
  assert(errors == 2 && allocations == failedCount && activity.state.length == 3);
  failAllocation = false;
  activity.arena = std::make_unique<KeyboardEntryActivity::Arena>();
  auto& arena = *activity.arena;
  arena.commands = makeUniqueNoThrow<XpUiCommand[]>(256);
  arena.commandCapacity = 256;
  arena.spans = makeUniqueNoThrow<XpUiKeyboardTextSpan[]>(64);
  arena.spanCapacity = 64;
  assert(arena.raw.grow(4096, 0) && arena.display.grow(4096, 0) && arena.sceneText.grow(4096, 0));
  auto& l = activity.layout;
  l.screen = {0, 0, 480, 800};
  l.content = l.screen;
  l.font = 1;
  l.small_font = 0;
  l.spacing = 8;
  l.header_height = 40;
  l.key_height = 56;
  l.hints_height = 40;
  l.key_spacing = 6;
  l.text_percent = 80;
  l.field_normal = 2;
  l.field_cursor = 3;
  l.field_end = -1;
  XpUiScene scene{};
  scene.generation = 0x12345678;
  const auto oldScene = scene;
  failAllocation = true;
  assert(!activity.buildRustScene(scene));
  assert(!memcmp(&scene, &oldScene, sizeof(scene)) && activity.state.length == 3);
  assert(!arena.paintText.data);
  failAllocation = false;
  assert(activity.buildRustScene(scene));
  assert(arena.paintText.capacity >= 4);
  const auto warmed = allocations;
  for (unsigned i = 0; i < 100; ++i) assert(activity.buildRustScene(scene));
  assert(warmed == allocations);
  activity.rustPublished();
  activity.publication.ready = true;
  const auto tables = activity.layouts();
  XpUiKeyboardPlacedKey key[16];
  assert(xp_ui_keyboard_row(&activity.publishedState, &tables, &activity.publishedLayout, 0, key, 16) == 1);
  XpUiInput tap{};
  tap.touch_flags = XP_UI_TOUCH_RELEASED;
  tap.touch_x = key[0].visual.x + key[0].visual.w * 3 / 4;
  tap.touch_y = key[0].visual.y + 5;
  activity.state.layout = 1;
  activity.step(tap);
  assert(activity.lastInput.value == 1000);
  arena.snapshot = activity.state;
  activity.rustPublished();
  activity.step(tap);
  assert(activity.lastInput.value == 1001);
  activity.state.input_type = 1;
  activity.mappedInput.x = tap.touch_x;
  activity.mappedInput.y = tap.touch_y;
  const auto beforeKeyTap = allocations;
  failAllocation = true;
  activity.loop();
  failAllocation = false;
  assert(allocations == beforeKeyTap && activity.baseLoops == 1 && errors == 2);
  puts(
      "Production allocator/retry: failed growth preserves bytes and cursor, logs once, remains active; retry succeeds "
      "outside model lock");
}
