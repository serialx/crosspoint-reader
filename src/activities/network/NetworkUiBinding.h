#pragma once
#include <xp_ui.h>

#include <string>

class GfxRenderer;
class MappedInputManager;

namespace networkui {
enum class Kind { Text, Header, Subheader, Footer, Progress, Qr, Help, Centered, Wrapped, WrappedBottom, FlowText };
// Borrowed only for this paint call; formatted strings stay owned by the caller.
struct Resource {
  Kind kind;
  const char* labels[4]{};
  const std::string* qr = nullptr;
  uint32_t received = 0, total = 0;
};
void paint(GfxRenderer&, MappedInputManager&, const XpUiCommand&, XpUiRect, const Resource&);
}  // namespace networkui
