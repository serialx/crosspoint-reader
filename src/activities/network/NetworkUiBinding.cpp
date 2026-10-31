#include "NetworkUiBinding.h"

#include <FreeInkUIGfxRenderer.h>
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
    case Kind::FlowText: {
      freeink::ui::GfxRendererTarget target(renderer);
      target.setFont(0, command.font);
      freeink::ui::TextStyle style;
      style.font = 0;
      style.bold = command.style & 1;
      style.maxLines = command.max_lines;
      style.align = static_cast<freeink::ui::TextAlign>(command.align);
      auto lineCommand = command;
      lineCommand.max_lines = 1;
      lineCommand.align = 0;
      freeink::ui::layoutText(target, {r.x, r.y, r.w, r.h}, resource.labels[0], style,
                              [&](const char* line, freeink::ui::Rect rect) {
                                lineCommand.rect = {rect.x, rect.y, rect.width, rect.height};
                                renderer.drawUiText(lineCommand, clip, line);
                              });
      break;
    }
    case Kind::Centered:
      UITheme::drawCenteredText(renderer, bounds, command.font, r.y, resource.labels[0], command.paint != 1,
                                static_cast<EpdFontFamily::Style>(command.style));
      break;
    case Kind::Wrapped:
    case Kind::WrappedBottom:
      UITheme::drawCenteredWrappedText(renderer, bounds, command.font, resource.labels[0], command.max_lines,
                                       command.paint != 1, static_cast<EpdFontFamily::Style>(command.style),
                                       resource.kind == Kind::WrappedBottom ? UITheme::TextVerticalAlignment::BOTTOM
                                                                            : UITheme::TextVerticalAlignment::CENTER);
      break;
    case Kind::Help:
      GUI.drawHelpText(renderer, bounds, resource.labels[0]);
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
  }
}
