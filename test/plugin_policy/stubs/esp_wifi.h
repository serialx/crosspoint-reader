#pragma once
#include "Oracle.h"
using wifi_ps_type_t = int;
using esp_err_t = int;
constexpr int WIFI_PS_MIN_MODEM=1, WIFI_PS_NONE=0, ESP_OK=0;
inline int esp_wifi_get_ps(wifi_ps_type_t* p) { *p=1; oracle::record("wifi-get"); return 0; }
inline int esp_wifi_set_ps(wifi_ps_type_t p) { oracle::record("wifi-set %d",p); return 0; }
