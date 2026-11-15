#pragma once

#include <FreeInkUI.h>

// Share the SDK header implementation used by the Rust screen resource adapters.
void paintUiHeader(freeink::ui::Frame<24>& frame, freeink::ui::Rect rect, const freeink::ui::HeaderProps& props);
