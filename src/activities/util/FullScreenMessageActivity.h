#pragma once
#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include "DialogActivity.h"
class FullScreenMessageActivity final : public DialogActivity {
  std::string text;
  EpdFontFamily::Style style;
  HalDisplay::RefreshMode refreshMode;

 public:
  explicit FullScreenMessageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text,
                                     EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                     HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH)
      : DialogActivity("FullScreenMessage", renderer, mappedInput),
        text(std::move(text)),
        style(style),
        refreshMode(refreshMode) {}
  XpDialogLaunch launch() const override;
  void releaseLaunch() override { std::string().swap(text); }
};
