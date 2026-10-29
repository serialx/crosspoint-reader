#pragma once
#include <xp_ui_home.h>

#include <array>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/RustActivityAdapter.h"
#include "activities/reader/ReaderChromeListActivity.h"
#include "components/CoverGridHomeUi.h"

struct Rect;

class HomeActivity final : public RustActivityAdapter {
  std::unique_ptr<CoverGridHomeUi> coverGridUi;
  XpUiHomeState state{};
  Publication publication;
  XpUiHomeTouch publishedTouch{};
  XpUiHomeGridGeometry publishedGrid{};
  struct Frame {
    XpUiHomeState snapshot{};
    XpUiHomeLayout layout{};
    XpUiHomePrepared covers{};
    XpUiHomeTouch touch{};
    XpUiHomeGridGeometry grid{};
    XpUiRect restoreRect{};
    ReaderChromeCommands commands;
    struct TitleWrap {
      int32_t font = 0;
      int16_t width = 0;
      uint8_t count = 0;
      uint8_t style = 0;
    };
    std::array<TitleWrap, 3> titleWraps{};
    std::array<uint8_t, 3> titleLineCounts{};
    std::vector<std::string> titleLines;
    uint8_t titleBook = 255;
    std::string authorLine;
    std::array<std::string, 6> menuLines;
    std::array<uint8_t, 6> menuKeys{};
    uint8_t menuCount = 0;
  } frame;
  bool touchActivated = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Region owned by the cover snapshot; prepared before scene construction.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;
  const bool cleanInitialRefresh;

  void onRecentBookSelected(const std::string& path);
  void onFileBrowserOpen();
  void onLibraryOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onPluginsOpen();

  Publication* rustPublication() override { return &publication; }
  XpUiInterest rustInterest(uint32_t now) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  // Home Back opens the continuing book, so it is activation rather than cancellation.
  void cancelRejectedInput(const XpUiInput&) override {}
  void rustSceneFailed() override {
    touchActivated = false;
    state.last_repeat = 0;
  }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  XpUiPaint rustResources() override;
  XpUiHomeTexts textServices();
  const char* text(uint32_t handle);
  void prepareTitle(uint8_t book);
  void sampleLayout();
  void prepareCovers();
  bool allocateCoverBuffer();
  bool storeCoverBuffer();  // Store frame buffer for cover image
  void freeCoverBuffer();   // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  void fillCoverGridFromLibrary();
  void resolveGridCoverPaths();
  void loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : RustActivityAdapter("Home", renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
