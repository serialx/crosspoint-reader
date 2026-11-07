#pragma once
#include "xp_ui_network.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  void (*layout)(void*, XpUiSettingsLayout*);
  XpUiSettingsResources (*resources)(void*);
  void (*paint)(void*, const XpUiCommand*, XpUiRect);
  void (*scroll_diagnostic)(int32_t delta, uint16_t count);
  void (*scene_diagnostic)(uint8_t invalid);
  void (*prepare_result_retire_n8)(void*, uint8_t cancelled, uint8_t mode);
} XpNetworkModesPhysical;
extern const XpNetworkModesPhysical XP_NETWORK_MODES_PHYSICAL;
#ifdef __cplusplus
}
#endif
