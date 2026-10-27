#pragma once
#include <array>

#include "Arduino.h"
class HalDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 800, DISPLAY_HEIGHT = 480, DISPLAY_WIDTH_BYTES = 100;
  static constexpr uint32_t BUFFER_SIZE = 48000;
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum class GrayscaleMode { Overlay };
  enum class GrayscaleBase { Combined };
  struct GrayscaleCapabilities {
    bool asyncBase = false;
    bool stripUploads = false;
    GrayscaleBase base{};
  };
  std::array<uint8_t, BUFFER_SIZE> pixels{};
  uint8_t* getFrameBuffer() { return pixels.data(); }
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
  void clearScreen(uint8_t value) { pixels.fill(value); }
  uint8_t* lendFrameBufferStorage(uint32_t* size) {
    *size = BUFFER_SIZE;
    return pixels.data();
  }
  void returnFrameBufferStorage() {}
  bool isInverted() const { return false; }
  bool supportsAsyncRefresh() const { return false; }
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode) const { return {}; }
  void displayBuffer(RefreshMode, bool = false) {}
  void displayBufferAsync(RefreshMode) {}
  void waitRefreshComplete() {}
  void drawImage(const uint8_t*, int, int, int, int) {}
  void displayGrayscaleBase(RefreshMode, bool) {}
  bool displayGrayscaleBase(GrayscaleMode, RefreshMode, bool) { return false; }
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool) {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
};
