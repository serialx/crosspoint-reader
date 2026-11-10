#include <SdCardFontRegistry.h>

#include <cstdio>
int main() {
  SdCardFontRegistry registry;
  registry.discover();
  for (const auto& family : registry.getFamilies()) {
    std::printf("FAMILY %s %d %zu\n", family.name.c_str(), family.vector, family.files.size());
    for (const auto& file : family.files) {
      std::printf("FILE %s %u %u\n", file.name ? file.name.get() : "canonical", static_cast<unsigned>(file.pointSize),
                  static_cast<unsigned>(file.style));
    }
  }
}
