#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
namespace freeink::ui {
struct ListItem {
  bool toggle = false, checked = false;
};
}  // namespace freeink::ui
struct {
  void setCheckboxRow(freeink::ui::ListItem& i, bool checked) { i = {true, checked}; }
} GUI;
struct {
  int focusReadingEnabled = 0, screenInverted = 0;
  struct {
    int enabled = 0;
  } ble;
} SETTINGS;
struct {
  bool on = false;
  bool isOn() const { return on; }
} Frontlight;
enum { STR_TOOL_CONTENTS, STR_TOOL_TEXT, STR_TOOL_MORE };
const char* tr(int id) {
  static const char* labels[] = {"Contents", "Text", "More"};
  return labels[id];
}
int clampPercent(int value) { return std::clamp(value, 0, 100); }
constexpr int kTextRowCount = 5;
struct EpubReaderMenuActivity {
  enum class MenuAction { NIGHT_MODE = 3, BLUETOOTH = 5, FRONTLIGHT = 4, OTHER = 0 };
  struct MenuItem {
    MenuAction action;
  };
};
struct Epub {
  struct Item {
    int level;
    std::string title;
  };
  std::vector<Item> items{{0, "zero"}, {1, "one"}, {2, "two"}, {4, "four"}};
  float extra = 0;
  bool fixedProgress = false;
  int getTocItemsCount() const { return items.size(); }
  Item getTocItem(int i) const { return items.at(i); }
  float calculateProgress(int spine, float progress) const {
    std::printf("progress %d %.9g\n", spine, progress);
    return fixedProgress ? 1.0f : extra + progress * .33333334f;
  }
};
struct Section {
  int currentPage = 0, count = 1;
  int estimatedTotalPages() const { return count; }
};
struct Input {
  bool touch = false;
  bool hasTouch() const {
    std::printf("touch %d\n", touch);
    return touch;
  }
};
struct ReaderToolbarUi {
  struct RowFlags {
    bool toggle = false, checked = false;
  };
  struct Checkbox {
    std::function<RowFlags(void*, int)> fn;
    template <typename F>
    Checkbox& operator=(F f) {
      fn = [f](void* ctx, int i) {
        if constexpr (std::is_invocable_r_v<RowFlags, F, void*, int>)
          return f(ctx, i);
        else {
          freeink::ui::ListItem item;
          f(ctx, i, item);
          return RowFlags{item.toggle, item.checked};
        }
      };
      return *this;
    }
  };
  struct Model {
    bool panel = false;
    const char* chapterTitle = nullptr;
    const char* pageInfo = nullptr;
    int progressPermille = 0;
    const char* panelTitle = nullptr;
    int itemCount = 0, selectedIndex = -1;
    std::function<std::string(int)> rowText, rowValue;
    Checkbox rowCheckbox;
    void* rowCheckboxContext = nullptr;
    int activeTool = 0;
    bool denseRows = false;
  } model;
  void setModel(const Model& value) { model = value; }
  void render() {
    std::printf("model %d %d %d %d %d %d [%s] [%s] [%s]\n", model.panel, model.activeTool, model.denseRows,
                model.selectedIndex, model.itemCount, model.progressPermille,
                model.chapterTitle ? model.chapterTitle : "", model.pageInfo ? model.pageInfo : "",
                model.panelTitle ? model.panelTitle : "");
    for (int i = 0; i < model.itemCount; ++i) {
      const auto flags = model.rowCheckbox.fn ? model.rowCheckbox.fn(model.rowCheckboxContext, i) : RowFlags{};
      std::printf("row %d [%s] [%s] %d %d\n", i, model.rowText ? model.rowText(i).c_str() : "",
                  model.rowValue ? model.rowValue(i).c_str() : "", flags.toggle, flags.checked);
    }
  }
};
struct EpubReaderActivity {
  enum class Overlay { None, Toolbar, Contents, Text, More };
  std::unique_ptr<Epub> epub = std::make_unique<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  std::unique_ptr<ReaderToolbarUi> toolbarUi = std::make_unique<ReaderToolbarUi>();
  Input mappedInput;
  Overlay overlay = Overlay::None;
  bool panelCursorShown = false;
  int focusedTool = 0, panelIndex = 2, currentSpineIndex = 3;
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems{{EpubReaderMenuActivity::MenuAction::OTHER},
                                                          {EpubReaderMenuActivity::MenuAction::NIGHT_MODE},
                                                          {EpubReaderMenuActivity::MenuAction::BLUETOOTH},
                                                          {EpubReaderMenuActivity::MenuAction::FRONTLIGHT}};
  std::string title = "Chapter title";
  std::string currentChapterTitle() const {
    std::puts("chapter-title");
    return title;
  }
  std::string textRowName(int i) const { return "text-" + std::to_string(i); }
  std::string textRowValue(int i) const { return "value-" + std::to_string(i); }
  std::string moreRowName(int i) const { return "more-" + std::to_string(i); }
  std::string moreRowValue(int i) const { return "more-value-" + std::to_string(i); }
  void renderOverlay();
};
// VIEW_METHODS
int main(int argc, char**) {
  if (argc > 1) {
    const int pages[] = {65534, INT_MAX - 1, INT_MIN, INT_MIN};
    const int counts[] = {65535, 65535, 65535, INT_MIN};
    for (int i = 0; i < 4; ++i) {
      EpubReaderActivity a;
      a.overlay = EpubReaderActivity::Overlay::Toolbar;
      a.section->currentPage = pages[i];
      a.section->count = counts[i];
      a.epub->fixedProgress = true;
      a.title = "Без назви";  // Longest shipped STR_UNNAMED by UTF-8 byte length (17).
      a.renderOverlay();
    }
    EpubReaderActivity a;
    a.overlay = EpubReaderActivity::Overlay::Toolbar;
    a.epub->fixedProgress = true;
    a.title.assign(4096, 'x');
    a.renderOverlay();
    return 0;
  }
  int index = 0;
  for (int mode = 0; mode < 5; ++mode)
    for (int touch = 0; touch < 2; ++touch)
      for (int cursor = 0; cursor < 2; ++cursor)
        for (int option = 0; option < 8; ++option)
          for (int position = 0; position < 6; ++position) {
            EpubReaderActivity a;
            a.overlay = static_cast<EpubReaderActivity::Overlay>(mode);
            a.mappedInput.touch = touch;
            a.panelCursorShown = cursor;
            a.focusedTool = option % 3;
            a.panelIndex = position - 1;
            SETTINGS.focusReadingEnabled = option & 1;
            SETTINGS.screenInverted = option & 2;
            SETTINGS.ble.enabled = option & 4;
            Frontlight.on = option & 1;
            const int counts[] = {0, 1, 40, 9999, -1, 40};
            const int pages[] = {0, 0, 11, 9998, -1, 50};
            a.section->count = counts[position];
            a.section->currentPage = pages[position];
            a.epub->extra = position == 5 ? .9f : .17f;
            std::printf("case %d\n", index++);
            a.renderOverlay();
          }
  for (int missing = 0; missing < 3; ++missing) {
    EpubReaderActivity a;
    if (missing == 0) a.epub.reset();
    if (missing == 1) a.section.reset();
    if (missing == 2) a.toolbarUi.reset();
    std::printf("missing %d\n", missing);
    a.renderOverlay();
  }
}
