#include <xp_ui_network.h>

#include <cassert>
#include <future>
#include <memory>
#include <optional>

#include "RustActivityAdapter.h"
inline thread_local int resourceDepth = 0;
using StaticSemaphore_t = std::mutex;
using SemaphoreHandle_t = std::mutex*;
constexpr int portMAX_DELAY = 0;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) { return storage; }
void xSemaphoreTake(SemaphoreHandle_t mutex, int) {
  assert(modelDepth == 0);
  mutex->lock();
  ++resourceDepth;
}
void xSemaphoreGive(SemaphoreHandle_t mutex) {
  --resourceDepth;
  mutex->unlock();
}
enum class NetworkMode { Join = 0 };
struct CrossPointWebServer {
  bool isRunning() { return false; }
};
// PRODUCTION_CLASS
std::atomic<bool> painting{false}, finishPaint{false};
bool buildOkay = true;
int finished = 0;
struct CrossPointWebServerActivity::CallContext {
  CrossPointWebServerActivity& activity;
  int unused;
};
uint32_t CrossPointWebServerActivity::service(void* context, uint32_t operation, uint32_t) {
  auto& self = static_cast<CallContext*>(context)->activity;
  assert(modelDepth == 0);
  if (operation == XP_UI_WEB_HOME) ++finished;
  if (operation == XP_UI_WEB_EXIT_INPUT) return self.mappedInput.wasReleased(MappedInputManager::Button::Back);
  return 0;
}
bool CrossPointWebServerActivity::buildRustScene(XpUiScene& scene) {
  ModelLock lock(*this);
  renderingState = model;
  scene.refresh = XP_UI_FAST;
  return buildOkay;
}
XpUiPaint CrossPointWebServerActivity::rustResources() {
  return {this, nullptr, nullptr, [](void*, uint32_t, uint32_t) -> uint8_t { return 1; },
          [](void* context, const XpGlyphTarget*, const XpUiCommand*, XpUiRect) {
            auto& self = *static_cast<CrossPointWebServerActivity*>(context);
            assert(resourceDepth == 1 && modelDepth == 0);
            const auto before = self.connectedSSID;
            painting = true;
            while (!finishPaint) std::this_thread::yield();
            assert(self.connectedSSID == before);
          }};
}
// PRODUCTION_METHODS
int main() {
  for (int iteration = 0; iteration < 60; ++iteration) {
    GfxRenderer renderer;
    MappedInputManager input;
    CrossPointWebServerActivity activity(renderer, input);
    activity.model.phase = XP_UI_WEB_RUNNING;
    activity.model.is_ap = 1;
    activity.connectedSSID = std::string(32, 'a');
    painting = false;
    finishPaint = false;
    renderer.finishRefresh = false;
    auto draw = std::async(std::launch::async, [&] {
      RenderLock lock;
      activity.render(std::move(lock));
    });
    while (!painting) std::this_thread::yield();
    std::atomic<bool> started{false};
    auto mutate = std::async(std::launch::async, [&] {
      started = true;
      CrossPointWebServerActivity::ResourceLock lock(activity);
      activity.connectedSSID = std::string(32, 'b');
    });
    while (!started) std::this_thread::yield();
    assert(mutate.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout);
    finishPaint = true;
    while (!renderer.refreshing) std::this_thread::yield();
    assert(mutate.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    mutate.get();
    assert(activity.connectedSSID == std::string(32, 'b'));
    auto cancel = std::async(std::launch::async, [&] {
      input.released = 1u << unsigned(MappedInputManager::Button::Back);
      activity.loop();
    });
    assert(cancel.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    cancel.get();
    renderer.finishRefresh = true;
    draw.get();
  }
  assert(finished == 60);
  // Web exposes only cancellation; it stays available after either failure.
  for (bool buildFailure : {false, true}) {
    GfxRenderer renderer;
    renderer.finishRefresh = true;
    renderer.acceptScene = buildFailure;
    MappedInputManager input;
    CrossPointWebServerActivity activity(renderer, input);
    activity.model.phase = XP_UI_WEB_RUNNING;
    activity.model.is_ap = 1;
    buildOkay = !buildFailure;
    {
      RenderLock lock;
      activity.render(std::move(lock));
    }
    input.released = 1u << unsigned(MappedInputManager::Button::Back);
    activity.loop();
  }
  assert(finished == 62);
}
