#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <set>
#include <vector>

#include "helper_session_fixture.h"

using namespace std::chrono_literals;
static std::mutex renderMutex, modelMutex;
static thread_local unsigned renderDepth, modelDepth;
static std::set<void*> chunks;
static unsigned allocations, releases, updates;
static std::promise<void> checked, resume, contender;
static std::shared_future<void> resumeFuture;
static std::atomic<bool> pauseDiscard;

static void physical() { assert(renderDepth == 1 && modelDepth == 0); }
static void* chunkAllocate(size_t bytes) {
  physical();
  auto* chunk = std::malloc(bytes);
  assert(chunk && chunks.insert(chunk).second);
  ++allocations;
  return chunk;
}
static void chunkRelease(void* chunk) {
  physical();
  assert(chunks.erase(chunk) == 1 && "each chunk has one serialized owner");
  ++releases;
  std::free(chunk);
}
struct GfxRenderer {
  static constexpr size_t BW_BUFFER_CHUNK_SIZE = 8000;
  std::array<uint8_t, 48000> pixels{};
  uint8_t* frameBuffer = pixels.data();
  size_t frameBufferSize = pixels.size();
  std::vector<uint8_t*> bwBufferChunks = std::vector<uint8_t*>(6);
  struct Display {
    void cleanupGrayscaleBuffers(uint8_t*) { physical(); }
  } display;
  void freeBwBufferChunks();
  bool storeBwBuffer();
  void restoreBwBuffer(bool resyncPanelBaseline);
  void discardStoredBwBuffer() { freeBwBufferChunks(); }
} renderer;

#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define malloc chunkAllocate
#define free chunkRelease
// ACTUAL_BACKUP_METHODS
#undef malloc
#undef free

static void* lockRender() {
  assert(!modelDepth && !renderDepth && "non-recursive render mutex");
  renderMutex.lock();
  ++renderDepth;
  return &renderMutex;
}
static void unlockRender(void* token) {
  physical();
  assert(token == &renderMutex);
  --renderDepth;
  renderMutex.unlock();
}
static const XpAppRuntime runtime = [] {
  XpAppRuntime value{};
  value.render_lock = lockRender;
  value.render_unlock = unlockRender;
  value.model_lock = [] {
    assert(!modelDepth);
    modelMutex.lock();
    ++modelDepth;
  };
  value.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
  };
  value.notify_render = [] {
    assert(!renderDepth && !modelDepth);
    ++updates;
  };
  return value;
}();
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves value{};
  value.settings_save = [] { assert(!renderDepth && !modelDepth); };
  value.fonts_ensure_loaded = [] { physical(); };
  value.render_discard_bw = [] {
    physical();
    if (pauseDiscard.exchange(false)) {
      checked.set_value();
      resumeFuture.wait();
    }
    renderer.discardStoredBwBuffer();
  };
  return value;
}();
static const XpAppResources resources{};

static void run(bool callerHeld) {
  assert(chunks.empty());
  checked = std::promise<void>();
  resume = std::promise<void>();
  contender = std::promise<void>();
  resumeFuture = resume.get_future().share();
  allocations = releases = updates = 0;
  auto* token = lockRender();
  std::fill(renderer.pixels.begin(), renderer.pixels.end(), 0x5a);
  assert(renderer.storeBwBuffer());
  unlockRender(token);
  XpEpubTestHelpersSeed seed{&renderer, nullptr, 7, 1, 0};
  void* owner = xp_epub_test_helpers_create_legacy_render(&seed, &leaves, &runtime, &resources);
  assert(owner);
  pauseDiscard = true;
  auto disposal = std::async(std::launch::async, [=] {
    if (callerHeld) {
      auto* held = lockRender();
      xp_epub_test_helpers_discard(owner);
      unlockRender(held);
    } else {
      xp_epub_test_helpers_apply_text_live(owner);
    }
  });
  // The physical discard leaf is reached only after the owner's saved-page check.
  assert(checked.get_future().wait_for(5s) == std::future_status::ready);
  auto paint = std::async(std::launch::async, [=] {
    contender.set_value();
    auto* held = lockRender();
    xp_epub_test_helpers_discard(owner);
    assert(chunks.empty());
    assert(renderer.storeBwBuffer());
    std::fill(renderer.pixels.begin(), renderer.pixels.end(), 0);
    renderer.restoreBwBuffer(false);
    assert(std::all_of(renderer.pixels.begin(), renderer.pixels.end(), [](uint8_t p) { return p == 0x5a; }));
    unlockRender(held);
  });
  assert(contender.get_future().wait_for(5s) == std::future_status::ready);
  assert(!renderMutex.try_lock() && "disposal must retain the render mutex");
  assert(paint.wait_for(50ms) == std::future_status::timeout);
  resume.set_value();
  disposal.get();
  paint.get();
  XpEpubTestHelpersObservation observation{};
  xp_epub_test_helpers_observe(owner, &observation);
  assert(!observation.stored && observation.refresh == 7);
  assert(chunks.empty() && allocations == 12 && releases == 12);
  assert(updates == unsigned(!callerHeld));
  xp_epub_test_helpers_destroy(owner);
}

int main() {
  run(true);
  run(false);
  std::puts("backup: held and loop entry serialize real discard/store/restore; 24 chunks released exactly once");
}
