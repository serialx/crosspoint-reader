#include <xp_ui_reader.h>

#include <cassert>
#include <future>

#include "RustActivityAdapter.h"

static std::atomic<bool> finishing = false, proceed = false, checking = false, attempted = false;
static std::atomic<bool> crossing = false, published = false, writerDone = false;
static thread_local bool renderThread = false;
void afterModelUnlock() {
  if (renderThread && criticalDepth == 0 && crossing && checking && !published) {
    while (!writerDone) std::this_thread::yield();
  }
}
class ReaderActivity : public RustActivityAdapter {
 public:
  using RustActivityAdapter::RustActivityAdapter;
  void render(RenderLock&&) override {
    assert(criticalDepth == 0);
    finishing = true;
    while (!proceed) std::this_thread::yield();
  }
};
class EpubReaderActivity : public ReaderActivity {
 public:
  enum class Overlay { None, Toolbar, Text };
  struct ObservedOverlay {
    Overlay value = Overlay::Toolbar;
    bool crossing = false;
    bool operator==(Overlay other) const {
      assert(criticalDepth > 0);  // The production completion must synchronize the read.
      if (crossing) {
        checking = true;
        while (!attempted) std::this_thread::yield();
      }
      return value == other;
    }
  } overlay;
  unsigned publications = 0;
  using ReaderActivity::ReaderActivity;
  bool runRecoveryPolicy(uint8_t) { return false; }
  void render(RenderLock&&) override;
  void fail() { rejectRustScene(); }
  bool rejected() const { return rustSceneRejected(); }
  void setOverlay(Overlay value) {
    ModelLock lock(*this);
    overlay.value = value;
  }
  void rustPublished() override {
    assert(criticalDepth > 0 && overlay.value == Overlay::None);
    ++publications;
    published = true;
  }
  void changeDuringCompletion() {
    while (!checking) std::this_thread::yield();
    const bool acquired = modelMutex.try_lock();
    assert(!acquired);  // Read and publication must exclude the transition.
    attempted = true;
    setOverlay(Overlay::Text);
    writerDone = true;
  }
};
// PARENT_RENDER
int main() {
  for (auto target :
       {EpubReaderActivity::Overlay::None, EpubReaderActivity::Overlay::Toolbar, EpubReaderActivity::Overlay::Text}) {
    GfxRenderer renderer;
    MappedInputManager input;
    EpubReaderActivity reader("reader", renderer, input);
    reader.fail();
    finishing = false;
    proceed = false;
    auto render = std::async(std::launch::async, [&] {
      RenderLock lock;
      reader.render(std::move(lock));
    });
    while (!finishing) std::this_thread::yield();
    reader.setOverlay(target);  // Back closes, or input opens/changes the overlay, before completion.
    proceed = true;
    render.get();
    assert(reader.rejected() == (target != EpubReaderActivity::Overlay::None));
    assert(reader.publications == (target == EpubReaderActivity::Overlay::None ? 1u : 0u));
  }
  GfxRenderer renderer;
  MappedInputManager input;
  EpubReaderActivity reader("reader", renderer, input);
  reader.fail();
  reader.setOverlay(EpubReaderActivity::Overlay::None);
  reader.overlay.crossing = true;
  crossing = true;
  published = false;
  writerDone = false;
  renderThread = true;
  finishing = false;
  proceed = true;
  checking = false;
  attempted = false;
  auto writer = std::async(std::launch::async, [&] { reader.changeDuringCompletion(); });
  {
    RenderLock lock;
    reader.render(std::move(lock));
  }
  writer.get();
  assert(reader.publications == 1 && !reader.rejected());
  assert(reader.overlay.value == EpubReaderActivity::Overlay::Text);
}
