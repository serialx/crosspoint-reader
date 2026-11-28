#pragma once
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <xp_ui.h>

#include <cassert>
#include <memory>

#include "activities/ActivityResult.h"
class MappedInputManager {};
class RenderLock {};
// Scheduling and painting are outside the constructor/destructor composition.
class Activity {
 public:
  virtual ~Activity() = default;
  virtual void onEnter() { assert(false); }
  virtual void loop() { assert(false); }
  virtual void render(RenderLock&&) { assert(false); }
  virtual bool isReaderActivity() const { return false; }
  virtual bool handleHomeGesture() {
    assert(false);
    return false;
  }
  void requestUpdate() { assert(false); }
};
