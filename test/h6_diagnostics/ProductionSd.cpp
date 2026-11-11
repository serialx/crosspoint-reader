#include <Logging.h>
#include <xp_sd_physical.h>

#include <cstdint>
#include <cstdio>

int main() {
  const uint32_t args[]{65, 66, 67, 68};
  const uint32_t ranges[][2]{{1, 39}, {0x101, 0x10c}, {0x200, 0x208}, {0x300, 0x300}, {0x401, 0x40a}, {0x501, 0x50f}};
  for (const auto& range : ranges) {
    for (uint32_t event = range[0]; event <= range[1]; ++event) {
      for (bool seekFailed : {false, true}) {
        h6CandidateLogs.clear();
        xp_native_sd_diagnostic("Family/path.cpfont", event, args, 2, 3, seekFailed, "Other");
        std::printf("%03x seek=%d logs=%zu\n", event, seekFailed, h6CandidateLogs.size());
        for (const auto& line : h6CandidateLogs) std::puts(line.c_str());
      }
    }
  }
}
