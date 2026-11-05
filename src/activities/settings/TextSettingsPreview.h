#pragma once
#include <xp_ui_text_settings.h>

#include <memory>

#include "components/themes/BaseTheme.h"
class GfxRenderer;
class Page;
namespace textsettings {
struct PreviewLayout {
  PreviewLayout();
  ~PreviewLayout();
  std::unique_ptr<Page> page;
  XpUiTextPreviewKey key{};
};
void renderPreview(GfxRenderer&, PreviewLayout&, int previewPadding, int labelGap, Rect bounds, const char* caption);
}  // namespace textsettings
