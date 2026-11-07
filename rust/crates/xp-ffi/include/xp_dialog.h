#pragma once
#include "xp_app.h"
#include "xp_ui_utility.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { XP_DIALOG_CONFIRMATION, XP_DIALOG_INTERVAL, XP_DIALOG_MESSAGE };
typedef struct {
  uint16_t title, format, boundary;
  int32_t initial, minimum, maximum, small_step, large_step;
  uint8_t edge_buttons, reader;
} XpDialogInterval;
// Call-scoped descriptor. Strings remain native-owned until release_launch.
struct XpDialogLaunch {
  uint8_t kind, style, refresh;
  int32_t message_font;
  XpAppText name, heading, body, cancel, confirm;
  XpDialogInterval interval;
};
// Retires with the last native caller that pushes a dialog.
uint8_t xp_app_dialog_push(void* activity);
// Retire when Rust dialog creation no longer constructs the native facade.
void xp_dialog_launch(void* activity, XpDialogLaunch* out);
void xp_dialog_release_launch(void* activity);
// Physical leaves retire with Phase 18 native UI drawing/formatting.
void xp_dialog_layout(void* activity, XpUiUtilityLayout* out);
XpUiMetrics xp_dialog_metrics(void* activity);
void xp_dialog_footer(void* activity, XpUiRect clip, const XpAppText* labels);
void xp_dialog_format(uint16_t format, int32_t value, uint8_t as_unsigned, uint8_t* out, size_t size);
void xp_dialog_log(void* activity, uint8_t event, uint32_t bytes);
// Scalar compatibility projection; retires with the last native result receiver.
void xp_dialog_deliver(const XpAppLegacy* legacy, void* parent, uint8_t kind, uint32_t value);
#ifdef __cplusplus
}
#endif
