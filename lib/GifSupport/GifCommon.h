#pragma once

#include <Arduino.h>
#include <HalStorage.h>
#include <xp_ffi.h>

using GifBasicInfo = XpGifInfo;

namespace GifCommon {
struct Source {
  HalFile& file;
  uint32_t lastYieldMs = 0;
  static void poll(void* context) {
    auto& source = *static_cast<Source*>(context);
    const uint32_t now = millis();
    if (now - source.lastYieldMs >= 250) {
      source.lastYieldMs = now;
      vTaskDelay(1);
    }
  }
  static size_t read(void* context, uint32_t offset, uint8_t* output, size_t len) {
    auto& source = *static_cast<Source*>(context);
    if (source.file.position() != offset && !source.file.seek(offset)) return 0;
    const int count = source.file.read(output, len);
    poll(context);
    return count > 0 ? static_cast<size_t>(count) : 0;
  }
  XpGifInput input() { return {this, read, poll}; }
};
inline bool readBasicInfo(HalFile& file, GifBasicInfo& info) {
  Source source{file};
  const auto input = source.input();
  const bool success = xp_gif_inspect(&input, &info);
  return file.seek(0) && success;
}
}  // namespace GifCommon
