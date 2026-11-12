#pragma once
#include <xp_sd_physical.h>

#include "Contract.h"

namespace hal_contract {
template <class FailNextOpen>
void checkSlots(FailNextOpen failNextOpen) {
  XpSdFileSlot slots[2];
  for (auto& slot : slots) xp_native_sd_handle_init(&slot);
  uint32_t size = 0;
  xp_native_sd_file_open(&slots[0], "font", &size);
  rejects([&] { xp_native_sd_file_read(&slots[1], nullptr, 0); });
  rejects([&] { xp_native_sd_file_seek(&slots[1], 0); });
  rejects([&] { xp_native_sd_file_close(&slots[1]); });
  failNextOpen();
  require(!xp_native_sd_file_open(&slots[1], "", &size));
  for (auto& slot : slots) {
    xp_native_sd_file_close(&slot);
    xp_native_sd_file_close(&slot);
    xp_native_sd_handle_destroy(&slot);
  }
}
}  // namespace hal_contract
