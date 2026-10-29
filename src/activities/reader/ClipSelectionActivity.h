#pragma once

#include <Epub/Page.h>
#include <FontCacheManager.h>
#include <xp_ui_dictionary.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "activities/RustActivityAdapter.h"
#include "components/OptionPopup.h"

class ClipSelectionActivity final : public RustActivityAdapter {
 public:
  enum class Mode : uint8_t { Clip, Lookup, DefinitionLookup };

  struct Selection {
    uint16_t firstPage = 0;
    uint16_t lastPage = 0;
    uint16_t firstWord = 0;
    uint16_t lastWord = 0;
    bool valid = false;
  };

  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        std::vector<std::unique_ptr<Page>> pages, int marginLeft, int marginTop, int initialX = -1,
                        int initialY = -1, Mode mode = Mode::Clip);

  // The definition viewer retains these pages until this child exits.
  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        std::span<const std::unique_ptr<Page>> borrowedPages, int marginLeft, int marginTop,
                        Selection selection, int initialX = -1, int initialY = -1);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool handleHomeGesture() override;

 private:
  using WordBox = XpUiSelectionWord;

  static constexpr size_t MAX_SELECTABLE_WORDS = 240;

  bool extractWords();
  size_t pageCount() const { return borrowedPages.empty() ? pages.size() : borrowedPages.size(); }
  const Page& pageAt(size_t index) const { return *(borrowedPages.empty() ? pages[index] : borrowedPages[index]); }
  bool isLookup() const { return mode != Mode::Clip; }
  void confirmSelection(ClippingResult::Action action = ClippingResult::Action::Clip);
  void loopButtons();
  bool handleButtons(uint8_t buttons);
  void cancel();
  bool buildSelectedText(int first, int last, std::string& text) const;

  std::vector<std::unique_ptr<Page>> pages;
  std::span<const std::unique_ptr<Page>> borrowedPages;
  Selection initialSelection;
  const int marginLeft;
  const int marginTop;
  const int initialX;
  const int initialY;
  const Mode mode;
  OptionPopup actionPopup;
  int fontId = 0;
  int lineHeight = 0;
  std::unique_ptr<WordBox[]> words;
  size_t wordCount = 0;
  uint16_t rowCount = 0;
  XpUiSelectionState state{};
  XpUiSelectionState rendered{};
  XpUiSelectionLayout layout{};
  XpUiSelectionPainted painted{}, nextPainted{};
  XpUiCommand commands[20]{};
  const XpUiSelectionWord* wireWords() const { return words.get(); }
  XpUiSelectionLayout sampleLayout() const;
  void applySelection(const XpUiSelectionUpdate&);
  XpUiPaint rustResources() override;
  void rustSceneFailed() override {
    state.pending_count = state.dragging = state.repeating = state.moved = 0;
    state.ignore_touch = 1;
    state.last_repeat = state.dwell_since = 0;
    state.dwell_index = -1;
    painted.valid = 0;
  }
  void cancelRejectedInput(const XpUiInput& input) override {
    if (!(input.released & XP_UI_BACK)) return;
    if (actionPopup.isActive())
      actionPopup.handleButtons(false, false, false, true, [this] { requestUpdate(); });
    else
      cancel();
  }
  // Keep the cache through overlays, footer and refresh, then release it for this frame.
  std::optional<FontCacheManager::PrewarmScope> framePrewarm;
};
