#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <xp_ui_koreader.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

class KOReaderUiActivity : public RustActivityAdapter {
 public:
  KOReaderUiActivity(const char*, GfxRenderer&, MappedInputManager&, XpKoKind);
  void onExit() override;
  bool preventAutoSleep() override;

 protected:
  // Runs and callbacks share the activity task; rendering never accesses this flag.
  bool runActive = false;
  class RunGuard {
    bool& active;
    bool entered;

   public:
    explicit RunGuard(bool& value) : active(value), entered(!value) {
      if (entered) active = true;
    }
    ~RunGuard() {
      if (entered) active = false;
    }
    explicit operator bool() const { return entered; }
    RunGuard(const RunGuard&) = delete;
    RunGuard& operator=(const RunGuard&) = delete;
  };
  XpKoState model{};
  XpKoState painting{};
  bool initializeUi();
  void publish(XpKoPhase, XpKoMessage = XP_KO_NO_MESSAGE, uint8_t selected = 0, const char* error = nullptr);
  void markReturn();
  virtual void snapshotResources() {}
  virtual void prepareText() {}
  virtual const char* resourceText(uint32_t) const;
  virtual void performEffect(const XpUiEffect&) = 0;

 private:
  const char* status = "";
  const char* paintedStatus = "";
  Publication publication;
  std::unique_ptr<XpUiCommand[]> commands;
  XpUiInteraction hitBuffers[2][2]{};
  uint8_t nextHits = 0;
  bool sceneFailed = false;
  bool touchDispatched = false;
  freeink::ui::GfxRendererTarget textTarget;
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override {
    nextHits ^= 1;
    sceneFailed = false;
  }
  void rustSceneFailed() override;
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
};
