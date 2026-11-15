#pragma once

#ifndef CROSSPOINT_UI_ORACLE
#error "UiOracleOverride.h is only for the local ui_oracle environment"
#endif

#ifdef __cplusplus
#include <cstdint>

// Library builds without the firmware HAL do not use its Frontlight macro.
#if __has_include(<HalFrontlight.h>)
#include <HalFrontlight.h>

#include "UiOracleFrontlight.h"

#undef Frontlight
#define Frontlight uiOracleFrontlight
#endif

class GfxRenderer;
class MappedInputManager;
void uiOracleEnter(GfxRenderer& renderer, MappedInputManager& input);
int32_t uiOracleWifiConnectionStatus(int32_t actual);
#endif
