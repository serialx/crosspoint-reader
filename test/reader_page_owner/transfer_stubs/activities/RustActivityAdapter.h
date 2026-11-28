#pragma once
#include "activities/Activity.h"
class RustActivityAdapter : public Activity {
 public:
  RustActivityAdapter(const char*, GfxRenderer&, MappedInputManager&) {}

 protected:
  virtual XpUiPaint rustResources() {
    assert(false);
    return {};
  }
  virtual XpUiInterest rustInterest(uint32_t) const {
    assert(false);
    return {};
  }
  virtual XpUiEffect updateRust(const XpUiInput&) {
    assert(false);
    return {};
  }
  virtual void applyRustEffect(const XpUiEffect&) { assert(false); }
  virtual void rustSceneFailed() { assert(false); }
  virtual void cancelRejectedInput(const XpUiInput&) { assert(false); }
};
