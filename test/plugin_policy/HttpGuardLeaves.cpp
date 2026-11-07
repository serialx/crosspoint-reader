#include <Logging.h>
#include <esp_wifi.h>

#include <cstdlib>

#include "network/HttpTransfer.h"

namespace httptransfer {
namespace {
int32_t getPowerSave(int32_t* mode) {
  wifi_ps_type_t value;
  const auto error = esp_wifi_get_ps(&value);
  if (error == ESP_OK) *mode = value;
  return error;
}
int32_t setPowerSave(int32_t mode) { return esp_wifi_set_ps(static_cast<wifi_ps_type_t>(mode)); }
void diagnostic(XpHttpDiagnostic event, XpHttpBytes, XpHttpBytes, size_t error, size_t) {
  if (event == XP_HTTP_LOG_GUARD_ENTER_FAILED) {
    LOG_ERR("WIFI", "Failed to disable WiFi power-save: %d", static_cast<int>(error));
  } else if (event == XP_HTTP_LOG_GUARD_RESTORE_FAILED) {
    LOG_ERR("WIFI", "Failed to restore WiFi power-save: %d", static_cast<int>(error));
  } else {
    std::abort();
  }
}
constexpr XpHttpRadioLeaves radio{getPowerSave, setPowerSave};
constexpr XpHttpDiagnosticsRetireZ1 diagnostics{diagnostic};
constexpr XpHttpLeaves physical{nullptr, nullptr, &radio, nullptr, &diagnostics};
}  // namespace
const XpHttpLeaves& leaves() { return physical; }
}  // namespace httptransfer
