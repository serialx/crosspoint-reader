#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <xp_ui_reader_chrome.h>

#include <memory>

void sampleReaderChromeTheme(const GfxRenderer& renderer, XpUiReaderTheme& theme, bool bookmarks = false);

// Scratch commands have no published pointers; failed growth can release the old block.
class ReaderChromeCommands {
 public:
  bool reserve(uint16_t count);
  bool grow();
  bool recover() { return capacity && reserve(capacity); }
  void reset() {
    commands.reset();
    capacity = 0;
  }
  bool ready() const { return commands != nullptr; }
  XpUiBuffer buffer() const { return {commands.get(), nullptr, static_cast<uint16_t>(commands ? capacity : 0), 0}; }

 private:
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t capacity = 0;
};
