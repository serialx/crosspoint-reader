#pragma once

#include <FreeInkUIGfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <xp_ui_reader_chrome.h>

#include <atomic>
#include <string>
#include <vector>

class GfxRenderer;
class MappedInputManager;

// Shared End-of-Book next-book menu for the EPUB and XTC readers. Collects up to
// MAX_SUGGESTIONS sibling books once per reader session, handles the menu input, and
// draws the end screen. With no suggestions the end screen keeps its historical
// plain-title look and behavior.
class EndOfBookOptions {
 public:
  enum class Action { None, Redraw, OpenBook, GoHome, LastPage };

  static constexpr size_t MAX_SUGGESTIONS = 3;

  explicit EndOfBookOptions(GfxRenderer& renderer);

  // Scans the book's folder for suggestions; no-op when already loaded. Call ONLY from
  // the reader's render() (the render task, serialized by RenderLock) — the loaded flag
  // is the release/acquire publication point that lets the main task read the finished
  // list safely.
  void loadOnce(const std::string& currentBookPath);

  // True when the suggestion menu is showing and should own the reader's input.
  bool menuActive() const;

  // Menu input handling, following the standard list idiom: a tap on a row opens it
  // (or Home), side Up/Down and front Left/Right move the selection (wrapping),
  // Confirm opens the selection, and a short Back press returns to the last page of
  // the book. Fills openPath when the result is OpenBook. Returns Action::None when
  // nothing relevant was pressed; callers continue their normal input path (keeping
  // long-press Back to the file browser working).
  Action handleMenuInput(const MappedInputManager& input, std::string* openPath);

  // Draws the full end screen (plain title, or the suggestion menu) onto a cleared buffer.
  void render(GfxRenderer& renderer, const MappedInputManager& input);

 private:
  GfxRenderer& renderer;
  freeink::ui::GfxRendererTarget textTarget;
  std::string folder;
  std::vector<std::string> names;
  std::atomic<bool> isLoaded{false};
  static constexpr size_t MAX_ROWS = MAX_SUGGESTIONS + 1;
  std::string rowLabels[MAX_ROWS];
  uint16_t rowCount = 0;
  XpUiReaderListState state{};
  XpUiReaderListState next{};
  XpUiReaderTheme theme{};
  // Four text rows plus selection, scrollbar, title, subtitle and footer.
  XpUiCommand commands[16]{};
  XpUiReaderHitLayout published{};
  portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
  bool ready = false;
  bool sceneFailed = false;
  bool suppressedContact = false, lastContact = false;
  uint32_t suppressed = 0, lastHeld = 0;
  const MappedInputManager* renderingInput = nullptr;
  const char* text(uint32_t handle) const;
  XpUiReaderRows resources();
  std::string fullPath(size_t index) const;
};
