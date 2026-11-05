#pragma once
#include <cstdint>
namespace HalDisplay {
enum Mode { HALF_REFRESH, FAST_REFRESH };
enum class GrayscaleMode { Absolute, Overlay };
enum class GrayscaleBase { Separate, Combined };
enum class Controller { UC8279, UC8253, SSD1677, Other };
}  // namespace HalDisplay
struct FrameCapabilities {
  HalDisplay::GrayscaleBase base = HalDisplay::GrayscaleBase::Separate;
  bool stripUploads = false, asyncBase = false;
  bool supported() const { return false; }
};
