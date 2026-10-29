#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <freertos/semphr.h>
#include <xp_ui_catalog.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

class CatalogUiActivity : public RustActivityAdapter {
 public:
  CatalogUiActivity(const char*, GfxRenderer&, MappedInputManager&, XpUiCatalogKind, int index = -1,
                    bool picker = false);
  ~CatalogUiActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  // RenderLock -> resources -> model. Input takes resources/model only, never
  // RenderLock. Recursive because resource transitions call shared helpers.
  class ResourceLock {
    SemaphoreHandle_t mutex;

   public:
    explicit ResourceLock(CatalogUiActivity& owner) : mutex(owner.resourceMutex) {
      xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    }
    ~ResourceLock() { xSemaphoreGiveRecursive(mutex); }
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
  };
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
  virtual void paintCatalogResource(const XpUiCommand&) {}
  virtual void catalogEffect(const XpUiEffect&) {}
  int selectedIndex() const;
  void selectIndex(int);
  void resetSelection();
  void closeRouting();
  bool hasUiStorage() const { return commands != nullptr; }

 private:
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateRecursiveMutexStatic(&resourceMutexStorage);
  Publication publication;
  XpUiListState next{};
  XpUiCatalogHits publishedHits{}, nextHits{};
  XpUiCatalogLayout layout{};
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t capacity = 0;
  bool touchActivated = false;
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
  bool rustSceneCurrent() const override;
  void rustPublished() override;
  void rustSceneFailed() override;
  void cancelRejectedInput(const XpUiInput&) override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
