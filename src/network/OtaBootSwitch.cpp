#include "OtaBootSwitch.h"

#include <Logging.h>
#include <xp_ota.h>

namespace ota_boot {
namespace {
struct Context {
  const esp_partition_t* dest;
  const esp_partition_t* otadata = nullptr;
};
size_t operation(void* opaque, XpBootOperation op, size_t off, uint8_t* bytes, size_t len) {
  auto& c = *static_cast<Context*>(opaque);
  switch (op) {
    case XP_BOOT_FIND:
      c.otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
      return c.otadata != nullptr;
    case XP_BOOT_SIZE:
      return c.otadata->size;
    case XP_BOOT_READ:
      return esp_partition_read(c.otadata, off, bytes, len) == ESP_OK;
    case XP_BOOT_ERASE:
      return esp_partition_erase_range(c.otadata, off, len) == ESP_OK;
    case XP_BOOT_WRITE:
      return esp_partition_write(c.otadata, off, bytes, len) == ESP_OK;
  }
  return 0;
}
// Events retain the original selection, read, erase and write diagnostics.
void diagnostic(void* opaque, uint32_t event, uint32_t a, uint32_t b, uint32_t c) {
  const auto& job = *static_cast<Context*>(opaque);
  switch (event) {
    case 0:
      LOG_ERR("BOOT", "otadata partition not found");
      break;
    case 1:
      LOG_ERR("BOOT", "otadata too small: %u", static_cast<unsigned>(a));
      break;
    case 2:
      LOG_ERR("BOOT", "otadata read failed");
      break;
    case 3:
      LOG_INF("BOOT", "otadata: active slot=%d seq=%u", static_cast<int>(a), static_cast<unsigned>(b));
      break;
    case 4:
      LOG_ERR("BOOT", "dest is not an OTA app partition (subtype=0x%02X)", static_cast<unsigned>(a));
      break;
    case 5:
      LOG_ERR("BOOT", "otadata erase failed (slot=%d)", static_cast<int>(a));
      break;
    case 6:
      LOG_ERR("BOOT", "otadata write failed (slot=%d)", static_cast<int>(a));
      break;
    case 7:
      LOG_INF("BOOT", "otadata: wrote slot=%d seq=%u crc=0x%08x -> %s", static_cast<int>(a), static_cast<unsigned>(b),
              static_cast<unsigned>(c), job.dest->label);
      break;
  }
}
}  // namespace
uint32_t computeSeqCrc(uint32_t seq) { return xp_ota_seq_crc(seq); }
bool switchTo(const esp_partition_t* dest) {
  if (!dest) return false;
  Context ctx{dest};
  const XpBootServices api{&ctx, operation, diagnostic};
  return xp_ota_boot_switch(&api, dest->subtype);
}
}  // namespace ota_boot
