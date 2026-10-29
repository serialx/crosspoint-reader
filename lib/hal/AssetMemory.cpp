#include <xp_ffi.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Same DEFAULT allocation domain as XpDisplayPlatform: malloc may use PSRAM.
extern "C" void* xp_assets_platform_alloc(size_t bytes, uint32_t caps) {
  return caps == 3 ? std::malloc(bytes) : nullptr;
}
extern "C" void* xp_assets_platform_realloc(void* data, size_t bytes, uint32_t caps) {
  return caps == 3 ? std::realloc(data, bytes) : nullptr;
}
extern "C" void xp_assets_platform_free(void* data) { std::free(data); }
