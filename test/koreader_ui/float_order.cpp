#include <xp_ui_koreader.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
static_assert(sizeof(XpKoState) == 12 && sizeof(XpKoOptions) == 12);
static_assert(offsetof(XpKoOptions, local_percentage) == 0 && offsetof(XpKoOptions, smart) == 4);
static uint32_t call(void* context, uint32_t operation, uint32_t, uint32_t value) {
  if (operation == XP_KO_UPLOAD_BUILD) *static_cast<uint32_t*>(context) = value;
  return 0;
}
int main() {
  uint32_t seed = 1;
  for (unsigned i = 0; i < 100000; ++i) {
    seed = seed * 1664525u + 1013904223u;
    float value;
    std::memcpy(&value, &seed, sizeof(value));
    if (!std::isfinite(value)) continue;
    float pct = value;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    const auto expected = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
    uint32_t actual = UINT32_MAX;
    XpKoServices services{&actual, call};
    XpKoOptions options{};
    options.local_percentage = value;
    options.rich = 1;
    xp_ui_koreader_upload(&options, &services);
    assert(actual == expected);
  }
}
