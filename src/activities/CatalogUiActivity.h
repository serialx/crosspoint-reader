#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <xp_ui_catalog.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

class CatalogUiActivity : public RustActivityAdapter {
 public:
  CatalogUiActivity(const char*, GfxRenderer&, MappedInputManager&, XpUiCatalogKind, int index = -1,
                    bool picker = false);
  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  XpUiCatalogState ui{}, paintState{};
  virtual int listCount() const = 0;
  virtual bool rowAt(int, XpUiCatalogRow&) const = 0;
  virtual void prepareRows() {}
  virtual void activateIndex(int) = 0;
  virtual void onBackButton() { finish(); }
  virtual bool handleCustomInput() { return false; }
  virtual const char* headerTitle() const = 0;
  virtual void drawFooter();
  virtual const char* catalogText(uint32_t) const;
  virtual bool hasSearch() const { return false; }
  virtual uint8_t pageLinks() const { return 0; }
  virtual void pageNumbers(int32_t&, int32_t&) const {}
  virtual void catalogEffect(const XpUiEffect&) {}
  int selectedIndex() const;
  void selectIndex(int);
  void resetSelection();
  void closeRouting();
  bool hasUiStorage() const { return commands != nullptr; }

 private:
  Publication publication;
  XpUiListState next{};
  XpUiCatalogHits publishedHits{}, nextHits{};
  XpUiCatalogLayout layout{};
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t capacity = 0;
  bool touchActivated = false;
  bool sceneFailed = false;
  bool buildCatalogScene(XpUiScene&);
  int32_t consumedScroll = 0;
  freeink::ui::GfxRendererTarget target;
  bool reserveCommands(uint16_t);
  const char* text(uint32_t) const;
  void paintHeader(XpUiRect);
  __attribute__((noinline)) void paintBrowserHeader();
  __attribute__((noinline)) XpUiRect searchButtonRect(int16_t minimum) const;
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
