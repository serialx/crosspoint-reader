#include <xp_ui_reader.h>

#include <cassert>
#include <cstdio>
#include <memory>
static unsigned lockDepth;
static void event(const char* name, int a = 0, int b = 0) { std::printf("%s %d %d lock=%u\n", name, a, b, lockDepth); }
struct RenderLock {
  RenderLock() {
    ++lockDepth;
    event("lock");
  }
  ~RenderLock() {
    event("unlock");
    assert(lockDepth);
    --lockDepth;
  }
};
struct Renderer {
  bool stores = false;
  void displayBuffer(int) { assert(false); }
  bool storeBwBuffer() {
    event("store", stores);
    return stores;
  }
  void restoreBwBuffer(bool resync) { event("restore", resync); }
};
struct ReaderToolbarUi {
  int selected = 0, top = 0;
  explicit ReaderToolbarUi(Renderer&) { event("create"); }
  ~ReaderToolbarUi() { event("drop"); }
  ReaderToolbarUi& nav() { return *this; }
  void reset(int index = 0) {
    selected = index;
    top = 0;
    event("reset", index);
  }
  void resetNavigation(int index = 0, int first = 0) {
    reset(index);
    top = first;
  }
  void begin() { event("begin"); }
};
struct HalDisplay {
  enum { FAST_REFRESH };
};
namespace BoardConfig {
bool isEegoA4() {
  assert(false);
  return false;
}
}  // namespace BoardConfig
bool xteinkClassPanel() {
  assert(false);
  return false;
}
struct EpubReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {}
  };
  int currentSpineIndex = 0, nextPageNumber = 0;
  struct Popup {
    void dismiss() { assert(false); }
  } overlayPopup;
  void discardOverlayPage() { assert(false); }
  void setRefreshCounter(int) { assert(false); }
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 2, panelIndex = 9;
  bool panelCursorShown = false, panelHoldJumped = true, overlayPageStored = false;
  bool section = true, pageBufferStale = false;
  Renderer renderer;
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  struct Input {
    bool touch = false;
    void resetHomeButtonInput() { event("home-reset"); }
    bool hasTouch() {
      event("touch", touch);
      return touch;
    }
  } mappedInput;
  int currentTocIndex() {
    event("toc-index");
    return 7;
  }
  void buildMoreActions() { event("more-actions"); }
  void settleOverlayRefresh() { event("settle"); }
  void renderOverlay() { event("render", int(overlay)); }
  void pushOverlayRefresh() { event("push"); }
  void requestUpdate() { event("update"); }
  void openOverlay(Overlay target);
};
// OPEN_METHODS
int main() {
  unsigned index = 0;
  for (int previous = 0; previous < 5; ++previous)
    for (int target = 0; target < 5; ++target)
      for (int flags = 0; flags < 64; ++flags) {
        EpubReaderActivity a;
        std::printf("case %u\n", index++);
        a.overlay = static_cast<EpubReaderActivity::Overlay>(previous);
        a.mappedInput.touch = flags & 1;
        a.section = flags & 2;
        a.pageBufferStale = flags & 4;
        a.overlayPageStored = flags & 8;
        a.renderer.stores = flags & 16;
        if (flags & 32) a.toolbarUi = std::make_unique<ReaderToolbarUi>(a.renderer);
        a.openOverlay(static_cast<EpubReaderActivity::Overlay>(target));
        std::printf("final %d %d %d %d %d %d %d %d\n", int(a.overlay), a.focusedTool, a.panelIndex, a.panelCursorShown,
                    a.panelHoldJumped, a.overlayPageStored, a.toolbarUi->selected, a.toolbarUi->top);
        assert(!lockDepth);
      }
}
