#pragma once

#include <xp_ui_home.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/RustActivityAdapter.h"
#include "activities/reader/ReaderChromeListActivity.h"
#include "components/OptionPopup.h"

class FileBrowserActivity final : public RustActivityAdapter {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // File actions
  bool removeDirFile(const std::string& fullPath);
  void showEntryActions();
  void startRename();
  void renameSelectedFile(const std::string& oldPath, const std::string& oldEntry, const std::string& newStem,
                          const std::string& extension);
  void deleteSelected();

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;
  OptionPopup optionPopup;

  // Rust requests rows on demand. Only `files` scales with the directory;
  // label/value scratch buffers remain valid until the next row request.
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;  // NAME_BUFFER_SIZE + "[]" + terminator slack
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};

  // CJK fallback glyphs are prewarmed for a bounded window of rows around the
  // viewport (one SD pass per list page, like the reader TOC) instead of the
  // whole folder. -1 = nothing prewarmed; reset by loadFiles().
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);

  int listCount() const { return static_cast<int>(files.size()); }
  void activateSelected();
  XpUiFileState state{};
  Publication publication;
  XpUiHomeListHits publishedHits{};
  struct Frame {
    ReaderChromeCommands commands;
    XpUiFileState snapshot{};
    XpUiFileFeedback next{};
    XpUiHomeListHits hits{};
  } frame;
  bool nativeLongPress = false, nativeLongTouch = false;
  int nativeTouchX = 0, nativeTouchY = 0;
  uint8_t touchFeedback = 0;
  Publication* rustPublication() override { return &publication; }
  XpUiInterest rustInterest(uint32_t now) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  XpUiPaint rustResources() override;
  XpUiHomeRows rows();
  XpUiFileFacts facts(int selected) const;
  const char* text(uint32_t handle);
  void loaded(uint8_t reason, int selected = 0);

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
