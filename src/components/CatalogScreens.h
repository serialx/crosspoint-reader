#pragma once

#include "UiAppHost.h"

class GfxRenderer;

// Draw the unified themed header (status band, title, rule, and a back button
// on touch boards) and reserve the button-hint band below the content. A
// trailing icon (e.g. search) becomes a header button.
void catalogScreenHeader(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const char* title,
                         const freeink::ui::BitmapRef& trailingIcon = {},
                         freeink::ui::ActionId trailingAction = freeink::ui::NO_ACTION);
