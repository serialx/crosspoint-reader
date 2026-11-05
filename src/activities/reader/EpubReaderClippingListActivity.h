#pragma once

#include <array>

#include "ReaderChromeListActivity.h"
#include "clippings/ClippingPreview.h"
#include "components/OptionPopup.h"

class EpubReaderClippingListActivity final : public ReaderChromeListActivity {
 public:
  EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  void rebuildRows();
  void refreshRowWindow(int start);
  void openSelected();
  void readSelected();
  void showDeleteConfirmation();
  void deleteSelected();

  int listCount() const override;
  void prepareRows(int first) override { refreshRowWindow(first); }
  const ReaderChromeRow* rowAt(int index) const override;
  const char* emptyText() const override { return tr(STR_NO_CLIPPINGS); }
  const char* helpText() const override { return tr(STR_HOLD_SELECT_TO_DELETE); }
  void onBackButton() override { finish(); }
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;

  const char* headerTitle() const override;

  static constexpr int ROW_WINDOW = 8;
  std::array<std::array<char, clippingPreview::BUFFER_BYTES>, ROW_WINDOW> previews{};
  std::array<size_t, ROW_WINDOW> previewLengths{};
  std::array<ReaderChromeRow, ROW_WINDOW> rowItems;
  int windowStart = -1;
  int windowCount = 0;
  OptionPopup confirmPopup;
  bool confirmingDelete = false;
  bool initialListRender = true;
};
