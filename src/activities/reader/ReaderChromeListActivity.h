#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <xp_ui_reader_chrome.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

struct ReaderChromeRow {
  const char* label = nullptr;
  const char* subtitle = nullptr;
  const char* value = nullptr;
  bool bookmarkIcon = false;
  bool toggle = false;
  bool checked = false;
};

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

// Owns the resource lifetime and Activity ABI for the reader's Rust lists.
class ReaderChromeListActivity : public RustActivityAdapter {
 public:
  ReaderChromeListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input, XpUiReaderListKind kind,
                           uint16_t rowCapacity);
  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  virtual int listCount() const = 0;
  virtual bool hasModel() const { return true; }
  virtual void prepareRows(int first) {}
  virtual const ReaderChromeRow* rowAt(int index) const = 0;
  virtual void activateIndex(int index) = 0;
  virtual void onRowLongPress(int index) {}
  virtual void swipeDelete(int index) {}
  virtual void onBackButton();
  virtual bool handleCustomInput() { return false; }
  virtual const char* headerTitle() const = 0;
  virtual const char* emptyText() const { return ""; }
  virtual const char* helpText() const { return ""; }
  virtual const char* progressText() const { return ""; }
  virtual void drawFooter();
  int selectedIndex() const { return state.list.selected; }
  void selectIndex(int index);
  bool resourcesReady() const { return arena.commands.ready(); }
  bool routingReady() const {
    ModelLock lock(*const_cast<ReaderChromeListActivity*>(this));
    return publication.ready;
  }
  bool renderList(bool refresh = true);

 private:
  struct Arena {
    ReaderChromeCommands commands;
    XpUiReaderListState next{};
    XpUiReaderTheme theme{};
    XpUiReaderHitLayout nextHits{};
  };
  XpUiReaderListKind kind;
  uint16_t rowCapacity;
  XpUiReaderListState state{};
  Publication publication;
  Arena arena;
  XpUiReaderHitLayout publishedHits{};
  freeink::ui::GfxRendererTarget textTarget;
  bool touchActivated = false;
  int32_t renderingScroll = 0;
  bool nativeLongPress = false;
  bool nativeLongTouch = false;
  int nativeTouchX = 0;
  int nativeTouchY = 0;
  const char* text(uint32_t handle) const;

  XpUiReaderRows rows();
  Publication* rustPublication() override { return &publication; }
  void rustPublished() override;
  void rustSceneFailed() override;
  XpUiInterest rustInterest(uint32_t now) const override;
  XpUiEffect updateRust(const XpUiInput& input) override;
  void applyRustEffect(const XpUiEffect& effect) override;
  XpUiPaint rustResources() override;
  bool buildRustScene(XpUiScene& scene) override;
};
