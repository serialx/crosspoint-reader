#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <freertos/semphr.h>
#include <xp_ui_updates.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

class UpdateUiActivity : public RustActivityAdapter {
 public:
  UpdateUiActivity(const char*, GfxRenderer&, MappedInputManager&, XpUiUpdateKind, bool recovery = false);
  ~UpdateUiActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool skipLoopDelay() override;

 protected:
  // RenderLock -> resources -> ModelLock. Never held during panel refresh.
  class ResourceLock {
    SemaphoreHandle_t mutex;

   public:
    explicit ResourceLock(UpdateUiActivity&);
    ~ResourceLock();
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
  };
  XpUiUpdateState model{};
  XpUiUpdateState painting{};
  uint8_t phase() const;
  int selectedIndex() const;
  int paintedSelectedIndex() const { return next.list.selected; }
  void notify(XpUiUpdateEvent, uint32_t a = 0, uint32_t b = 0);
  uint8_t serviceResult(XpUiUpdateEvent, uint32_t a = 0, uint32_t b = 0);
  void closeRouting();
  virtual void performAction(XpUiUpdateAction, int32_t) = 0;
  virtual int rowCount() const { return 0; }
  virtual int paintedRowCount() const { return 0; }
  virtual int familyCount() const { return 0; }
  virtual void prepareRows() {}
  virtual bool row(uint16_t, XpUiSettingsRow&) const { return false; }
  virtual const char* dynamicText(uint32_t) const { return ""; }
  virtual void prepareText() {}
  virtual void releaseText() {}
  virtual void afterHeader() const {}
  virtual const char* headerSubtitle() const { return nullptr; }
  virtual const char* fontConfirmLabel() const { return ""; }

 private:
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateRecursiveMutexStatic(&resourceMutexStorage);
  Publication publication;
  XpUiUpdateFeedback feedback{}, next{};
  XpUiUpdateEffect pending{};
  bool pendingHaptic = false;
  bool routesReady = false;
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t capacity = 0;
  int inputRowCount = 0;
  int inputFamilyCount = 0;
  freeink::ui::GfxRendererTarget textTarget;
  char percentText[16]{};
  bool reserve(uint16_t);
  const char* text(uint32_t) const;
  void hints(uint8_t) const;
  XpUiUpdateResources resources();
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  void rustSceneFailed() override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
