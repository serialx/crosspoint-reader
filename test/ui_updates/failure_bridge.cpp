#include <xp_ui_updates.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
namespace HalDisplay {
enum { FAST_REFRESH, HALF_REFRESH, FULL_REFRESH };
}
namespace haptic_feedback {
void touchAction(bool) {}
}  // namespace haptic_feedback
static bool failAllocation = false;
template <class T>
auto makeUniqueNoThrow(size_t count) {
  if (failAllocation) return std::unique_ptr<T>{};
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]);
}
struct Renderer {
  bool reject = false;
  bool drawUiScene(const XpUiScene&, const XpUiPaint*) { return !reject; }
  void displayBuffer(int) {}
};
class RustActivityAdapter {
 public:
  struct RenderLock {};
  struct ModelLock {
    explicit ModelLock(RustActivityAdapter&) {}
  };
  struct Publication {
    XpUiScene scene{};
    bool ready = false;
  };
  std::string name = "test";
  Renderer renderer;
  virtual ~RustActivityAdapter() = default;
  virtual Publication* rustPublication() = 0;
  virtual bool buildRustScene(XpUiScene&) = 0;
  virtual void rustPublished() = 0;
  virtual void rustSceneFailed() = 0;
  XpUiPaint rustResources() { return {}; }
  uint32_t suppressButtons = 0, lastHeld = 0;
  bool sceneFailed = false, suppressContact = false, lastContact = false;
  bool paintRustScene(const XpUiScene&, bool publish = true);
  void rejectRustScene();
  void publishRustScene(const XpUiScene&);
  void render(RenderLock&&);
  bool rustSceneCurrent() const { return true; }
  void requestUpdate() {}
};
class UpdateUiActivity : public RustActivityAdapter {
 public:
  Publication publication;
  XpUiUpdateState model{}, painting{};
  XpUiUpdateFeedback feedback{}, next{};
  XpUiUpdateEffect pending{};
  bool pendingHaptic = false, routesReady = false;
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t capacity = 0;
  int inputRowCount = 3, inputFamilyCount = 3;
  unsigned actions = 0, finishes = 0;
  struct ResourceLock {
    ResourceLock(UpdateUiActivity&) {}
    ~ResourceLock() {}
  };
  void releaseText() {}
  void render(RenderLock&&);
  Publication* rustPublication() override { return &publication; }
  bool reserve(uint16_t);
  bool buildRustScene(XpUiScene& scene) override {
    painting = model;
    if (!reserve(48)) return false;
    scene = {};
    scene.refresh = XP_UI_FAST;
    return true;
  }
  void rustPublished() override;
  void rustSceneFailed() override;
  XpUiEffect updateRust(const XpUiInput&);
  void applyRustEffect(const XpUiEffect&);
  void requestUpdate(bool) {}
  void performAction(XpUiUpdateAction action, int32_t) {
    if (action == XP_UI_UPDATE_FINISH)
      ++finishes;
    else
      ++actions;
  }
  void input(uint32_t pressed, uint32_t released) {
    XpUiInput input{};
    input.pressed = pressed;
    input.released = released;
    if (sceneFailed) {
      input.pressed &= XP_UI_BACK;
      input.released &= XP_UI_BACK;
    }
    updateRust(input);
    applyRustEffect({});
  }
};
// METHODS
int main() {
  for (bool published : {false, true})
    for (bool allocation : {false, true}) {
      UpdateUiActivity a;
      xp_ui_updates_init(&a.model, XP_UI_UPDATE_CACHE, 0);
      a.model.popup_selected = 1;
      if (published) a.render({});
      // Force the real fallible-growth branch while preserving the old arena.
      assert(a.reserve(20));
      auto* original = a.commands.get();
      a.capacity = 20;
      a.renderer.reject = !allocation;
      failAllocation = allocation;
      a.model.list.active = 2;
      a.model.list.pending_scroll = 3;
      a.model.touch_active = 2;
      a.render({});
      assert(!a.publication.ready);
      assert(a.sceneFailed);
      assert(a.model.list.active == -1 && a.model.list.pending_scroll == 0 && a.model.touch_active == 0);
      if (allocation) assert(a.commands.get() == original && a.capacity == 20);
      a.input(2, 2);
      assert(a.actions == 0);
      a.input(4, 4);
      assert(a.model.popup_selected == 1);
      a.input(1, 1);
      assert(a.finishes == 1);
      a.model.popup_active = 1;
      a.renderer.reject = false;
      failAllocation = false;
      a.render({});
      assert(a.publication.ready && !a.sceneFailed && a.capacity == 48);
      a.input(0, 2);
      assert(a.actions == 1);
    }
  UpdateUiActivity before;
  xp_ui_updates_init(&before.model, XP_UI_UPDATE_CACHE, 0);
  before.model.popup_selected = 1;
  before.input(0, 2);
  assert(before.actions == 1);
  UpdateUiActivity font;
  xp_ui_updates_init(&font.model, XP_UI_UPDATE_FONTS, 0);
  font.model.phase = 3;
  font.model.has_groups = 1;
  font.publication.ready = font.routesReady = true;
  font.feedback.rows.first_row = {0, 0, 100, 20};
  font.feedback.rows.count = font.feedback.rows.uniform = 1;
  font.input(0, XP_UI_BACK);
  assert(font.model.phase == 2 && !font.routesReady && !font.publication.ready);
  XpUiInput tap{};
  tap.touch_flags = XP_UI_TOUCH_RELEASED;
  tap.touch_x = tap.touch_y = 5;
  font.updateRust(tap);
  assert(font.pending.action == XP_UI_UPDATE_NONE);
  font.painting = font.model;
  font.next.rows = font.feedback.rows;
  font.rustPublished();
  font.publication.ready = true;
  font.updateRust(tap);
  assert(font.pending.action == XP_UI_UPDATE_ENTER_GROUP);
  puts(
      "build growth and paint rejection preserve Back, gate activation, retain the arena, and recover before/after "
      "first paint");
}
