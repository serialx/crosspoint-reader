#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <I18n.h>
#include <xp_ui_settings.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

struct SettingsUiRow {
  const char* label = nullptr;
  const char* subtitle = nullptr;
  const char* value = nullptr;
  uint32_t icon = 0;
  bool toggle = false;
  bool toggleChecked = false;
  bool enabled = true;
  int16_t actionValue = 0;
};

class SettingsUiActivity : public RustActivityAdapter {
 public:
  SettingsUiActivity(const char*, GfxRenderer&, MappedInputManager&, XpUiSettingsKind);
  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  bool hasUiStorage() const { return commands != nullptr; }
  virtual int listCount() const = 0;
  virtual void prepareRows() {}
  virtual const SettingsUiRow* rowAt(int) const = 0;
  virtual void activateIndex(int) = 0;
  virtual const char* headerTitle() const { return nullptr; }
  virtual const char* headerTrailing() const { return nullptr; }
  virtual void drawFooter();
  virtual void drawPreview() {}
  virtual uint8_t screenFlags() const { return 0; }
  virtual bool smallLabelsOnTouch() const { return false; }
  virtual const char* specialText(uint32_t) const { return nullptr; }
  virtual bool handleCustomInput() { return false; }
  virtual void onRowLongPress(int) {}
  virtual void onBackButton();
  int selectedIndex() const;
  void selectIndex(int);
  void resetSelection(int = 0);
  bool routingReady() const;
  void resetUi();
  static void setCheckboxRow(SettingsUiRow&, bool);

 private:
  friend class KOReaderSettingsActivity;
  bool uiSceneFailed() const;
  void rustSceneFailed() override;
  XpUiSettingsKind kind;
  XpUiListState state{};
  XpUiListState next{};
  XpUiSettingsHits publishedHits{}, nextHits{};
  XpUiSettingsLayout layout{};
  Publication publication;
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t commandCapacity = 0;
  int32_t renderingScroll = 0;
  bool longTouch = false;
  bool touchActivated = false;
  int touchX = -1, touchY = -1;
  freeink::ui::GfxRendererTarget textTarget;
  bool reserveCommands(uint16_t);
  const char* text(uint32_t) const;
  XpUiSettingsResources resources();
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
