#pragma once
#include <Epub.h>

#include <memory>
#include <string>

#include "ReaderChromeListActivity.h"

class EpubReaderChapterSelectionActivity final : public ReaderChromeListActivity {
  std::shared_ptr<Epub> epub;
  int currentTocIndex = 0;

  // Retain the existing bounded SD-backed title window and glyph prewarm.
  static constexpr int TOC_WINDOW = 24;
  std::string windowLabels[TOC_WINDOW];
  ReaderChromeRow windowItems[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;
  void refreshTocWindow(int start);

  // Total TOC items count
  int listCount() const override { return epub ? epub->getTocItemsCount() : 0; }
  void prepareRows(int first) override { refreshTocWindow(first); }
  const ReaderChromeRow* rowAt(int index) const override;
  bool hasModel() const override { return !!epub; }
  const char* headerTitle() const override;
  const char* emptyText() const override;
  void activateIndex(int index) override;

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, int currentTocIndex);
  void onEnter() override;
};
