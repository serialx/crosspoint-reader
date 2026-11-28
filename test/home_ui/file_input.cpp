#include <FreeInkApp.h>

#include "ButtonNavigator.h"
namespace fui = freeink::ui;
constexpr unsigned long GO_HOME_MS = 1000;
struct RenderLock {
  template <class T>
  explicit RenderLock(T&) {}
  void unlock() {}
};
struct FilePathResult {
  std::string path;
};
struct ActivityResult {
  bool isCancelled = false;
  std::string path;
  ActivityResult() = default;
  explicit ActivityResult(FilePathResult value) : path(std::move(value.path)) {}
};
struct OptionPopup {
  template <class T>
  bool handleInput(const MappedInputManager&, T) {
    return false;
  }
};
struct App {
  void clearTapFlash() {}
};
class FileBrowserActivity {
 public:
  enum class Mode { Books, PickFirmware };
  Mode mode = Mode::Books;
  MappedInputManager mappedInput;
  ButtonNavigator buttonNavigator;
  fui::ListNav nav;
  std::string basepath = "/", effect;
  std::vector<std::string> files;
  OptionPopup optionPopup;
  App app;
  int updates = 0;
  int listCount() const { return static_cast<int>(files.size()); }
  fui::ListNav& activeNav() { return nav; }
  void requestUpdate() { ++updates; }
  void onGoHome() { effect = "home"; }
  void onSelectBook(const std::string& path) { effect = "open:" + path; }
  void setResult(ActivityResult result) { effect = result.isCancelled ? "cancel" : "result:" + result.path; }
  void finish() {}
  void showEntryActions() { effect = "options"; }
  void activateSelected();
  bool handleCustomInput();
  bool handleButtons();
  void navigateButtons();
  void moveSelectionTo(int);
  size_t findEntry(const std::string&) const;
  void loadFiles() {
    if (basepath == "/")
      files = {"Books/", "Alpha.epub", "Beta.bin"};
    else if (basepath == "/Books")
      files = {"Child/", "Gamma.epub"};
    else
      files.clear();
  }
};
