#pragma once
#include "xp_ui_settings.h"
#ifdef __cplusplus
extern "C" {
#endif

// Synchronous physical leaves; descriptors and terminated title text are borrowed
// only for the call. The caller serializes drawing with the existing RenderLock.
void xp_settings_shell_snapshot(XpUiSettingsLayout* layout);
// icon is 1..6 for a settings bitmap; zero paints the header with version text.
void xp_settings_shell_paint(const XpUiCommand* command, XpUiRect clip, const char* title, uint8_t icon);
void xp_settings_shell_theme_reload(void);

#ifdef __cplusplus
}
#endif
