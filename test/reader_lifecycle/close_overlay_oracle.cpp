#include <xp_ui_reader.h>

#include <cassert>
#include <cstdio>
#include <memory>
#include <utility>
static bool isA4, isXteink;
static int32_t* counter;
static void event(const char* name) { std::printf("%s counter=%d\n", name, static_cast<int>(*counter)); }
namespace BoardConfig {
bool isEegoA4() {
  event("is-a4");
  return isA4;
}
}  // namespace BoardConfig
bool xteinkClassPanel() {
  event("panel-class");
  return isXteink;
}
struct RenderLock {
  RenderLock() { event("lock"); }
  ~RenderLock() { event("unlock"); }
};
struct HalDisplay {
  enum { FAST_REFRESH };
};
struct EpubReaderActivity {
  XpUiReaderBaseModel baseModel{};
  int32_t& pagesUntilFullRefresh = baseModel.pages_until_full_refresh;
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {}
  };
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::Text;
  struct Toolbar {
    ~Toolbar() { event("toolbar-drop"); }
  };
  std::unique_ptr<Toolbar> toolbarUi;
  struct Input {
    void resetHomeButtonInput() { event("home-reset"); }
  } mappedInput;
  struct Popup {
    void dismiss() { event("popup-dismiss"); }
  } overlayPopup;
  struct Renderer {
    void restoreBwBuffer(bool resync) {
      assert(!resync);
      event("restore-no-resync");
    }
    void displayBuffer(int mode) {
      assert(mode == HalDisplay::FAST_REFRESH);
      event("display-fast");
    }
  } renderer;
  bool overlayPageStored = false;
  void settleOverlayRefresh() { event("settle"); }
  void discardOverlayPage() {
    event("discard");
    overlayPageStored = false;
  }
  void requestUpdate() { event("update"); }
  void setRefreshCounter(int32_t value) {
    ModelLock lock(*this);
    xp_ui_reader_base_model_write(&baseModel, XP_UI_READER_BASE_FIELD_COUNTER, static_cast<uint32_t>(value));
  }
  void closeOverlayToPage();
};
// CLOSE_OVERLAY_METHOD
int main() {
  unsigned caseId = 0;
  for (bool a4 : {false, true})
    for (bool xteink : {false, true})
      for (bool stored : {false, true})
        for (bool toolbar : {false, true})
          for (int initial : {0, 1, 15}) {
            EpubReaderActivity r;
            xp_ui_reader_base_init(&r.baseModel, 0, 0);
            r.baseModel.pages_until_full_refresh = initial;
            counter = &r.baseModel.pages_until_full_refresh;
            isA4 = a4;
            isXteink = xteink;
            r.overlayPageStored = stored;
            if (toolbar) r.toolbarUi = std::make_unique<EpubReaderActivity::Toolbar>();
            std::printf("case=%u a4=%d panel=%d stored=%d toolbar=%d initial=%d\n", caseId++, a4, xteink, stored,
                        toolbar, initial);
            r.closeOverlayToPage();
            std::printf("final overlay=%d stored=%d toolbar=%d counter=%d\n", int(r.overlay), r.overlayPageStored,
                        bool(r.toolbarUi), static_cast<int>(*counter));
            assert(!xp_ui_reader_base_model_read(&r.baseModel, XP_UI_READER_BASE_FIELD_FORCED));
          }
}
