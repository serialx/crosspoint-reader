#pragma once

#include <xp_ui.h>

#include "Activity.h"

// ActivityManager still owns navigation, results, and the one render task.
// Derived facades own Rust state and any bounded scene arena for their visit.
class RustActivityAdapter : public Activity {
 public:
  RustActivityAdapter(const char* name, GfxRenderer& renderer, MappedInputManager& input)
      : Activity(name, renderer, input) {}
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  struct Publication {
    XpUiScene scene{};
    bool ready = false;
  };
  // Only snapshot/update/publication use this lock, never layout, painting or refresh.
  portMUX_TYPE modelMutex = portMUX_INITIALIZER_UNLOCKED;
  class ModelLock {
    portMUX_TYPE& mutex;

   public:
    explicit ModelLock(RustActivityAdapter& owner) : mutex(owner.modelMutex) { taskENTER_CRITICAL(&mutex); }
    ~ModelLock() { taskEXIT_CRITICAL(&mutex); }
    ModelLock(const ModelLock&) = delete;
    ModelLock& operator=(const ModelLock&) = delete;
  };
  // Published hits remain immutable while the next scene is built.
  virtual Publication* rustPublication() { return nullptr; }
  virtual bool buildRustScene(XpUiScene&) { return false; }
  virtual void rustPublished() {}
  virtual void rustSceneFailed() {}
  virtual XpUiPaint rustResources() { return {}; }
  virtual XpUiInterest rustInterest(uint32_t nowMs) const { return {}; }
  virtual XpUiEffect updateRust(const XpUiInput&) { return {}; }
  virtual void applyRustEffect(const XpUiEffect&) {}
  // Failure differs from not-yet-painted. Custom renderers must report every
  // rejection; only a complete successful publication may clear it. Hooks run
  // under ModelLock and may touch only scalars/fixed POD, never owning resources.
  void rejectRustScene();
  bool rustSceneRejected() const;
  // Call before custom input, including native long presses and queued contacts.
  // Consumes failed/held-over gestures, preserves the family's cancel policy,
  // and schedules retry without waiting for the render task.
  bool consumeRejectedInput();
  virtual void cancelRejectedInput(const XpUiInput& input);
  bool paintRustScene(const XpUiScene& scene, bool publish = true);
  void publishRustScene(const XpUiScene& scene);
  XpUiInput sampleRustInput() const;

 private:
  uint32_t suppressButtons = 0;
  uint32_t lastHeld = 0;
  bool sceneFailed = false;
  bool suppressContact = false;
  bool lastContact = false;

 protected:
  XpUiMetrics rustMetrics() const;
};
