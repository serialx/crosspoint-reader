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
  // Input and publication are serialized by RenderLock. No partial layout is
  // visible to input; failure closes routing while keeping the displayed frame.
  virtual Publication* rustPublication() { return nullptr; }
  virtual bool buildRustScene(XpUiScene&) { return false; }
  virtual void rustPublished() {}
  virtual XpUiPaint rustResources() { return {}; }
  virtual XpUiEffect updateRust(const XpUiInput&) { return {}; }
  virtual void applyRustEffect(const XpUiEffect&) {}
  bool paintRustScene(const XpUiScene& scene);
  XpUiMetrics rustMetrics() const;
};
