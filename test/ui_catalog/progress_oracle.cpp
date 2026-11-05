// Frozen redraw/sleep predicates from CatalogActivity at 31d5cd46.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
struct Catalog {
  int lastRenderedPercent = -1;
  uint32_t lastProgressUpdateMs = 0;
  bool progress(uint32_t downloaded, uint32_t total, uint32_t now) {
    const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
    if (percent >= 100 || lastRenderedPercent < 0 || percent >= lastRenderedPercent + 5 ||
        now - lastProgressUpdateMs >= 5000) {
      lastRenderedPercent = percent;
      lastProgressUpdateMs = now;
      return true;
    }
    return false;
  }
};
int main() {
  for (uint32_t total : {0u, 100u, 4194304u}) {
    for (uint32_t start : {0u, 0xfffff000u}) {
      Catalog state;
      for (uint32_t step = 0; step <= 110; ++step) {
        const uint32_t downloaded = total ? total * step / 100 : step * 1024;
        const uint32_t now = start + step * 100;
        std::printf("progress %u %u %u %u %d\n", total, start, downloaded, now, state.progress(downloaded, total, now));
      }
    }
  }
  for (int state : {0, 1, 2, 3, 4, 5, 6, 11}) {
    const bool awake = state == 0 || state == 1 || state == 2 || state == 3 || state == 5 || state == 11;
    std::printf("awake %d %d\n", state, awake);
  }
}
