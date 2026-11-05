#pragma once
#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include <string>
#include <utility>

#include "activities/RustActivityAdapter.h"

class FullScreenMessageActivity final : public RustActivityAdapter {
  std::string text;
  EpdFontFamily::Style style;
  HalDisplay::RefreshMode refreshMode;

 public:
  explicit FullScreenMessageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text,
                                     const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                     const HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH)
      : RustActivityAdapter("FullScreenMessage", renderer, mappedInput),
        text(std::move(text)),
        style(style),
        refreshMode(refreshMode) {}
  void onEnter() override;
  void loop() override {}
  void render(RenderLock&&) override {}
};
