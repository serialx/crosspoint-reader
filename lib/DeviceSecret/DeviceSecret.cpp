#include "DeviceSecret.h"

#include <Logging.h>
#include <esp_random.h>
#include <nvs.h>
#include <xp_hal.h>

#include <mutex>

namespace {
XpHalSecret state{};
std::mutex lock;
}  // namespace

XpHalSecret* borrowDeviceSecret(uint32_t* handle, XpHalSecretServices* services) {
  static_assert(sizeof(nvs_handle_t) == sizeof(*handle));
  lock.lock();
  *services = {handle,
               [](void* ctx) { return nvs_open("devid", NVS_READWRITE, static_cast<nvs_handle_t*>(ctx)) == ESP_OK; },
               [](void* ctx, uint8_t* bytes) -> uint8_t {
                 size_t stored = 32;
                 const auto err = nvs_get_blob(*static_cast<nvs_handle_t*>(ctx), "secret", bytes, &stored);
                 if (err == ESP_OK) return stored == 32 ? 0 : 2;
                 return err == ESP_ERR_NVS_NOT_FOUND ? 1 : 2;
               },
               [](void*, uint8_t* bytes) { esp_fill_random(bytes, 32); },
               [](void* ctx, const uint8_t* bytes) {
                 return nvs_set_blob(*static_cast<nvs_handle_t*>(ctx), "secret", bytes, 32) == ESP_OK;
               },
               [](void* ctx) { return nvs_commit(*static_cast<nvs_handle_t*>(ctx)) == ESP_OK; },
               [](void* ctx) { nvs_close(*static_cast<nvs_handle_t*>(ctx)); }};
  return &state;
}

void releaseDeviceSecret() { lock.unlock(); }

bool deviceSecret(uint8_t (&out)[32]) {
  uint32_t handle{};
  XpHalSecretServices services;
  auto* borrowed = borrowDeviceSecret(&handle, &services);
  const bool result = xp_hal_secret(borrowed, &services, out);
  if (!result) {
    LOG_ERR("DSEC", "Device secret unavailable");
  }
  releaseDeviceSecret();
  return result;
}
