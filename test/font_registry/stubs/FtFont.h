#pragma once
#include <cstdint>
namespace freeink::font {
class FtFont {
 public:
  struct FaceInfo {
    uint16_t weight = 400;
    bool italic = false;
  };
  enum class InspectResult { Ok, Invalid };
  using ReadFn = unsigned long (*)(void*, unsigned long, unsigned char*, unsigned long);
  static InspectResult inspectStream(ReadFn, void*, unsigned long, FaceInfo&) { return InspectResult::Invalid; }
};
}  // namespace freeink::font
