  FontCacheManager frameCache;
  FontCacheManager* getFontCacheManager() const { return const_cast<FontCacheManager*>(&frameCache); }
  FrameCapabilities grayscaleCapabilities(HalDisplay::GrayscaleMode = HalDisplay::GrayscaleMode::Overlay) const { return {}; }
  int getScreenHeight() const { return getDisplayHeight(); }
  void displayBuffer(HalDisplay::Mode) {}
  void displayBufferAsync(HalDisplay::Mode) {}
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode, HalDisplay::Mode) { return false; }
  void displayGrayscaleBase(HalDisplay::Mode) {}
  void clearScreen(uint8_t) {}
  void preconditionGrayscale() {}
  void setRenderMode(RenderMode) {}
  void beginStripTarget(uint8_t*, int, int) {}
  void endStripTarget() {}
  void waitRefreshComplete() {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, int, int) {}
  void displayGrayBuffer() {}
  void cleanupGrayscaleWithFrameBuffer() {}
  bool storeBwBuffer() { return false; }
  void restoreBwBuffer() {}
  void copyGrayscaleLsbBuffers() {}
  void copyGrayscaleMsbBuffers() {}
