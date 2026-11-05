#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#define private public
#include <SdCardFontRegistry.h>
#undef private
void record(const std::string& name) {
  uint8_t size = 77, style = 88;
  const bool ok = SdCardFontRegistry::parseFilename(name.c_str(), size, style);
  std::printf("%d %u %u %d\n", ok, size, style,
              SdCardFontRegistry::isCanonicalCpfontName(name.c_str(), "Family", size));
}
int main() {
  for (const std::string stem :
       std::vector<std::string>{"", "Family", "_", "a_b", std::string(127, 'a'), std::string(128, 'a')}) {
    for (const char* size : {"", "0", "1", "14", "255", "256", "-1", "+14", " 14", "\t14", "14 ", "00014",
                             "999999999999999999999999999999999", "0x10"}) {
      for (const char* ext : {".cpfont", ".CPFONT", ".cpfont.tmp", ""}) record(stem + "_" + size + ext);
    }
  }
  std::string name = "Family_14.cpfont";
  for (size_t i = 0; i < name.size(); ++i) {
    for (unsigned c = 1; c < 256; ++c) {
      auto mutation = name;
      mutation[i] = static_cast<char>(c);
      record(mutation);
    }
  }
}
