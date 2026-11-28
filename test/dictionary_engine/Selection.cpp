#include <DictionaryRegistry.h>
#include <HalStorage.h>
#include <xp_ui_dictionary.h>

#include <cassert>
#include <vector>

namespace {
struct {
  char dictionaryName[16] = "Demo";
} SETTINGS;
struct Activity {
  void onEnter() {}
};
struct Selection : Activity {
  enum class Mode { Clip, DefinitionLookup };
  Mode mode = Mode::Clip;
  bool lookupHidden = false;
  void onEnter() {
#include "SelectionEntry.inc"
  }
};

std::vector<uint32_t> actionLabels() {
  Selection entry;
  entry.onEnter();
  XpUiSelectionWord word{};
  word.x = 40;
  word.width = word.height = 20;
  word.text = "word";
  word.endOffset = 4;
  XpUiSelectionState state{};
  xp_ui_selection_init(&state, XP_UI_SELECT_CLIP, &word, 1, 300, 0, 0);
  XpUiSelectionLayout layout{};
  layout.screen = layout.safe = {0, 0, 300, 180};
  layout.spacing = layout.padding = 4;
  layout.label_width = 40;
  layout.label_height = 18;
  layout.touch = 1;
  layout.lookup_hidden = entry.lookupHidden;
  XpUiCommand commands[24]{};
  const XpUiBuffer buffer{commands, nullptr, 24, 0};
  XpUiSelectionPainted old{}, next{};
  XpUiScene scene{};
  assert(xp_ui_selection_view(&state, &word, 1, &layout, &old, &buffer, &next, &scene) == 1);
  std::vector<uint32_t> labels;
  for (unsigned i = 0; i < scene.command_count; ++i) {
    if (commands[i].kind == XP_UI_TEXT_HANDLE) labels.push_back(commands[i].resource);
  }
  return labels;
}
}  // namespace

int main() {
  files["/dictionaries"].directory = true;
  files["/dictionaries/Demo"].directory = true;
  files["/dictionaries/Demo/book.idx"] = {};
  files["/dictionaries/Demo/book.dict"] = {};
  assert((actionLabels() == std::vector<uint32_t>{XP_UI_DICT_LOOKUP, XP_UI_DICT_CLIP, XP_UI_DICT_BOOKMARK}));
  files.erase("/dictionaries/Demo/book.dict");
  assert((actionLabels() == std::vector<uint32_t>{XP_UI_DICT_CLIP, XP_UI_DICT_BOOKMARK}));
}
