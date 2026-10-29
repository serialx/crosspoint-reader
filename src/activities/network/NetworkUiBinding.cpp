#include "NetworkUiBinding.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/QrUtils.h"

void networkui::paint(GfxRenderer& renderer, MappedInputManager& input, const XpUiCommand& command, XpUiRect clip,
                      const Resource& resource) {
  const auto& r = command.rect;
  const Rect bounds{r.x, r.y, r.w, r.h};
  switch (resource.kind) {
    case Kind::Text:
      if (command.max_lines == 0 && command.align == 1)
        renderer.drawCenteredText(command.font, r.y, resource.labels[0], command.paint != 1,
                                  static_cast<EpdFontFamily::Style>(command.style));
      else
        renderer.drawUiText(command, clip, resource.labels[0]);
      break;
    case Kind::Header:
      GUI.drawHeader(renderer, bounds, resource.labels[0], resource.labels[1]);
      break;
    case Kind::Subheader:
      GUI.drawSubHeader(renderer, bounds, resource.labels[0], resource.labels[1]);
      break;
    case Kind::Footer: {
      const auto labels =
          input.mapLabels(resource.labels[0], resource.labels[1], resource.labels[2], resource.labels[3]);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case Kind::Progress:
      GUI.drawProgressBar(renderer, bounds, resource.received, resource.total);
      break;
    case Kind::Qr:
      QrUtils::drawQrCode(renderer, bounds, *resource.qr);
      break;
    case Kind::Line:
      renderer.drawLine(r.x, r.y, r.x + r.w, r.y + r.h, command.width, command.paint != 1);
      break;
  }
}
