#include <xp_ui_home.h>

#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>

constexpr int GO_HOME_MS = 1000;
enum class StrId { STR_OPEN, STR_DELETE, STR_RENAME, STR_FILENAME };
struct {
  const char* get(StrId) { return "option"; }
} I18N;
struct MappedInputManager {
  enum class Button { Confirm };
  uint16_t pressed = 0, released = 0;
  bool longPress = false, longTouch = false, touch = false;
  bool hasTouch() const { return touch; }
  bool wasLongPressed(Button, int) { return longPress; }
  bool wasScreenLongPress(int& x, int& y) {
    x = y = 5;
    return longTouch;
  }
  void wasSwipe(int* x, int* y) { *x = *y = 5; }
};
class OptionPopup {
 public:
  static constexpr int MAX_OPTIONS = 16;
  bool active = false, uiReady = false;
  std::string title, headline, message;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
  bool isActive() const { return active; }
  bool handleInput(MappedInputManager& i, const std::function<void()>& redraw) {
    return handleButtons(false, false, i.released & XP_UI_CONFIRM, i.released & XP_UI_BACK, redraw);
  }
  // POPUP_METHODS
};
class RustActivityAdapter {
 public:
  MappedInputManager mappedInput;
  struct ModelLock {
    explicit ModelLock(RustActivityAdapter&) {}
  };
  virtual XpUiEffect updateRust(const XpUiInput&) = 0;
  virtual XpUiInterest rustInterest(uint32_t) const = 0;
  virtual void applyRustEffect(const XpUiEffect&) = 0;
  void loop() {
    XpUiInput i{};
    i.pressed = mappedInput.pressed;
    i.released = mappedInput.released;
    i.now_ms = 2000;
    if (xp_ui_input_due(&rustInterestValue(), &i)) applyRustEffect(updateRust(i));
  }
  XpUiInterest interest{};
  const XpUiInterest& rustInterestValue() {
    interest = rustInterest(2000);
    return interest;
  }
  void requestUpdate() {}
};
class FileBrowserActivity : public RustActivityAdapter {
 public:
  enum class Mode { Books, Firmware };
  Mode mode = Mode::Books;
  XpUiFileState state{};
  XpUiHomeListHits publishedHits{};
  struct {
    bool ready = true;
  } publication;
  bool nativeLongPress = false, nativeLongTouch = false;
  int nativeTouchX = 0, nativeTouchY = 0;
  uint8_t touchFeedback = 0;
  std::string basepath = "/";
  std::vector<std::string> files{"first.epub", "second.epub", "folder/"};
  OptionPopup optionPopup;
  std::vector<XpUiEffect> effects;
  FileBrowserActivity() {
    xp_ui_file_init(&state, 0);
    state.list.selected = 1;
  }
  int listCount() const { return files.size(); }
  XpUiFileFacts facts(int selected) const;
  void activateSelected();
  void showEntryActions();
  void loop();
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void deleteSelected() { assert(false); }
  void startRename() { assert(false); }
  void applyRustEffect(const XpUiEffect& effect) override {
    if (effect.kind == XP_UI_FILE_OPTIONS) showEntryActions();
    if (effect.kind) effects.push_back(effect);
  }
};
