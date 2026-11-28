#pragma once

#include "HttpTransfer.h"

// Network transfers can run for minutes (OPDS crawls, plugin API calls, OTA
// streams). Modem sleep powers the radio down between DTIM beacons, which adds
// multi-second latency to TLS handshakes and can stall packets mid-transfer,
// so disable WiFi power-save for the duration of the exchange. The previous
// mode is restored on scope exit, so guards nest safely and contexts that keep
// power-save off for their whole lifetime (the web server) are not reverted.
struct WifiPowerSaveGuard {
  WifiPowerSaveGuard() {
    const auto& api = httptransfer::leaves();
    previous = xp_wifi_guard_enter_retire_n7_s1_s2_w3b(api.radio, api.diagnostics);
  }
  ~WifiPowerSaveGuard() {
    const auto& api = httptransfer::leaves();
    xp_wifi_guard_exit_retire_n7_s1_s2_w3b(previous, api.radio, api.diagnostics);
  }
  WifiPowerSaveGuard(const WifiPowerSaveGuard&) = delete;
  WifiPowerSaveGuard& operator=(const WifiPowerSaveGuard&) = delete;

 private:
  XpHttpGuardToken previous{};
};
