#pragma once
#include <xp_dialog.h>

#include "activities/Activity.h"

// Native constructor and physical services; all dialog state belongs to Rust.
class DialogActivity : public Activity {
 protected:
  DialogActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input)
      : Activity(name, renderer, input) {}

 public:
  enum class Log : uint8_t { Enter, Exit, Arena, Build, Paint, MessageBuild };
  DialogActivity* dialog() final { return this; }
  virtual XpDialogLaunch launch() const = 0;
  virtual void releaseLaunch() {}
  void layout(XpUiUtilityLayout&) const;
  XpUiMetrics metrics() const;
  void footer(XpUiRect, const XpAppText*) const;
  void log(Log event, uint32_t bytes) const;
};
