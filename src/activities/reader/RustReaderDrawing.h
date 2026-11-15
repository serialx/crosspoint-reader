#pragma once

#include <xp_app.h>
#include <xp_ui_reader_chrome.h>
#include <xp_ui_utility.h>

class GfxRenderer;
class MappedInputManager;

// Synchronous physical operations; child state and scenes stay in Rust.
// Returns the existing HAL side-button topology for Percent input.
bool sampleReaderUtilityLayout(GfxRenderer& renderer, const MappedInputManager& input, XpUiUtilityLayout& layout);
void sampleReaderChildTheme(const GfxRenderer& renderer, XpUiReaderTheme& theme, XpUiReaderThemeFacts& facts,
                            bool toolbar);
void drawReaderHeader(GfxRenderer& renderer, XpUiRect bounds, XpAppText title, bool list);
void drawReaderUtilityFooter(GfxRenderer& renderer, const MappedInputManager& input, XpUiRect clip,
                             const XpAppText* labels);
void drawReaderToolbarIcon(GfxRenderer& renderer, uint32_t icon, XpUiRect bounds, XpUiRect clip);
#ifdef XP_APP_TEST
void prewarmReaderRows(GfxRenderer& renderer, int32_t font, const void* context,
                       XpAppText (*row)(const void*, uint32_t), uint32_t count);

#endif
