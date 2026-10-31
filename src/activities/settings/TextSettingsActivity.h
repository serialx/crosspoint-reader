#pragma once
#include <SdCardFontRegistry.h>
#include <xp_ui_text_settings.h>

#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/RustActivityAdapter.h"
#include "activities/reader/ReaderChromeListActivity.h"
#include "components/OptionPopup.h"

class TextSettingsActivity final : public RustActivityAdapter {
 public:
  enum class Tab : uint8_t { Family, Size, Layout, Style, Count };
  TextSettingsActivity(GfxRenderer&, MappedInputManager&, const SdCardFontRegistry*, Tab initialTab = Tab::Family);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };
  struct SizeEntry {
    std::string name;
    uint8_t pointSize;
  };
  const SdCardFontRegistry* registry_;
  OptionPopup optionPopup_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;
  XpUiTextState state{}, rendering{}, next{};
  XpUiTextValues values{};
  XpUiTextLayout layout{};
  XpUiReaderHitLayout publishedHits{}, nextHits{};
  ReaderChromeCommands commands;
  Publication publication{};
  char rowValues[7][16]{};
  const char* rowText[7]{};
  char caption[128]{};
  int32_t renderingScroll = 0;
  bool touchActivated = false;
  void rustSceneFailed() override { touchActivated = false; }
  int rebuildSizeList();
  bool applyFamily(int index);
  bool applySize(int index);
  void activateRow(int index);
  const char* text(uint32_t handle) const;
  XpUiReaderRows rows();
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  bool rustSceneCurrent() const override;
  void rustPublished() override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t now) const override { return xp_ui_text_interest(&state, now); }
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
