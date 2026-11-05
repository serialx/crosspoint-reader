#include <xp_ota.h>

static size_t otaOperation(void*, XpOtaOperation, const uint8_t*, size_t) { return 0; }
static bool fetch(void*, bool, XpOtaSink, void*) { return false; }
static size_t bootOperation(void*, XpBootOperation, size_t, uint8_t*, size_t) { return 0; }
int main() {
  XpOtaState state{};
  constexpr uint8_t tag[] = "CROSSPOINT-BOARD-V1:x4;";
  const XpOtaServices ota{nullptr, otaOperation, fetch, nullptr, tag, sizeof(tag) - 1, false};
  if (xp_ota_check(&state, &ota) != 6) return 1;
  if (xp_ota_install(&state, &ota, true) != 5) return 2;
  if (xp_ota_install(&state, &ota, false) != 4) return 3;
  const XpBootServices boot{nullptr, bootOperation, nullptr};
  if (xp_ota_boot_switch(&boot, 16)) return 4;
}
