#pragma once

#include <FreeInkUIGfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <functional>
#include <string>

#include "ReaderChromeListActivity.h"

class GfxRenderer;
class MappedInputManager;

// Resource facade for Rust toolbar/panel scenes over the reader page.
// The parent keeps its Model, navigation and Event API while EpubReaderActivity owns its lifecycle.
class ReaderToolbarUi {
 public:
  enum class Event { None = 0, Dismiss = 1, Tool = 2, PrevChapter = 3, NextChapter = 4, Scrub = 5, Row = 6 };

  struct RowFlags {
    bool toggle = false;
    bool checked = false;
  };

  struct Model {
    bool panel = false;  // false = toolbar, true = a Contents/Text/More panel
    // Toolbar
    const char* chapterTitle = nullptr;
    const char* pageInfo = nullptr;  // "12/40   51%"
    int progressPermille = 0;        // 0..1000 book progress (scrub handle)
    // Panel
    const char* panelTitle = nullptr;
    int itemCount = 0;
    int selectedIndex = -1;  // row the buttons' cursor sits on; -1 = none shown
    std::function<std::string(int)> rowText;
    std::function<std::string(int)> rowValue;
    RowFlags (*rowCheckbox)(void* ctx, int index) = nullptr;
    void* rowCheckboxContext = nullptr;
    // Tile row: the tool in focus (toolbar) / the open panel (panel). 0..2.
    int activeTool = 0;
    // Button boards keep the theme's denser list row height (as every other
    // list does there); touch boards use FreeInkUI's finger-sized rows.
    bool denseRows = false;
  };

  struct Routed {
    Event event = Event::None;
    int value = 0;        // Tool: tool index; Row: row index
    int permille = -1;    // Scrub: 0..1000 along the track
    bool routed = false;  // the gate was open and a touch frame was routed
    int x = 0;            // touch position of the routed frame (logical px)
    int y = 0;
  };

  explicit ReaderToolbarUi(GfxRenderer& renderer);

  // Screen-entry reset + action wiring. Call once when the overlay opens.
  void begin();
  // What the next render() draws. The strings must stay alive until render()
  // returns; the row callbacks are invoked during render() for the visible rows.
  void setModel(const Model& model) { model_ = model; }
  // Paint on the render task (no refresh, no clear). The parent publishes or rejects the result.
  bool render();
  // Route one loop-task input frame; returns the action it mapped to, if any.
  Routed route(const MappedInputManager& input);

  void resetNavigation(int selected = 0, int top = 0);
  bool scrollNavigation(int delta, int count);
  // Rows one page holds, measured after the first render.
  int visibleRows() const;

 private:
  GfxRenderer& renderer;
  freeink::ui::GfxRendererTarget textTarget;
  Model model_;
  XpUiListState nav_{};
  static constexpr int kMaxWindow = 16;
  std::string windowLabels_[kMaxWindow];
  std::string windowValues_[kMaxWindow];
  RowFlags rowFlags[kMaxWindow];
  uint16_t windowFirst = 0, windowCount = 0;
  char pageIndicator[16]{};
  ReaderChromeCommands commands;
  XpUiReaderTheme theme{};
  XpUiReaderToolbarInput contact{-1, -1, 0, {0, 0, 0}};
  XpUiReaderHitLayout published{};
  mutable portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
  bool ready = false;
  const char* text(uint32_t handle) const;
  XpUiReaderRows resources();
};
