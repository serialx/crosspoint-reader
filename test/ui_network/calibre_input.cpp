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
struct CrossPointWebServer {
  struct WsUploadStatus {
    size_t received = 0, total = 0;
    uint32_t lastCompleteAt = 0;
    bool inProgress = false;
    std::string filename, lastCompleteName;
  };
  bool isRunning() { return false; }
  WsUploadStatus getWsUploadStatus() { return {}; }
};
// PRODUCTION_CLASS
std::atomic<bool> painting{false}, finishPaint{false};
bool buildOkay = true;
int finished = 0;
uint32_t CalibreConnectActivity::service(void*, uint32_t operation, uint32_t) {
  if (operation == XP_UI_CALIBRE_FINISH) ++finished;
  return 0;
}
const char* CalibreConnectActivity::text(uint32_t) { return nullptr; }
bool CalibreConnectActivity::buildRustScene(XpUiScene& scene) {
  ModelLock lock(*this);
  renderingState = model;
  scene.refresh = XP_UI_FAST;
  return buildOkay;
}
XpUiPaint CalibreConnectActivity::rustResources() {
  return {this, nullptr, nullptr, [](void*, uint32_t, uint32_t) -> uint8_t { return 1; },
          [](void* context, const XpGlyphTarget*, const XpUiCommand*, XpUiRect) {
            auto& self = *static_cast<CalibreConnectActivity*>(context);
            assert(resourceDepth == 1 && modelDepth == 0);
            const auto before = self.currentUploadName;
            painting = true;
            while (!finishPaint) std::this_thread::yield();
            assert(self.currentUploadName == before);
          }};
}
// PRODUCTION_METHODS
int main() {
  for (int iteration = 0; iteration < 60; ++iteration) {
    GfxRenderer renderer;
    MappedInputManager input;
    CalibreConnectActivity activity(renderer, input);
    CalibreConnectActivity::CallContext context{activity, {}};
    context.upload.emplace();
    context.upload->filename = std::string(260, 'a');
    CalibreConnectActivity::publish(&context, &activity.model, XP_UI_CALIBRE_COPY_UPLOAD);
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
      context.upload->filename = std::string(260, 'b');
      CalibreConnectActivity::publish(&context, &activity.model, XP_UI_CALIBRE_COPY_UPLOAD);
    });
    while (!started) std::this_thread::yield();
    assert(mutate.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout);
    finishPaint = true;
    while (!renderer.refreshing) std::this_thread::yield();
    assert(mutate.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    mutate.get();
    assert(activity.currentUploadName == std::string(260, 'b'));
    auto cancel = std::async(std::launch::async, [&] {
      input.pressed = 1u << unsigned(MappedInputManager::Button::Back);
      activity.loop();
    });
    assert(cancel.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready);
    cancel.get();
    renderer.finishRefresh = true;
    draw.get();
  }
  assert(finished == 60);
  // Calibre exposes only cancellation; it stays available after either failure.
  for (bool buildFailure : {false, true}) {
    GfxRenderer renderer;
    renderer.finishRefresh = true;
    renderer.acceptScene = buildFailure;
    MappedInputManager input;
    CalibreConnectActivity activity(renderer, input);
    buildOkay = !buildFailure;
    {
      RenderLock lock;
      activity.render(std::move(lock));
    }
    input.pressed = 1u << unsigned(MappedInputManager::Button::Back);
    activity.loop();
  }
  assert(finished == 62);
}
