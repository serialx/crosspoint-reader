#pragma once
#include <xp_font_registry.h>

#include <atomic>
namespace protection_test {
inline XpLoadedFonts fonts;
inline const void* fontSlot = &fonts;
inline std::atomic<uint32_t> generation{0};
}  // namespace protection_test
namespace freeink {
struct MemoryManager {
  static MemoryManager& instance() {
    static MemoryManager value;
    return value;
  }
  void clearCaches() {
    xp_font_cache_release(&protection_test::fonts, XP_FONT_EVICT_RENDER);
    xp_font_cache_release(&protection_test::fonts, XP_FONT_EVICT_SD);
  }
};
}  // namespace freeink
